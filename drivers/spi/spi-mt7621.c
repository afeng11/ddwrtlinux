/*
 * spi-mt7621.c -- MediaTek MT7621 SPI controller driver
 *
 * Copyright (C) 2011 Sergiy <piratfm@gmail.com>
 * Copyright (C) 2011-2013 Gabor Juhos <juhosg@openwrt.org>
 * Copyright (C) 2014-2015 Felix Fietkau <nbd@openwrt.org>
 *
 * Some parts are based on spi-orion.c:
 *   Author: Shadi Ammouri <shadi@marvell.com>
 *   Copyright (C) 2007-2008 Marvell Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/reset.h>
#include <linux/spi/spi.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/swab.h>

#include <ralink_regs.h>

#define SPI_BPW_MASK(bits) BIT((bits) - 1)

#define DRIVER_NAME			"spi-mt7621"
/* in usec */
#define RALINK_SPI_WAIT_MAX_LOOP	2000

/* SPISTAT register bit field */
#define SPISTAT_BUSY			BIT(0)

#define MT7621_SPI_TRANS	0x00
#define SPITRANS_BUSY		BIT(16)

#define MT7621_SPI_OPCODE	0x04
#define MT7621_SPI_DATA0	0x08
#define SPI_CTL_TX_RX_CNT_MASK	0xff
#define SPI_CTL_START		BIT(8)

#define MT7621_SPI_POLAR	0x38
#define MT7621_SPI_MASTER	0x28
#define MT7621_SPI_MOREBUF	0x2c
#define MT7621_SPI_SPACE	0x3c

#define RT2880_SPI_MODE_BITS	(SPI_CPOL | SPI_CPHA | SPI_LSB_FIRST | SPI_CS_HIGH)

struct mt7621_spi;

struct mt7621_spi {
	struct spi_master	*master;
	void __iomem		*base;
	unsigned int		sys_freq;
	unsigned int		speed;
	struct clk		*clk;
	spinlock_t		lock;

	struct mt7621_spi_ops	*ops;
};

static inline struct mt7621_spi *spidev_to_mt7621_spi(struct spi_device *spi)
{
	return spi_master_get_devdata(spi->master);
}

static inline u32 mt7621_spi_read(struct mt7621_spi *rs, u32 reg)
{
	return ioread32(rs->base + reg);
}

static inline void mt7621_spi_write(struct mt7621_spi *rs, u32 reg, u32 val)
{
	iowrite32(val, rs->base + reg);
}

static void mt7621_spi_set_cs(struct spi_device *spi, int enable)
{
	struct mt7621_spi *rs = spidev_to_mt7621_spi(spi);
	u32 polar = mt7621_spi_read(rs, MT7621_SPI_POLAR);

	if (enable)
		polar |= 1;
	else
		polar &= ~1;
	mt7621_spi_write(rs, MT7621_SPI_POLAR, polar);
}

static inline int mt7621_spi_wait_till_ready(struct spi_device *spi)
{
	struct mt7621_spi *rs = spidev_to_mt7621_spi(spi);
	int i;

	for (i = 0; i < RALINK_SPI_WAIT_MAX_LOOP; i++) {
		u32 status;

		status = mt7621_spi_read(rs, MT7621_SPI_TRANS);
		if ((status & SPITRANS_BUSY) == 0) {
			return 0;
		}
		cpu_relax();
		udelay(1);
	}

	return -ETIMEDOUT;
}

static int mt7621_spi_transfer_one_message(struct spi_master *master,
					   struct spi_message *m)
{
	struct mt7621_spi *rs = spi_master_get_devdata(master);
	struct spi_device *spi = m->spi;
	struct spi_transfer *t = NULL;
	int status = 0;
	int i, len = 0;
	int rx_len = 0;
	u32 data[9] = { 0 };
	u32 val;

	mt7621_spi_wait_till_ready(spi);

	list_for_each_entry(t, &m->transfers, transfer_list) {
		const u8 *buf = t->tx_buf;

		if (t->rx_buf)
			rx_len += t->len;

		if (!buf)
			continue;

		if (WARN_ON(len + t->len > 36)) {
			status = -EIO;
			goto msg_done;
		}

		for (i = 0; i < t->len; i++, len++)
			data[len / 4] |= buf[i] << (8 * (len & 3));
	}

	if (WARN_ON(rx_len > 32)) {
		status = -EIO;
		goto msg_done;
	}

	data[0] = swab32(data[0]);
	if (len < 4)
		data[0] >>= (4 - len) * 8;

	for (i = 0; i < len; i += 4)
		mt7621_spi_write(rs, MT7621_SPI_OPCODE + i, data[i / 4]);

	val = (min_t(int, len, 4) * 8) << 24;
	if (len > 4)
		val |= (len - 4) * 8;
	val |= (rx_len * 8) << 12;
	mt7621_spi_write(rs, MT7621_SPI_MOREBUF, val);

	mt7621_spi_set_cs(spi, 1);

	val = mt7621_spi_read(rs, MT7621_SPI_TRANS);
	val |= SPI_CTL_START;
	mt7621_spi_write(rs, MT7621_SPI_TRANS, val);

	mt7621_spi_wait_till_ready(spi);

	mt7621_spi_set_cs(spi, 0);

	for (i = 0; i < rx_len; i += 4)
		data[i / 4] = mt7621_spi_read(rs, MT7621_SPI_DATA0 + i);

	m->actual_length = len + rx_len;

	len = 0;
	list_for_each_entry(t, &m->transfers, transfer_list) {
		u8 *buf = t->rx_buf;

		if (!buf)
			continue;

		for (i = 0; i < t->len; i++, len++)
			buf[i] = data[len / 4] >> (8 * (len & 3));
	}

msg_done:
	m->status = status;
	spi_finalize_current_message(master);

	return 0;
}

static int mt7621_spi_setup(struct spi_device *spi)
{
	return 0;
}

static void mt7621_spi_reset(struct mt7621_spi *rs)
{
	u32 master = mt7621_spi_read(rs, MT7621_SPI_MASTER);

	master &= ~(0xfff << 16);
	master |= 13 << 16;
	master |= 7 << 29;
	master |= 1 << 2;

	mt7621_spi_write(rs, MT7621_SPI_MASTER, master);
}

static const struct of_device_id mt7621_spi_match[] = {
	{ .compatible = "ralink,mt7621-spi" },
	{},
};
MODULE_DEVICE_TABLE(of, mt7621_spi_match);

static int mt7621_spi_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	struct spi_master *master;
	struct mt7621_spi *rs;
	unsigned long flags;
	void __iomem *base;
	struct resource *r;
	int status = 0;
	struct clk *clk;
	struct mt7621_spi_ops *ops;

	match = of_match_device(mt7621_spi_match, &pdev->dev);
	if (!match)
		return -EINVAL;
	ops = (struct mt7621_spi_ops *)match->data;

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(&pdev->dev, r);
	if (IS_ERR(base))
		return PTR_ERR(base);

	clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(clk)) {
		dev_err(&pdev->dev, "unable to get SYS clock, err=%d\n",
			status);
		return PTR_ERR(clk);
	}

	status = clk_prepare_enable(clk);
	if (status)
		return status;

	master = spi_alloc_master(&pdev->dev, sizeof(*rs));
	if (master == NULL) {
		dev_dbg(&pdev->dev, "master allocation failed\n");
		return -ENOMEM;
	}

	master->mode_bits = RT2880_SPI_MODE_BITS;

	master->setup = mt7621_spi_setup;
	master->transfer_one_message = mt7621_spi_transfer_one_message;
	master->bits_per_word_mask = SPI_BPW_MASK(8);
	master->dev.of_node = pdev->dev.of_node;
	master->num_chipselect = 1;

	dev_set_drvdata(&pdev->dev, master);

	rs = spi_master_get_devdata(master);
	rs->base = base;
	rs->clk = clk;
	rs->master = master;
	rs->sys_freq = clk_get_rate(rs->clk);
	rs->ops = ops;
	dev_dbg(&pdev->dev, "sys_freq: %u\n", rs->sys_freq);
	spin_lock_irqsave(&rs->lock, flags);

	device_reset(&pdev->dev);

	mt7621_spi_reset(rs);

	return spi_register_master(master);
}

static int mt7621_spi_remove(struct platform_device *pdev)
{
	struct spi_master *master;
	struct mt7621_spi *rs;

	master = dev_get_drvdata(&pdev->dev);
	rs = spi_master_get_devdata(master);

	clk_disable(rs->clk);
	spi_unregister_master(master);

	return 0;
}

MODULE_ALIAS("platform:" DRIVER_NAME);

static struct platform_driver mt7621_spi_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
		.of_match_table = mt7621_spi_match,
	},
	.probe = mt7621_spi_probe,
	.remove = mt7621_spi_remove,
};

module_platform_driver(mt7621_spi_driver);

MODULE_DESCRIPTION("MT7621 SPI driver");
MODULE_AUTHOR("Felix Fietkau <nbd@openwrt.org>");
MODULE_LICENSE("GPL");
