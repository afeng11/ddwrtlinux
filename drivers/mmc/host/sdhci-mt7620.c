/* Copyright Statement:
 *
 * This software/firmware and related documentation ("MediaTek Software") are
 * protected under relevant copyright laws. The information contained herein
 * is confidential and proprietary to MediaTek Inc. and/or its licensors.
 * Without the prior written permission of MediaTek inc. and/or its licensors,
 * any reproduction, modification, use or disclosure of MediaTek Software,
 * and information contained herein, in whole or in part, shall be strictly prohibited.
 *
 * MediaTek Inc. (C) 2010. All rights reserved.
 *
 * BY OPENING THIS FILE, RECEIVER HEREBY UNEQUIVOCALLY ACKNOWLEDGES AND AGREES
 * THAT THE SOFTWARE/FIRMWARE AND ITS DOCUMENTATIONS ("MEDIATEK SOFTWARE")
 * RECEIVED FROM MEDIATEK AND/OR ITS REPRESENTATIVES ARE PROVIDED TO RECEIVER ON
 * AN "AS-IS" BASIS ONLY. MEDIATEK EXPRESSLY DISCLAIMS ANY AND ALL WARRANTIES,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE OR NONINFRINGEMENT.
 * NEITHER DOES MEDIATEK PROVIDE ANY WARRANTY WHATSOEVER WITH RESPECT TO THE
 * SOFTWARE OF ANY THIRD PARTY WHICH MAY BE USED BY, INCORPORATED IN, OR
 * SUPPLIED WITH THE MEDIATEK SOFTWARE, AND RECEIVER AGREES TO LOOK ONLY TO SUCH
 * THIRD PARTY FOR ANY WARRANTY CLAIM RELATING THERETO. RECEIVER EXPRESSLY ACKNOWLEDGES
 * THAT IT IS RECEIVER'S SOLE RESPONSIBILITY TO OBTAIN FROM ANY THIRD PARTY ALL PROPER LICENSES
 * CONTAINED IN MEDIATEK SOFTWARE. MEDIATEK SHALL ALSO NOT BE RESPONSIBLE FOR ANY MEDIATEK
 * SOFTWARE RELEASES MADE TO RECEIVER'S SPECIFICATION OR TO CONFORM TO A PARTICULAR
 * STANDARD OR OPEN FORUM. RECEIVER'S SOLE AND EXCLUSIVE REMEDY AND MEDIATEK'S ENTIRE AND
 * CUMULATIVE LIABILITY WITH RESPECT TO THE MEDIATEK SOFTWARE RELEASED HEREUNDER WILL BE,
 * AT MEDIATEK'S OPTION, TO REVISE OR REPLACE THE MEDIATEK SOFTWARE AT ISSUE,
 * OR REFUND ANY SOFTWARE LICENSE FEES OR SERVICE CHARGE PAID BY RECEIVER TO
 * MEDIATEK FOR SUCH MEDIATEK SOFTWARE AT ISSUE.
 *
 * The following software/firmware and/or related documentation ("MediaTek Software")
 * have been modified by MediaTek Inc. All revisions are subject to any receiver's
 * applicable license agreements with MediaTek Inc.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/ioport.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/blkdev.h>
#include <linux/slab.h>
#include <linux/mmc/host.h>
#include <linux/mmc/card.h>
#include <linux/mmc/core.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/sd.h>
#include <linux/mmc/sdio.h>
#include <linux/dma-mapping.h>

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/pm.h>

#define MSDC_SMPL_FALLING   (1)
#define MSDC_CD_PIN_EN      (1 << 0)  /* card detection pin is wired   */
#define MSDC_WP_PIN_EN      (1 << 1)  /* write protection pin is wired */
#define MSDC_REMOVABLE      (1 << 5)  /* removable slot                */
#define MSDC_SYS_SUSPEND    (1 << 6)  /* suspended by system           */
#define MSDC_HIGHSPEED      (1 << 7)

#define IRQ_SDC 22

#include <asm/dma.h>

#include "mt6575_sd.h"

#define DRV_NAME            "mtk-sd"

#define HOST_MAX_NUM        (1) /* +/- by chhung */

#define HOST_MAX_MCLK       (48000000) /* +/- by chhung */
#define HOST_MIN_MCLK       (260000)

#define HOST_MAX_BLKSZ      (2048)

#define MSDC_OCR_AVAIL      (MMC_VDD_28_29 | MMC_VDD_29_30 | MMC_VDD_30_31 | MMC_VDD_31_32 | MMC_VDD_32_33)

#define GPIO_PULL_DOWN      (0)
#define GPIO_PULL_UP        (1)

#define DEFAULT_DEBOUNCE    (8)       /* 8 cycles */
#define DEFAULT_DTOC        (40)      /* data timeout counter. 65536x40 sclk. */

#define CMD_TIMEOUT         (HZ/10)     /* 100ms */
#define DAT_TIMEOUT         (HZ/2 * 5)  /* 500ms x5 */

#define MAX_DMA_CNT         (64 * 1024 - 512)   /* a single transaction for WIFI may be 50K*/

#define MAX_GPD_NUM         (1 + 1)  /* one null gpd */
#define MAX_BD_NUM          (1024)
#define MAX_BD_PER_GPD      (MAX_BD_NUM)

#define MAX_HW_SGMTS        (MAX_BD_NUM)
#define MAX_PHY_SGMTS       (MAX_BD_NUM)
#define MAX_SGMT_SZ         (MAX_DMA_CNT)
#define MAX_REQ_SZ          (MAX_SGMT_SZ * 8)  

#ifdef MT6575_SD_DEBUG
static struct msdc_regs *msdc_reg[HOST_MAX_NUM];
#endif 

//=================================
#define PERI_MSDC0_PDN      (15)
//#define PERI_MSDC1_PDN    (16)
//#define PERI_MSDC2_PDN    (17)
//#define PERI_MSDC3_PDN    (18)

struct msdc_host *msdc_6575_host[] = {NULL,NULL,NULL,NULL};

struct msdc_hw msdc0_hw = {
	.clk_src        = 0,
	.cmd_edge       = MSDC_SMPL_FALLING,
	.data_edge      = MSDC_SMPL_FALLING,
	.clk_drv        = 4,
	.cmd_drv        = 4,
	.dat_drv        = 4,
	.data_pins      = 4,
	.data_offset    = 0,
	.flags          = MSDC_SYS_SUSPEND | MSDC_WP_PIN_EN | MSDC_CD_PIN_EN | MSDC_REMOVABLE | MSDC_HIGHSPEED,
};

static struct resource mtk_sd_resources[] = {
	[0] = {
		.start  = 0xb0130000,
		.end    = 0xb0133fff,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = IRQ_SDC,	/*FIXME*/
		.end    = IRQ_SDC,	/*FIXME*/
		.flags  = IORESOURCE_IRQ,
	},
};

static struct platform_device mtk_sd_device = {
	.name           = "mtk-sd",
	.id             = 0,
	.num_resources  = ARRAY_SIZE(mtk_sd_resources),
	.resource       = mtk_sd_resources,
};
/* end of +++ */

static int msdc_rsp[] = {
    0,  /* RESP_NONE */
    1,  /* RESP_R1 */
    2,  /* RESP_R2 */
    3,  /* RESP_R3 */
    4,  /* RESP_R4 */
    1,  /* RESP_R5 */
    1,  /* RESP_R6 */
    1,  /* RESP_R7 */
    7,  /* RESP_R1b */
};

/* For Inhanced DMA */
#define msdc_init_gpd_ex(gpd,extlen,cmd,arg,blknum) \
    do { \
        ((gpd_t*)gpd)->extlen = extlen; \
        ((gpd_t*)gpd)->cmd    = cmd; \
        ((gpd_t*)gpd)->arg    = arg; \
        ((gpd_t*)gpd)->blknum = blknum; \
    }while(0)
    
#define msdc_init_bd(bd, blkpad, dwpad, dptr, dlen) \
    do { \
        BUG_ON(dlen > 0xFFFFUL); \
        ((bd_t*)bd)->blkpad = blkpad; \
        ((bd_t*)bd)->dwpad  = dwpad; \
        ((bd_t*)bd)->ptr    = (void*)dptr; \
        ((bd_t*)bd)->buflen = dlen; \
    }while(0)

#define msdc_txfifocnt()   ((sdr_read32(MSDC_FIFOCS) & MSDC_FIFOCS_TXCNT) >> 16)
#define msdc_rxfifocnt()   ((sdr_read32(MSDC_FIFOCS) & MSDC_FIFOCS_RXCNT) >> 0)
#define msdc_fifo_write32(v)   sdr_write32(MSDC_TXDATA, (v))
#define msdc_fifo_write8(v)    sdr_write8(MSDC_TXDATA, (v))
#define msdc_fifo_read32()   sdr_read32(MSDC_RXDATA)
#define msdc_fifo_read8()    sdr_read8(MSDC_RXDATA)	


#define msdc_dma_on()        sdr_clr_bits(MSDC_CFG, MSDC_CFG_PIO)
#define msdc_dma_off()       sdr_set_bits(MSDC_CFG, MSDC_CFG_PIO)

#define msdc_retry(expr,retry,cnt) \
    do { \
        int backup = cnt; \
        while (retry) { \
            if (!(expr)) break; \
            if (cnt-- == 0) { \
                retry--; mdelay(1); cnt = backup; \
            } \
        } \
        WARN_ON(retry == 0); \
    } while(0)

#if 0 /* +/- chhung */
#define msdc_reset() \
    do { \
        int retry = 3, cnt = 1000; \
        sdr_set_bits(MSDC_CFG, MSDC_CFG_RST); \
        dsb(); \
        msdc_retry(sdr_read32(MSDC_CFG) & MSDC_CFG_RST, retry, cnt); \
    } while(0)
#else
#define msdc_reset() \
    do { \
        int retry = 3, cnt = 1000; \
        sdr_set_bits(MSDC_CFG, MSDC_CFG_RST); \
        msdc_retry(sdr_read32(MSDC_CFG) & MSDC_CFG_RST, retry, cnt); \
    } while(0)
#endif /* end of +/- */

#define msdc_clr_int() \
    do { \
        volatile u32 val = sdr_read32(MSDC_INT); \
        sdr_write32(MSDC_INT, val); \
    } while(0)

#define msdc_clr_fifo() \
    do { \
        int retry = 3, cnt = 1000; \
        sdr_set_bits(MSDC_FIFOCS, MSDC_FIFOCS_CLR); \
        msdc_retry(sdr_read32(MSDC_FIFOCS) & MSDC_FIFOCS_CLR, retry, cnt); \
    } while(0)

#define msdc_irq_save(val) \
    do { \
        val = sdr_read32(MSDC_INTEN); \
        sdr_clr_bits(MSDC_INTEN, val); \
    } while(0)
	
#define msdc_irq_restore(val) \
    do { \
        sdr_set_bits(MSDC_INTEN, val); \
    } while(0)

/* clock source for host: global */
static u32 hclks[] = {48000000}; /* +/- by chhung */

//============================================
// the power for msdc host controller: global
//    always keep the VMC on. 
//============================================
#define msdc_vcore_on(host) \
    do { \
        printk("[+]VMC ref. count<%d>\n", ++host->pwr_ref); \
        (void)hwPowerOn(MT65XX_POWER_LDO_VMC, VOL_3300, "SD"); \
    } while (0)
#define msdc_vcore_off(host) \
    do { \
        printk("[-]VMC ref. count<%d>\n", --host->pwr_ref); \
        (void)hwPowerDown(MT65XX_POWER_LDO_VMC, "SD"); \
    } while (0)

//====================================
// the vdd output for card: global 
//   always keep the VMCH on. 
//==================================== 
#define msdc_vdd_on(host) \
    do { \
        (void)hwPowerOn(MT65XX_POWER_LDO_VMCH, VOL_3300, "SD"); \
    } while (0)   
#define msdc_vdd_off(host) \
    do { \
        (void)hwPowerDown(MT65XX_POWER_LDO_VMCH, "SD"); \
    } while (0)      

#define sdc_is_busy()          (sdr_read32(SDC_STS) & SDC_STS_SDCBUSY)
#define sdc_is_cmd_busy()      (sdr_read32(SDC_STS) & SDC_STS_CMDBUSY)

#define sdc_send_cmd(cmd,arg) \
    do { \
        sdr_write32(SDC_ARG, (arg)); \
        sdr_write32(SDC_CMD, (cmd)); \
    } while(0)

// can modify to read h/w register.
//#define is_card_present(h)   ((sdr_read32(MSDC_PS) & MSDC_PS_CDSTS) ? 0 : 1);
#define is_card_present(h)     (((struct msdc_host*)(h))->card_inserted)

/* +++ chhung */
#ifndef __ASSEMBLY__
#define PHYSADDR(a)             (((unsigned long)(a)) & 0x1fffffff)
#else
#define PHYSADDR(a)             ((a) & 0x1fffffff)
#endif
/* end of +++ */
static unsigned int msdc_do_command(struct msdc_host   *host, 
                                      struct mmc_command *cmd,
                                      int                 tune,
                                      unsigned long       timeout);  
                                     
static int msdc_tune_cmdrsp(struct msdc_host*host,struct mmc_command *cmd);

#ifdef MT6575_SD_DEBUG
static void msdc_dump_card_status(struct msdc_host *host, u32 status)
{
    static char *state[] = {
        "Idle",			/* 0 */
        "Ready",		/* 1 */
        "Ident",		/* 2 */
        "Stby",			/* 3 */
        "Tran",			/* 4 */
        "Data",			/* 5 */
        "Rcv",			/* 6 */
        "Prg",			/* 7 */
        "Dis",			/* 8 */
        "Reserved",		/* 9 */
        "Reserved",		/* 10 */
        "Reserved",		/* 11 */
        "Reserved",		/* 12 */
        "Reserved",		/* 13 */
        "Reserved",		/* 14 */
        "I/O mode",		/* 15 */
    };
    if (status & R1_OUT_OF_RANGE)
        printk("[CARD_STATUS] Out of Range\n");
    if (status & R1_ADDRESS_ERROR)
        printk("[CARD_STATUS] Address Error\n");
    if (status & R1_BLOCK_LEN_ERROR)
        printk("[CARD_STATUS] Block Len Error\n");
    if (status & R1_ERASE_SEQ_ERROR)
        printk("[CARD_STATUS] Erase Seq Error\n");
    if (status & R1_ERASE_PARAM)
        printk("[CARD_STATUS] Erase Param\n");
    if (status & R1_WP_VIOLATION)
        printk("[CARD_STATUS] WP Violation\n");
    if (status & R1_CARD_IS_LOCKED)
        printk("[CARD_STATUS] Card is Locked\n");
    if (status & R1_LOCK_UNLOCK_FAILED)
        printk("[CARD_STATUS] Lock/Unlock Failed\n");
    if (status & R1_COM_CRC_ERROR)
        printk("[CARD_STATUS] Command CRC Error\n");
    if (status & R1_ILLEGAL_COMMAND)
        printk("[CARD_STATUS] Illegal Command\n");
    if (status & R1_CARD_ECC_FAILED)
        printk("[CARD_STATUS] Card ECC Failed\n");
    if (status & R1_CC_ERROR)
        printk("[CARD_STATUS] CC Error\n");
    if (status & R1_ERROR)
        printk("[CARD_STATUS] Error\n");
    if (status & R1_UNDERRUN)
        printk("[CARD_STATUS] Underrun\n");
    if (status & R1_OVERRUN)
        printk("[CARD_STATUS] Overrun\n");
    if (status & R1_CID_CSD_OVERWRITE)
        printk("[CARD_STATUS] CID/CSD Overwrite\n");
    if (status & R1_WP_ERASE_SKIP)
        printk("[CARD_STATUS] WP Eraser Skip\n");
    if (status & R1_CARD_ECC_DISABLED)
        printk("[CARD_STATUS] Card ECC Disabled\n");
    if (status & R1_ERASE_RESET)
        printk("[CARD_STATUS] Erase Reset\n");
    if (status & R1_READY_FOR_DATA)
        printk("[CARD_STATUS] Ready for Data\n");
    if (status & R1_SWITCH_ERROR)
        printk("[CARD_STATUS] Switch error\n");
    if (status & R1_APP_CMD)
        printk("[CARD_STATUS] App Command\n");
    
    printk("[CARD_STATUS] '%s' State\n", state[R1_CURRENT_STATE(status)]);
}

static void msdc_dump_ocr_reg(struct msdc_host *host, u32 resp)
{
    if (resp & (1 << 7))
        printk("[OCR] Low Voltage Range\n");
    if (resp & (1 << 15))
        printk("[OCR] 2.7-2.8 volt\n");
    if (resp & (1 << 16))
        printk("[OCR] 2.8-2.9 volt\n");
    if (resp & (1 << 17))
        printk("[OCR] 2.9-3.0 volt\n");
    if (resp & (1 << 18))
        printk("[OCR] 3.0-3.1 volt\n");
    if (resp & (1 << 19))
        printk("[OCR] 3.1-3.2 volt\n");
    if (resp & (1 << 20))
        printk("[OCR] 3.2-3.3 volt\n");
    if (resp & (1 << 21))
        printk("[OCR] 3.3-3.4 volt\n");
    if (resp & (1 << 22))
        printk("[OCR] 3.4-3.5 volt\n");
    if (resp & (1 << 23))
        printk("[OCR] 3.5-3.6 volt\n");
    if (resp & (1 << 24))
        printk("[OCR] Switching to 1.8V Accepted (S18A)\n");
    if (resp & (1 << 30))
        printk("[OCR] Card Capacity Status (CCS)\n");
    if (resp & (1 << 31))
        printk("[OCR] Card Power Up Status (Idle)\n");
    else
        printk("[OCR] Card Power Up Status (Busy)\n");
}

static void msdc_dump_rca_resp(struct msdc_host *host, u32 resp)
{
    u32 status = (((resp >> 15) & 0x1) << 23) |
                 (((resp >> 14) & 0x1) << 22) |
                 (((resp >> 13) & 0x1) << 19) |
                   (resp & 0x1fff);

    printk("[RCA] 0x%.4x\n", resp >> 16);
    
    msdc_dump_card_status(host, status);
}

static void msdc_dump_io_resp(struct msdc_host *host, u32 resp)
{
    u32 flags = (resp >> 8) & 0xFF;
    char *state[] = {"DIS", "CMD", "TRN", "RFU"};

    if (flags & (1 << 7))
        printk("[IO] COM_CRC_ERR\n");
    if (flags & (1 << 6))
        printk("[IO] Illgal command\n");
    if (flags & (1 << 3))
        printk("[IO] Error\n");
    if (flags & (1 << 2))
        printk("[IO] RFU\n");
    if (flags & (1 << 1))
        printk("[IO] Function number error\n");
    if (flags & (1 << 0))
        printk("[IO] Out of range\n");

    printk("[IO] State: %s, Data:0x%x\n", state[(resp >> 12) & 0x3], resp & 0xFF);
}
#endif

static void msdc_set_timeout(struct msdc_host *host, u32 ns, u32 clks)
{
	u32 base = host->base;
	u32 timeout, clk_ns;

	host->timeout_ns = ns;
	host->timeout_clks = clks;

	clk_ns = 1000000000UL / host->sclk;
	timeout = ns / clk_ns + clks;
	timeout = timeout >> 16; /* in 65536 sclk cycle unit */
	timeout = timeout > 1 ? timeout - 1 : 0;
	timeout = timeout > 255 ? 255 : timeout;

	sdr_set_field(SDC_CFG, SDC_CFG_DTOC, timeout);

/*	printk("Set read data timeout: %dns %dclks -> %d x 65536 cycles\n",
		ns, clks, timeout + 1);*/
}

static void msdc_eirq_sdio(void *data)
{
	struct msdc_host *host = (struct msdc_host *)data;

//	printk("SDIO EINT\n");

	mmc_signal_sdio_irq(host->mmc);
}

static void msdc_eirq_cd(void *data)
{
	struct msdc_host *host = (struct msdc_host *)data;

//	printk("CD EINT\n");

	tasklet_hi_schedule(&host->card_tasklet);
}

static void msdc_tasklet_card(unsigned long arg)
{
	struct msdc_host *host = (struct msdc_host *)arg;
	struct msdc_hw *hw = host->hw;
	u32 base = host->base;
	u32 inserted;
	u32 status = 0;

	spin_lock(&host->lock);

	if (hw->get_cd_status) {
		inserted = hw->get_cd_status();
	} else {
		status = sdr_read32(MSDC_PS);
		inserted = (status & MSDC_PS_CDSTS) ? 0 : 1;
	}

	host->card_inserted = inserted;

	if (!host->suspend) {
		host->mmc->f_max = HOST_MAX_MCLK;
		mmc_detect_change(host->mmc, msecs_to_jiffies(20));
	}

//	printk("card found<%s>\n", inserted ? "inserted" : "removed");

	spin_unlock(&host->lock);
}

static void msdc_set_mclk(struct msdc_host *host, int ddr, unsigned int hz)
{
	u32 base = host->base;
	u32 hclk = host->hclk;
	u32 mode, flags, div, sclk;

	if (!hz) {
//		printk("set mclk to 0!!!\n");
		msdc_reset();
		return;
	}

	msdc_irq_save(flags);

	if (ddr) {
		mode = 0x2;
		if (hz >= (hclk >> 2)) {
			div = 1;
			sclk = hclk >> 2;
		} else {
			div  = (hclk + ((hz << 2) - 1)) / (hz << 2);
			sclk = (hclk >> 2) / div;
		}
	} else if (hz >= hclk) {
		mode = 0x1;
		div = 0;
		sclk = hclk;
	} else {
		mode = 0x0;
		if (hz >= (hclk >> 1)) {
			div = 0;
			sclk = hclk >> 1;
		} else {
			div = (hclk + ((hz << 2) - 1)) / (hz << 2);
			sclk = (hclk >> 2) / div;
		}
	}

	sdr_set_field(MSDC_CFG, MSDC_CFG_CKMOD, mode);
	sdr_set_field(MSDC_CFG, MSDC_CFG_CKDIV, div);

	while (!(sdr_read32(MSDC_CFG) & MSDC_CFG_CKSTB));

	host->sclk = sclk;
	host->mclk = hz;
	msdc_set_timeout(host, host->timeout_ns, host->timeout_clks);

/*	printk("!!! Set<%dKHz> Source<%dKHz> -> sclk<%dKHz>\n",
		hz / 1000, hclk / 1000, sclk / 1000);
*/
	msdc_irq_restore(flags);
}

static void msdc_abort_data(struct msdc_host *host)
{
	u32 base = host->base;
	struct mmc_command *stop = host->mrq->stop;

//	printk("Need to Abort. dma<%d>\n", host->dma_xfer);

	msdc_reset();
	msdc_clr_fifo();
	msdc_clr_int();

	if (stop) {
//		printk("stop when abort CMD<%d>\n", stop->opcode);
		msdc_do_command(host, stop, 0, CMD_TIMEOUT);
	}
}

static unsigned int msdc_command_start(struct msdc_host *host,
		struct mmc_command *cmd, int tune, unsigned long timeout)
{
	u32 wints = MSDC_INT_CMDRDY | MSDC_INT_RSPCRCERR | MSDC_INT_CMDTMO |
		MSDC_INT_ACMDRDY | MSDC_INT_ACMDCRCERR | MSDC_INT_ACMDTMO |
		MSDC_INT_ACMD19_DONE;
	u32 base = host->base;
	u32 opcode = cmd->opcode;
	u32 rawcmd;
	u32 resp;
	unsigned long tmo;

	if (opcode == MMC_SEND_OP_COND || opcode == SD_APP_OP_COND)
		resp = RESP_R3;
	else if (opcode == MMC_SET_RELATIVE_ADDR || opcode == SD_SEND_RELATIVE_ADDR)
		resp = (mmc_cmd_type(cmd) == MMC_CMD_BCR) ? RESP_R6 : RESP_R1;
	else if (opcode == MMC_FAST_IO)
		resp = RESP_R4;
	else if (opcode == MMC_GO_IRQ_STATE)
		resp = RESP_R5;
	else if (opcode == MMC_SELECT_CARD)
		resp = (cmd->arg != 0) ? RESP_R1B : RESP_NONE;
	else if (opcode == SD_IO_RW_DIRECT || opcode == SD_IO_RW_EXTENDED)
		resp = RESP_R1;
	else if (opcode == SD_SEND_IF_COND && (mmc_cmd_type(cmd) == MMC_CMD_BCR))
		resp = RESP_R1;
	else {
		switch (mmc_resp_type(cmd)) {
		case MMC_RSP_R1:
			resp = RESP_R1;
			break;
		case MMC_RSP_R1B:
			resp = RESP_R1B;
			break;
		case MMC_RSP_R2:
			resp = RESP_R2;
			break;
		case MMC_RSP_R3:
			resp = RESP_R3;
			break;
		case MMC_RSP_NONE:
		default:
			resp = RESP_NONE;
			break;
		}
	}

	cmd->error = 0;
	rawcmd = opcode | msdc_rsp[resp] << 7 | host->blksz << 16;

	if (opcode == MMC_READ_MULTIPLE_BLOCK) {
		rawcmd |= (2 << 11);
	} else if (opcode == MMC_READ_SINGLE_BLOCK) {
		rawcmd |= (1 << 11);
	} else if (opcode == MMC_WRITE_MULTIPLE_BLOCK) {
		rawcmd |= ((2 << 11) | (1 << 13));
	} else if (opcode == MMC_WRITE_BLOCK) {
		rawcmd |= ((1 << 11) | (1 << 13));
	} else if (opcode == SD_IO_RW_EXTENDED) {
		if (cmd->data->flags & MMC_DATA_WRITE)
			rawcmd |= (1 << 13);
		if (cmd->data->blocks > 1)
			rawcmd |= (2 << 11);
		else
			rawcmd |= (1 << 11);
	} else if (opcode == SD_IO_RW_DIRECT && cmd->flags == (unsigned int)-1) {
		rawcmd |= (1 << 14);
	} else if ((opcode == SD_APP_SEND_SCR) ||
			(opcode == SD_APP_SEND_NUM_WR_BLKS) ||
			(opcode == SD_SWITCH && (mmc_cmd_type(cmd) == MMC_CMD_ADTC)) ||
			(opcode == SD_APP_SD_STATUS && (mmc_cmd_type(cmd) == MMC_CMD_ADTC)) ||
			(opcode == MMC_SEND_EXT_CSD && (mmc_cmd_type(cmd) == MMC_CMD_ADTC))) {
		rawcmd |= (1 << 11);
	} else if (opcode == MMC_STOP_TRANSMISSION) {
		rawcmd |= (1 << 14);
		rawcmd &= ~(0x0FFF << 16);
	}

//	printk("CMD<%d><0x%.8x> Arg<0x%.8x>\n", opcode , rawcmd, cmd->arg);

	tmo = jiffies + timeout;

	if (opcode == MMC_SEND_STATUS) {
		for (;;) {
			if (!sdc_is_cmd_busy())
				break;

			if (time_after(jiffies, tmo)) {
				//printk("XXX cmd_busy timeout: before CMD<%d>\n", opcode);
				cmd->error = (unsigned int)-ETIMEDOUT;
				msdc_reset();
				goto end;
			}
		}
	} else {
		for (;;) {
			if (!sdc_is_busy())
				break;
			if (time_after(jiffies, tmo)) {
				//printk("XXX sdc_busy timeout: before CMD<%d>\n", opcode);
				cmd->error = (unsigned int)-ETIMEDOUT;
				msdc_reset();
				goto end;
			}
		}
	}

	//BUG_ON(in_interrupt());
	host->cmd = cmd;
	host->cmd_rsp = resp;
	init_completion(&host->cmd_done);
	sdr_set_bits(MSDC_INTEN, wints);
	sdc_send_cmd(rawcmd, cmd->arg);

end:
	return cmd->error;
}

static unsigned int msdc_command_resp(struct msdc_host *host, struct mmc_command *cmd,
				int tune, unsigned long timeout)
{
	u32 base = host->base;
	//u32 opcode = cmd->opcode;
	u32 resp;
	u32 wints = MSDC_INT_CMDRDY | MSDC_INT_RSPCRCERR | MSDC_INT_CMDTMO |
		MSDC_INT_ACMDRDY | MSDC_INT_ACMDCRCERR | MSDC_INT_ACMDTMO |
		MSDC_INT_ACMD19_DONE;

	resp = host->cmd_rsp;

	BUG_ON(in_interrupt());
	spin_unlock(&host->lock);
	if (!wait_for_completion_timeout(&host->cmd_done, 10*timeout)) {
		//printk("XXX CMD<%d> wait_for_completion timeout ARG<0x%.8x>\n", opcode, cmd->arg);
		cmd->error = (unsigned int)-ETIMEDOUT;
		msdc_reset();
	}
	spin_lock(&host->lock);

	sdr_clr_bits(MSDC_INTEN, wints);
	host->cmd = NULL;

	if (!tune)
		return cmd->error;

	/* memory card CRC */
	if (host->hw->flags & MSDC_REMOVABLE && cmd->error == (unsigned int)(-EIO) ) {
		if (sdr_read32(SDC_CMD) & 0x1800) {
			msdc_abort_data(host);
		} else {
			msdc_reset();
			msdc_clr_fifo();
			msdc_clr_int();
		}
		cmd->error = msdc_tune_cmdrsp(host,cmd);
	}

	return cmd->error;
}

static unsigned int msdc_do_command(struct msdc_host *host, struct mmc_command *cmd,
			int tune, unsigned long timeout)
{
	if (!msdc_command_start(host, cmd, tune, timeout))
		msdc_command_resp(host, cmd, tune, timeout);

	//printk("        return<%d> resp<0x%.8x>\n", cmd->error, cmd->resp[0]);
	return cmd->error;
}

static int msdc_pio_abort(struct msdc_host *host, struct mmc_data *data, unsigned long tmo)
{
	u32  base = host->base;
	int  ret = 0;

	if (atomic_read(&host->abort))
		ret = 1;

	if (time_after(jiffies, tmo)) {
		data->error = (unsigned int)-ETIMEDOUT;
		//printk("XXX PIO Data Timeout: CMD<%d>\n", host->mrq->cmd->opcode);
		ret = 1;
	}

	if (ret) {
		msdc_reset();
		msdc_clr_fifo();
		msdc_clr_int();
		//printk("msdc pio find abort\n");
	}

	return ret;
}

static int msdc_pio_read(struct msdc_host *host, struct mmc_data *data)
{
	struct scatterlist *sg = data->sg;
	u32  base = host->base;
	u32  num = data->sg_len;
	u32 *ptr;
	u8  *u8ptr;
	u32  left;
	u32  count, size = 0;
	u32  wints = MSDC_INTEN_DATTMO | MSDC_INTEN_DATCRCERR;
	unsigned long tmo = jiffies + DAT_TIMEOUT;

	sdr_set_bits(MSDC_INTEN, wints);
	while (num) {
		left = sg_dma_len(sg);
		ptr = sg_virt(sg);
		while (left) {
			if ((left >=  MSDC_FIFO_THD) && (msdc_rxfifocnt() >= MSDC_FIFO_THD)) {
				count = MSDC_FIFO_THD >> 2;
				do {
					*ptr++ = msdc_fifo_read32();
				} while (--count);
				left -= MSDC_FIFO_THD;
			} else if ((left < MSDC_FIFO_THD) && msdc_rxfifocnt() >= left) {
				while (left > 3) {
					*ptr++ = msdc_fifo_read32();
					left -= 4;
				}

				u8ptr = (u8 *)ptr;
				while(left) {
					* u8ptr++ = msdc_fifo_read8();
					left--;
				}
			}

			if (msdc_pio_abort(host, data, tmo))
				goto end;
		}
		size += sg_dma_len(sg);
		sg = sg_next(sg); num--;
	}
end:
	data->bytes_xfered += size;
	//printk("        PIO Read<%d>bytes\n", size);

	sdr_clr_bits(MSDC_INTEN, wints);
	if(data->error)
		printk("read pio data->error<%d> left<%d> size<%d>\n", data->error, left, size);

	return data->error;
}

static int msdc_pio_write(struct msdc_host* host, struct mmc_data *data)
{
	u32  base = host->base;
	struct scatterlist *sg = data->sg;
	u32  num = data->sg_len;
	u32 *ptr;
	u8  *u8ptr;
	u32  left;
	u32  count, size = 0;
	u32  wints = MSDC_INTEN_DATTMO | MSDC_INTEN_DATCRCERR;
	unsigned long tmo = jiffies + DAT_TIMEOUT;

	sdr_set_bits(MSDC_INTEN, wints);
	while (num) {
		left = sg_dma_len(sg);
		ptr = sg_virt(sg);

		while (left) {
			if (left >= MSDC_FIFO_SZ && msdc_txfifocnt() == 0) {
				count = MSDC_FIFO_SZ >> 2;
				do {
					msdc_fifo_write32(*ptr); ptr++;
				} while (--count);
				left -= MSDC_FIFO_SZ;
			} else if (left < MSDC_FIFO_SZ && msdc_txfifocnt() == 0) {
				while (left > 3) {
					msdc_fifo_write32(*ptr); ptr++;
					left -= 4;
				}

				u8ptr = (u8*)ptr;
				while( left) {
					msdc_fifo_write8(*u8ptr);
					u8ptr++;
					left--;
				}
			}

			if (msdc_pio_abort(host, data, tmo))
				goto end;
		}
		size += sg_dma_len(sg);
		sg = sg_next(sg); num--;
	}
end:
	data->bytes_xfered += size;
	//printk("        PIO Write<%d>bytes\n", size);
	if(data->error)
		printk("write pio data->error<%d>\n", data->error);

	sdr_clr_bits(MSDC_INTEN, wints);

	return data->error;
}

static void msdc_dma_start(struct msdc_host *host)
{
	u32 base = host->base;
	u32 wints = MSDC_INTEN_XFER_COMPL | MSDC_INTEN_DATTMO | MSDC_INTEN_DATCRCERR;

	sdr_set_bits(MSDC_INTEN, wints);
	sdr_set_field(MSDC_DMA_CTRL, MSDC_DMA_CTRL_START, 1);

	//printk("DMA start\n");
}

static void msdc_dma_stop(struct msdc_host *host)
{
	u32 base = host->base;
	u32 wints = MSDC_INTEN_XFER_COMPL | MSDC_INTEN_DATTMO | MSDC_INTEN_DATCRCERR;

	//printk("DMA status: 0x%.8x\n",sdr_read32(MSDC_DMA_CFG));

	sdr_set_field(MSDC_DMA_CTRL, MSDC_DMA_CTRL_STOP, 1);
	while (sdr_read32(MSDC_DMA_CFG) & MSDC_DMA_CFG_STS);
	sdr_clr_bits(MSDC_INTEN, wints); /* Not just xfer_comp */

	//printk("DMA stop\n");
}

static u8 msdc_dma_calcs(u8 *buf, u32 len)
{
	u32 i, sum = 0;

	for (i = 0; i < len; i++)
		sum += buf[i];

	return 0xFF - (u8)sum;
}

static int msdc_dma_config(struct msdc_host *host, struct msdc_dma *dma)
{
	u32 base = host->base;
	u32 sglen = dma->sglen;
	u32 j, num, bdlen;
	u8  blkpad, dwpad, chksum;
	struct scatterlist *sg = dma->sg;
	gpd_t *gpd;
	bd_t *bd;

	switch (dma->mode) {
	case MSDC_MODE_DMA_BASIC:
		BUG_ON(dma->xfersz > 65535);
		BUG_ON(dma->sglen != 1);
		sdr_write32(MSDC_DMA_SA, PHYSADDR(sg_dma_address(sg)));
		sdr_set_field(MSDC_DMA_CTRL, MSDC_DMA_CTRL_LASTBUF, 1);
		sdr_set_field(MSDC_DMA_CTRL, MSDC_DMA_CTRL_XFERSZ, sg_dma_len(sg));
		sdr_set_field(MSDC_DMA_CTRL, MSDC_DMA_CTRL_BRUSTSZ, dma->burstsz);
		sdr_set_field(MSDC_DMA_CTRL, MSDC_DMA_CTRL_MODE, 0);
		break;

	case MSDC_MODE_DMA_DESC:
		blkpad = (dma->flags & DMA_FLAG_PAD_BLOCK) ? 1 : 0;
		dwpad  = (dma->flags & DMA_FLAG_PAD_DWORD) ? 1 : 0;
		chksum = (dma->flags & DMA_FLAG_EN_CHKSUM) ? 1 : 0;

		num = (sglen + MAX_BD_PER_GPD - 1) / MAX_BD_PER_GPD;
		BUG_ON(num !=1 );

		gpd = dma->gpd;
		bd = dma->bd;
		bdlen = sglen;

		gpd->hwo = 1;  /* hw will clear it */
		gpd->bdp = 1;
		gpd->chksum = 0;  /* need to clear first. */
		gpd->chksum = (chksum ? msdc_dma_calcs((u8 *)gpd, 16) : 0);

		for (j = 0; j < bdlen; j++) {
			msdc_init_bd(&bd[j], blkpad, dwpad, sg_dma_address(sg), sg_dma_len(sg));
			if( j == bdlen - 1)
				bd[j].eol = 1;
			else
				bd[j].eol = 0;
			bd[j].chksum = 0; /* checksume need to clear first */
			bd[j].chksum = (chksum ? msdc_dma_calcs((u8 *)(&bd[j]), 16) : 0);
			sg++;
		}

		dma->used_gpd += 2;
		dma->used_bd += bdlen;

		sdr_set_field(MSDC_DMA_CFG, MSDC_DMA_CFG_DECSEN, chksum);
		sdr_set_field(MSDC_DMA_CTRL, MSDC_DMA_CTRL_BRUSTSZ, dma->burstsz);
		sdr_set_field(MSDC_DMA_CTRL, MSDC_DMA_CTRL_MODE, 1);
		sdr_write32(MSDC_DMA_SA, PHYSADDR((u32)dma->gpd_addr));
		break;
	}

//	printk("DMA_CTRL = 0x%x\n", sdr_read32(MSDC_DMA_CTRL));
//	printk("DMA_CFG  = 0x%x\n", sdr_read32(MSDC_DMA_CFG));
//	printk("DMA_SA   = 0x%x\n", sdr_read32(MSDC_DMA_SA));

	return 0;
}

static void msdc_dma_setup(struct msdc_host *host, struct msdc_dma *dma,
			struct scatterlist *sg, unsigned int sglen)
{
	BUG_ON(sglen > MAX_BD_NUM);

	dma->sg = sg;
	dma->flags = DMA_FLAG_EN_CHKSUM;
	dma->sglen = sglen;
	dma->xfersz = host->xfer_size;
	dma->burstsz = MSDC_BRUST_64B;

	if (sglen == 1 && sg_dma_len(sg) <= MAX_DMA_CNT)
		dma->mode = MSDC_MODE_DMA_BASIC;
	else
		dma->mode = MSDC_MODE_DMA_DESC;

//	printk("DMA mode<%d> sglen<%d> xfersz<%d>\n", dma->mode, dma->sglen, dma->xfersz);

	msdc_dma_config(host, dma);
}

static void msdc_set_blknum(struct msdc_host *host, u32 blknum)
{
	u32 base = host->base;

	sdr_write32(SDC_BLK_NUM, blknum);
}

static int msdc_do_request(struct mmc_host*mmc, struct mmc_request*mrq)
{
	struct msdc_host *host = mmc_priv(mmc);
	struct mmc_command *cmd;
	struct mmc_data *data;
	u32 base = host->base;
	unsigned int left=0;
	int dma = 0, read = 1, dir = DMA_FROM_DEVICE, send_type=0;

#define SND_DAT 0
#define SND_CMD 1

	BUG_ON(mmc == NULL);
	BUG_ON(mrq == NULL);

	host->error = 0;
	atomic_set(&host->abort, 0);

	cmd  = mrq->cmd;
	data = mrq->cmd->data;

	if (!data) {
		send_type = SND_CMD;
		if (msdc_do_command(host, cmd, 1, CMD_TIMEOUT) != 0)
			goto done;
	} else {
		BUG_ON(data->blksz > HOST_MAX_BLKSZ);
		send_type=SND_DAT;

		data->error = 0;
		read = data->flags & MMC_DATA_READ ? 1 : 0;
		host->data = data;
		host->xfer_size = data->blocks * data->blksz;
		host->blksz = data->blksz;

		host->dma_xfer = dma = ((host->xfer_size >= 512) ? 1 : 0);

		if (read)
			if ((host->timeout_ns != data->timeout_ns) ||
					(host->timeout_clks != data->timeout_clks))
				msdc_set_timeout(host, data->timeout_ns, data->timeout_clks);

			msdc_set_blknum(host, data->blocks);

			if (dma) {
				msdc_dma_on();
				init_completion(&host->xfer_done);

				if (msdc_command_start(host, cmd, 1, CMD_TIMEOUT) != 0)
					goto done;

				dir = read ? DMA_FROM_DEVICE : DMA_TO_DEVICE;
				dma_map_sg(mmc_dev(mmc), data->sg, data->sg_len, dir);
				msdc_dma_setup(host, &host->dma, data->sg, data->sg_len);

				if (msdc_command_resp(host, cmd, 1, CMD_TIMEOUT) != 0)
					goto done;

				msdc_dma_start(host);

				spin_unlock(&host->lock);
				if (!wait_for_completion_timeout(&host->xfer_done, DAT_TIMEOUT)) {
					/*printk("XXX CMD<%d> wait xfer_done<%d> timeout!!\n", cmd->opcode, data->blocks * data->blksz);
					printk("    DMA_SA   = 0x%x\n", sdr_read32(MSDC_DMA_SA));
					printk("    DMA_CA   = 0x%x\n", sdr_read32(MSDC_DMA_CA));
					printk("    DMA_CTRL = 0x%x\n", sdr_read32(MSDC_DMA_CTRL));
					printk("    DMA_CFG  = 0x%x\n", sdr_read32(MSDC_DMA_CFG));*/
					data->error = (unsigned int)-ETIMEDOUT;

					msdc_reset();
					msdc_clr_fifo();
					msdc_clr_int();
				}
				spin_lock(&host->lock);
				msdc_dma_stop(host);
			} else {
				if (msdc_do_command(host, cmd, 1, CMD_TIMEOUT) != 0)
					goto done;

				if (read) {
					if (msdc_pio_read(host, data))
						goto done;
				} else {
					if (msdc_pio_write(host, data))
						goto done;
				}

			if (!read) {
				while (1) {
					left = msdc_txfifocnt();
					if (left == 0) {
						break;
					}
					if (msdc_pio_abort(host, data, jiffies + DAT_TIMEOUT)) {
						break;
					/* Fix me: what about if data error, when stop ? how to? */
					}
				}
			} else {
				/* Fix me: read case: need to check CRC error */	
			}

			/* For write case: SDCBUSY and Xfer_Comp will assert when DAT0 not busy. 
			   For read case : SDCBUSY and Xfer_Comp will assert when last byte read out from FIFO.
			*/

			/* try not to wait xfer_comp interrupt. 
			the next command will check SDC_BUSY. 
			SDC_BUSY means xfer_comp assert 
			*/ 

		} // PIO mode 

		/* Last: stop transfer */
		if (data->stop){ 
			if (msdc_do_command(host, data->stop, 0, CMD_TIMEOUT) != 0) {
				goto done; 
			}
		} 
	}

done:
	if (data != NULL) {
		host->data = NULL;
		host->dma_xfer = 0;    
		if (dma != 0) {
			msdc_dma_off();     
			host->dma.used_bd  = 0;
			host->dma.used_gpd = 0;
			dma_unmap_sg(mmc_dev(mmc), data->sg, data->sg_len, dir);
		}
		host->blksz = 0;  

	//	printk("CMD<%d> data<%s %s> blksz<%d> block<%d> error<%d>",cmd->opcode, (dma? "dma":"pio\n"), 
	//		(read ? "read ":"write") ,data->blksz, data->blocks, data->error);                
	}

	if (mrq->cmd->error) host->error = 0x001;
	if (mrq->data && mrq->data->error) host->error |= 0x010;     
	if (mrq->stop && mrq->stop->error) host->error |= 0x100; 

	//if (host->error) printk("host->error<%d>\n", host->error);     

	return host->error;
}

static int msdc_app_cmd(struct mmc_host *mmc, struct msdc_host *host)
{
    struct mmc_command cmd;    
    struct mmc_request mrq;
    u32 err; 

    memset(&cmd, 0, sizeof(struct mmc_command));    
    cmd.opcode = MMC_APP_CMD;    
#if 0   /* bug: we meet mmc->card is null when ACMD6 */   
    cmd.arg = mmc->card->rca << 16;
#else 
    cmd.arg = host->app_cmd_arg;     
#endif    
    cmd.flags = MMC_RSP_SPI_R1 | MMC_RSP_R1 | MMC_CMD_AC;

    memset(&mrq, 0, sizeof(struct mmc_request));
    mrq.cmd = &cmd; cmd.mrq = &mrq;
    cmd.data = NULL;        

    err = msdc_do_command(host, &cmd, 0, CMD_TIMEOUT);     
    return err;      	
}

static int msdc_tune_cmdrsp(struct msdc_host*host, struct mmc_command *cmd)
{
    int result = -1;
    u32 base = host->base;
    u32 rsmpl, cur_rsmpl, orig_rsmpl;
    u32 rrdly, cur_rrdly = 0, orig_rrdly;
    u32 skip = 1;
    
    /* ==== don't support 3.0 now ====
           1: R_SMPL[1] 
           2: PAD_CMD_RESP_RXDLY[26:22] 
	    ==========================*/

    // save the previous tune result 
    sdr_get_field(MSDC_IOCON,    MSDC_IOCON_RSPL,        orig_rsmpl);
    sdr_get_field(MSDC_PAD_TUNE, MSDC_PAD_TUNE_CMDRRDLY, orig_rrdly);

    rrdly = 0; 
    do {
        for (rsmpl = 0; rsmpl < 2; rsmpl++) {
            /* Lv1: R_SMPL[1] */    	
            cur_rsmpl = (orig_rsmpl + rsmpl) % 2;         
            if (skip == 1) {
                skip = 0; 	
                continue;	
            }
            sdr_set_field(MSDC_IOCON, MSDC_IOCON_RSPL, cur_rsmpl); 

            if (host->app_cmd) {
                result = msdc_app_cmd(host->mmc, host);	
                if (result) {
                    //printk("TUNE_CMD app_cmd<%d> failed: RESP_RXDLY<%d>,R_SMPL<%d>\n", 
                      //   host->mrq->cmd->opcode, cur_rrdly, cur_rsmpl);
                    continue;
                } 
            }          
            result = msdc_do_command(host, cmd, 0, CMD_TIMEOUT); // not tune.             
            //printk("TUNE_CMD<%d> %s PAD_CMD_RESP_RXDLY[26:22]<%d> R_SMPL[1]<%d>\n", cmd->opcode,
//                       (result == 0) ? "PASS" : "FAIL", cur_rrdly, cur_rsmpl);
                       	
            if (result == 0) {
                return 0; 	
            }                        	
            if (result != (unsigned int)(-EIO)) { 
              //  printk("TUNE_CMD<%d> Error<%d> not -EIO\n", cmd->opcode, result);	
                return result;	 
            }

            /* should be EIO */
            if (sdr_read32(SDC_CMD) & 0x1800) { /* check if has data phase */ 
                msdc_abort_data(host);
            }
        }
		
        /* Lv2: PAD_CMD_RESP_RXDLY[26:22] */              	
        cur_rrdly = (orig_rrdly + rrdly + 1) % 32;
        sdr_set_field(MSDC_PAD_TUNE, MSDC_PAD_TUNE_CMDRRDLY, cur_rrdly);		
    }while (++rrdly < 32);
	
    return result;
}

/* Support SD2.0 Only */
static int msdc_tune_bread(struct mmc_host *mmc, struct mmc_request *mrq)
{
    struct msdc_host *host = mmc_priv(mmc);
    u32 base = host->base;
    u32 ddr=0;
    u32 dcrc = 0;
    u32 rxdly, cur_rxdly0, cur_rxdly1;
    u32 dsmpl, cur_dsmpl,  orig_dsmpl;
    u32 cur_dat0,  cur_dat1,  cur_dat2,  cur_dat3;
    u32 cur_dat4,  cur_dat5,  cur_dat6,  cur_dat7;
    u32 orig_dat0, orig_dat1, orig_dat2, orig_dat3;
    u32 orig_dat4, orig_dat5, orig_dat6, orig_dat7;
    int result = -1;
    u32 skip = 1;

    sdr_get_field(MSDC_IOCON, MSDC_IOCON_DSPL, orig_dsmpl);
	
    /* Tune Method 2. */
    sdr_set_field(MSDC_IOCON, MSDC_IOCON_DDLSEL, 1);

    rxdly = 0; 
    do {
        for (dsmpl = 0; dsmpl < 2; dsmpl++) {
            cur_dsmpl = (orig_dsmpl + dsmpl) % 2;
            if (skip == 1) {
                skip = 0; 	
                continue;	
            }             
            sdr_set_field(MSDC_IOCON, MSDC_IOCON_DSPL, cur_dsmpl);

            if (host->app_cmd) {
                result = msdc_app_cmd(host->mmc, host);	
                if (result) {
                    //printk("TUNE_BREAD app_cmd<%d> failed\n", host->mrq->cmd->opcode);	
                    continue;
                } 
            } 
            result = msdc_do_request(mmc,mrq);
            
            sdr_get_field(SDC_DCRC_STS, SDC_DCRC_STS_POS|SDC_DCRC_STS_NEG, dcrc); /* RO */
            if (!ddr) dcrc &= ~SDC_DCRC_STS_NEG;
            //printk("TUNE_BREAD<%s> dcrc<0x%x> DATRDDLY0/1<0x%x><0x%x> dsmpl<0x%x>\n",
                       // (result == 0 && dcrc == 0) ? "PASS" : "FAIL", dcrc,
                       // sdr_read32(MSDC_DAT_RDDLY0), sdr_read32(MSDC_DAT_RDDLY1), cur_dsmpl);

            /* Fix me: result is 0, but dcrc is still exist */
            if (result == 0 && dcrc == 0) {
                goto done;
            } else {
                /* there is a case: command timeout, and data phase not processed */
                if (mrq->data->error != 0 && mrq->data->error != (unsigned int)(-EIO)) {
                    //printk("TUNE_READ: result<0x%x> cmd_error<%d> data_error<%d>\n", 
                         //      result, mrq->cmd->error, mrq->data->error);	
                    goto done;     	
                }
            }
        }    

        cur_rxdly0 = sdr_read32(MSDC_DAT_RDDLY0);
        cur_rxdly1 = sdr_read32(MSDC_DAT_RDDLY1);

        /* E1 ECO. YD: Reverse */
        if (sdr_read32(MSDC_ECO_VER) >= 4) {
            orig_dat0 = (cur_rxdly0 >> 24) & 0x1F;
            orig_dat1 = (cur_rxdly0 >> 16) & 0x1F;
            orig_dat2 = (cur_rxdly0 >>  8) & 0x1F;
            orig_dat3 = (cur_rxdly0 >>  0) & 0x1F;
            orig_dat4 = (cur_rxdly1 >> 24) & 0x1F;
            orig_dat5 = (cur_rxdly1 >> 16) & 0x1F;
            orig_dat6 = (cur_rxdly1 >>  8) & 0x1F;
            orig_dat7 = (cur_rxdly1 >>  0) & 0x1F;
        } else {   
            orig_dat0 = (cur_rxdly0 >>  0) & 0x1F;
            orig_dat1 = (cur_rxdly0 >>  8) & 0x1F;
            orig_dat2 = (cur_rxdly0 >> 16) & 0x1F;
            orig_dat3 = (cur_rxdly0 >> 24) & 0x1F;
            orig_dat4 = (cur_rxdly1 >>  0) & 0x1F;
            orig_dat5 = (cur_rxdly1 >>  8) & 0x1F;
            orig_dat6 = (cur_rxdly1 >> 16) & 0x1F;
            orig_dat7 = (cur_rxdly1 >> 24) & 0x1F;
        }
                
        if (ddr) {
            cur_dat0 = (dcrc & (1 << 0) || dcrc & (1 << 8))  ? ((orig_dat0 + 1) % 32) : orig_dat0;
            cur_dat1 = (dcrc & (1 << 1) || dcrc & (1 << 9))  ? ((orig_dat1 + 1) % 32) : orig_dat1;
            cur_dat2 = (dcrc & (1 << 2) || dcrc & (1 << 10)) ? ((orig_dat2 + 1) % 32) : orig_dat2;
            cur_dat3 = (dcrc & (1 << 3) || dcrc & (1 << 11)) ? ((orig_dat3 + 1) % 32) : orig_dat3;
        } else {
            cur_dat0 = (dcrc & (1 << 0)) ? ((orig_dat0 + 1) % 32) : orig_dat0;
            cur_dat1 = (dcrc & (1 << 1)) ? ((orig_dat1 + 1) % 32) : orig_dat1;
            cur_dat2 = (dcrc & (1 << 2)) ? ((orig_dat2 + 1) % 32) : orig_dat2;
            cur_dat3 = (dcrc & (1 << 3)) ? ((orig_dat3 + 1) % 32) : orig_dat3;
        }
        cur_dat4 = (dcrc & (1 << 4)) ? ((orig_dat4 + 1) % 32) : orig_dat4;
        cur_dat5 = (dcrc & (1 << 5)) ? ((orig_dat5 + 1) % 32) : orig_dat5;
        cur_dat6 = (dcrc & (1 << 6)) ? ((orig_dat6 + 1) % 32) : orig_dat6;
        cur_dat7 = (dcrc & (1 << 7)) ? ((orig_dat7 + 1) % 32) : orig_dat7;

        cur_rxdly0 = (cur_dat0 << 24) | (cur_dat1 << 16) | (cur_dat2 << 8) | (cur_dat3 << 0);
        cur_rxdly1 = (cur_dat4 << 24) | (cur_dat5 << 16) | (cur_dat6 << 8) | (cur_dat7 << 0);

        sdr_write32(MSDC_DAT_RDDLY0, cur_rxdly0);
        sdr_write32(MSDC_DAT_RDDLY1, cur_rxdly1);

    } while (++rxdly < 32);   
          
done:
    return result;
}

static int msdc_tune_bwrite(struct mmc_host *mmc,struct mmc_request *mrq)
{
	  struct msdc_host *host = mmc_priv(mmc);
    u32 base = host->base;

    u32 wrrdly, cur_wrrdly = 0, orig_wrrdly;
    u32 dsmpl,  cur_dsmpl,  orig_dsmpl;
    u32 rxdly,  cur_rxdly0;
    u32 orig_dat0, orig_dat1, orig_dat2, orig_dat3;
    u32 cur_dat0,  cur_dat1,  cur_dat2,  cur_dat3;
    int result = -1;
    u32 skip = 1;

    // MSDC_IOCON_DDR50CKD need to check. [Fix me] 
    
    sdr_get_field(MSDC_PAD_TUNE, MSDC_PAD_TUNE_DATWRDLY, orig_wrrdly);
    sdr_get_field(MSDC_IOCON,    MSDC_IOCON_DSPL,        orig_dsmpl );

    /* Tune Method 2. just DAT0 */  
    sdr_set_field(MSDC_IOCON, MSDC_IOCON_DDLSEL, 1);
    cur_rxdly0 = sdr_read32(MSDC_DAT_RDDLY0);
    
    /* E1 ECO. YD: Reverse */
    if (sdr_read32(MSDC_ECO_VER) >= 4) {
        orig_dat0 = (cur_rxdly0 >> 24) & 0x1F;
        orig_dat1 = (cur_rxdly0 >> 16) & 0x1F;
        orig_dat2 = (cur_rxdly0 >>  8) & 0x1F;
        orig_dat3 = (cur_rxdly0 >>  0) & 0x1F;
    } else {
        orig_dat0 = (cur_rxdly0 >>  0) & 0x1F;
        orig_dat1 = (cur_rxdly0 >>  8) & 0x1F;
        orig_dat2 = (cur_rxdly0 >> 16) & 0x1F;
        orig_dat3 = (cur_rxdly0 >> 24) & 0x1F;
    }

    rxdly = 0;
    do {
        wrrdly = 0;
        do {    
            for (dsmpl = 0; dsmpl < 2; dsmpl++) {
                cur_dsmpl = (orig_dsmpl + dsmpl) % 2;
                if (skip == 1) {
                    skip = 0;
                    continue; 	
                }    
                sdr_set_field(MSDC_IOCON, MSDC_IOCON_DSPL, cur_dsmpl);
                
                if (host->app_cmd) {
                    result = msdc_app_cmd(host->mmc, host);	
                    if (result) {
                        //printk("TUNE_BWRITE app_cmd<%d> failed\n", host->mrq->cmd->opcode);	
                        continue;
                    } 
                }             
                result = msdc_do_request(mmc,mrq);
            
                //printk("TUNE_BWRITE<%s> DSPL<%d> DATWRDLY<%d> MSDC_DAT_RDDLY0<0x%x>\n", 
                  //        result == 0 ? "PASS" : "FAIL", 
                    //      cur_dsmpl, cur_wrrdly, cur_rxdly0);
            
                if (result == 0) {
                    goto done;
                }
                else {
                    /* there is a case: command timeout, and data phase not processed */
                    if (mrq->data->error != (unsigned int)(-EIO)) {
                        //printk("TUNE_READ: result<0x%x> cmd_error<%d> data_error<%d>\n", 
                        //  &&         result, mrq->cmd->error, mrq->data->error);	
                        goto done;     	
                    }
                }       
            }
            cur_wrrdly = (orig_wrrdly + wrrdly + 1) % 32;
            sdr_set_field(MSDC_PAD_TUNE, MSDC_PAD_TUNE_DATWRDLY, cur_wrrdly);             
        } while (++wrrdly < 32); 
        
        cur_dat0 = (orig_dat0 + rxdly) % 32; /* only adjust bit-1 for crc */
        cur_dat1 = orig_dat1;
        cur_dat2 = orig_dat2;
        cur_dat3 = orig_dat3;                    
    
        cur_rxdly0 = (cur_dat0 << 24) | (cur_dat1 << 16) | (cur_dat2 << 8) | (cur_dat3 << 0);       
        sdr_write32(MSDC_DAT_RDDLY0, cur_rxdly0);    
    } while (++rxdly < 32); 

done:
    return result;
}

static int msdc_get_card_status(struct mmc_host *mmc, struct msdc_host *host, u32 *status)
{
	struct mmc_command cmd;
	struct mmc_request mrq;
	u32 err;

	memset(&cmd, 0, sizeof(struct mmc_command));
	cmd.opcode = MMC_SEND_STATUS;
	if (mmc->card) {
		cmd.arg = mmc->card->rca << 16;
	} else {
		//printk("cmd13 mmc card is null\n");
		cmd.arg = host->app_cmd_arg;
	}
	cmd.flags = MMC_RSP_SPI_R2 | MMC_RSP_R1 | MMC_CMD_AC;

	memset(&mrq, 0, sizeof(struct mmc_request));
	mrq.cmd = &cmd; cmd.mrq = &mrq;
	cmd.data = NULL;

	err = msdc_do_command(host, &cmd, 1, CMD_TIMEOUT);

	if (status)
		*status = cmd.resp[0];

	return err;
}

static int msdc_check_busy(struct mmc_host *mmc, struct msdc_host *host)
{
	u32 err = 0;
	u32 status = 0;

	do {
		err = msdc_get_card_status(mmc, host, &status);
		if (err)
			return err;
		/* need cmd12? */
		//printk("cmd<13> resp<0x%x>\n", status);
	} while (R1_CURRENT_STATE(status) == 7);

	return err;
}

static int msdc_tune_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct msdc_host *host = mmc_priv(mmc);
	struct mmc_command *cmd;
	struct mmc_data *data;
	int ret=0, read;

	cmd  = mrq->cmd;
	data = mrq->cmd->data;

	read = data->flags & MMC_DATA_READ ? 1 : 0;

	if (read) {
		if (data->error == (unsigned int)(-EIO))
			ret = msdc_tune_bread(mmc,mrq);
	} else {
		ret = msdc_check_busy(mmc, host);
		if (ret){
			//printk("XXX cmd13 wait program done failed\n");
			return ret;
		}
		/* CRC and TO */
		/* Fix me: don't care card status? */
		ret = msdc_tune_bwrite(mmc,mrq);
	}

	return ret;
}

static void msdc_ops_request(struct mmc_host *mmc,struct mmc_request *mrq)
{
	struct msdc_host *host = mmc_priv(mmc);

	if (host->mrq) {
		//printk("XXX host->mrq<0x%.8x>\n", (int)host->mrq);
		BUG();
	}
	if (!is_card_present(host) || host->power_mode == MMC_POWER_OFF) {
		//printk("cmd<%d> card<%d> power<%d>\n", mrq->cmd->opcode, is_card_present(host), host->power_mode);
		mrq->cmd->error = (unsigned int)-ENOMEDIUM;
		mrq->done(mrq);
		return;
	}
	spin_lock(&host->lock);

	host->mrq = mrq;

	if (msdc_do_request(mmc,mrq))
		if(host->hw->flags & MSDC_REMOVABLE && mrq->data && mrq->data->error)
			msdc_tune_request(mmc,mrq);

	if (mrq->cmd->opcode == MMC_APP_CMD) {
		host->app_cmd = 1;
		host->app_cmd_arg = mrq->cmd->arg;  /* save the RCA */
	} else {
		host->app_cmd = 0;
	}

	host->mrq = NULL;

	spin_unlock(&host->lock);

	mmc_request_done(mmc, mrq);
}

/* called by ops.set_ios */
static void msdc_set_buswidth(struct msdc_host *host, u32 width)
{
    u32 base = host->base;
    u32 val = sdr_read32(SDC_CFG);
    
    val &= ~SDC_CFG_BUSWIDTH;
    
    switch (width) {
    default:
    case MMC_BUS_WIDTH_1:
        width = 1;
        val |= (MSDC_BUS_1BITS << 16);
        break;
    case MMC_BUS_WIDTH_4:
        val |= (MSDC_BUS_4BITS << 16);
        break;
    case MMC_BUS_WIDTH_8:
        val |= (MSDC_BUS_8BITS << 16);
        break;
    }
    
    sdr_write32(SDC_CFG, val);

    //printk("Bus Width = %d\n", width);
}

/* ops.set_ios */
static void msdc_ops_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct msdc_host *host = mmc_priv(mmc);
	struct msdc_hw *hw=host->hw;
	u32 base = host->base;
	u32 ddr = 0;

#ifdef MT6575_SD_DEBUG
	static char *vdd[] = {
	"1.50v", "1.55v", "1.60v", "1.65v", "1.70v", "1.80v", "1.90v",
	"2.00v", "2.10v", "2.20v", "2.30v", "2.40v", "2.50v", "2.60v",
	"2.70v", "2.80v", "2.90v", "3.00v", "3.10v", "3.20v", "3.30v",
	"3.40v", "3.50v", "3.60v"		
	};
	static char *power_mode[] = {
	"OFF", "UP", "ON"
	};
	static char *bus_mode[] = {
	"UNKNOWN", "OPENDRAIN", "PUSHPULL"
	};
	static char *timing[] = {
	"LEGACY", "MMC_HS", "SD_HS"
	};

	/*printk("SET_IOS: CLK(%dkHz), BUS(%s), BW(%u), PWR(%s), VDD(%s), TIMING(%s)\n",
	ios->clock / 1000, bus_mode[ios->bus_mode],
	(ios->bus_width == MMC_BUS_WIDTH_4) ? 4 : 1,
	power_mode[ios->power_mode], vdd[ios->vdd], timing[ios->timing]);*/
#endif

	msdc_set_buswidth(host, ios->bus_width);

	/* Power control ??? */
	switch (ios->power_mode) {
	case MMC_POWER_OFF:
	case MMC_POWER_UP:
		// msdc_set_power_mode(host, ios->power_mode); /* --- by chhung */
		break;
	case MMC_POWER_ON:
		host->power_mode = MMC_POWER_ON;
		break;
	default:
		break;
	}

	/* Clock control */
	if (host->mclk != ios->clock) {
		if(ios->clock > 25000000) {
			//printk("SD data latch edge<%d>\n", hw->data_edge);
			sdr_set_field(MSDC_IOCON, MSDC_IOCON_RSPL, hw->cmd_edge);
			sdr_set_field(MSDC_IOCON, MSDC_IOCON_DSPL, hw->data_edge);
		} else {
			sdr_write32(MSDC_IOCON,      0x00000000);
			sdr_write32(MSDC_DAT_RDDLY0, 0x10101010);	// for MT7620 E2 and afterward
			sdr_write32(MSDC_DAT_RDDLY1, 0x00000000);
			sdr_write32(MSDC_PAD_TUNE,   0x84101010);	// for MT7620 E2 and afterward
		}
		msdc_set_mclk(host, ddr, ios->clock);
	}
}

/* ops.get_ro */
static int msdc_ops_get_ro(struct mmc_host *mmc)
{
    struct msdc_host *host = mmc_priv(mmc);
    u32 base = host->base;
    unsigned long flags;
    int ro = 0;

    if (host->hw->flags & MSDC_WP_PIN_EN) { /* set for card */
        spin_lock_irqsave(&host->lock, flags);
        ro = (sdr_read32(MSDC_PS) >> 31);
        spin_unlock_irqrestore(&host->lock, flags);
    }
    return ro;
}

/* ops.get_cd */
static int msdc_ops_get_cd(struct mmc_host *mmc)
{
    struct msdc_host *host = mmc_priv(mmc);
    u32 base = host->base;    
    unsigned long flags;
    int present = 1;

    /* for sdio, MSDC_REMOVABLE not set, always return 1 */
    if (!(host->hw->flags & MSDC_REMOVABLE)) {
        /* For sdio, read H/W always get<1>, but may timeout some times */       	    	
#if 1
        host->card_inserted = 1;       
        return 1;
#else
        host->card_inserted = (host->pm_state.event == PM_EVENT_USER_RESUME) ? 1 : 0; 
        printk("sdio ops_get_cd<%d>\n", host->card_inserted);
        return host->card_inserted; 
#endif
    }

    /* MSDC_CD_PIN_EN set for card */
    if (host->hw->flags & MSDC_CD_PIN_EN) {
        spin_lock_irqsave(&host->lock, flags);
#if 0        
        present = host->card_inserted;  /* why not read from H/W: Fix me*/
#else
        present = (sdr_read32(MSDC_PS) & MSDC_PS_CDSTS) ? 0 : 1; 
        host->card_inserted = present;  
#endif        
        spin_unlock_irqrestore(&host->lock, flags);
    } else {
        present = 0; /* TODO? Check DAT3 pins for card detection */
    }

    //printk("ops_get_cd return<%d>\n", present);
    return present;
}

/* ops.enable_sdio_irq */
static void msdc_ops_enable_sdio_irq(struct mmc_host *mmc, int enable)
{
    struct msdc_host *host = mmc_priv(mmc);
    struct msdc_hw *hw = host->hw;
    u32 base = host->base;
    u32 tmp;

    if (hw->flags & MSDC_EXT_SDIO_IRQ) { /* yes for sdio */
        if (enable) {
            hw->enable_sdio_eirq();  /* combo_sdio_enable_eirq */
        } else {
            hw->disable_sdio_eirq(); /* combo_sdio_disable_eirq */
        }
    } else { 
    	  //printk("XXX \n");  /* so never enter here */
        tmp = sdr_read32(SDC_CFG);
        /* FIXME. Need to interrupt gap detection */
        if (enable) {
            tmp |= (SDC_CFG_SDIOIDE | SDC_CFG_SDIOINTWKUP);           
        } else {
            tmp &= ~(SDC_CFG_SDIOIDE | SDC_CFG_SDIOINTWKUP);
        }
        sdr_write32(SDC_CFG, tmp);      
    }
}

static struct mmc_host_ops mt_msdc_ops = {
    .request         = msdc_ops_request,
    .set_ios         = msdc_ops_set_ios,
    .get_ro          = msdc_ops_get_ro,
    .get_cd          = msdc_ops_get_cd,
    .enable_sdio_irq = msdc_ops_enable_sdio_irq,
};

/*--------------------------------------------------------------------------*/
/* interrupt handler                                                    */
/*--------------------------------------------------------------------------*/
static irqreturn_t msdc_irq(int irq, void *dev_id)
{
    struct msdc_host  *host = (struct msdc_host *)dev_id;
    struct mmc_data   *data = host->data;
    struct mmc_command *cmd = host->cmd;
    u32 base = host->base;
        
    u32 cmdsts = MSDC_INT_RSPCRCERR  | MSDC_INT_CMDTMO  | MSDC_INT_CMDRDY  |
                 MSDC_INT_ACMDCRCERR | MSDC_INT_ACMDTMO | MSDC_INT_ACMDRDY |
                 MSDC_INT_ACMD19_DONE;                 
    u32 datsts = MSDC_INT_DATCRCERR  |MSDC_INT_DATTMO;

    u32 intsts = sdr_read32(MSDC_INT);
    u32 inten  = sdr_read32(MSDC_INTEN); inten &= intsts; 

    sdr_write32(MSDC_INT, intsts);  /* clear interrupts */
    /* MSG will cause fatal error */
        
    /* card change interrupt */
    if (intsts & MSDC_INT_CDSC){
        //printk("MSDC_INT_CDSC irq<0x%.8x>\n", intsts); 
        tasklet_hi_schedule(&host->card_tasklet);
        /* tuning when plug card ? */
    }
    
    /* sdio interrupt */
    if (intsts & MSDC_INT_SDIOIRQ){
        //printk("XXX MSDC_INT_SDIOIRQ\n");  /* seems not sdio irq */
        //mmc_signal_sdio_irq(host->mmc);
    }

    /* transfer complete interrupt */
    if (data != NULL) {
        if (inten & MSDC_INT_XFER_COMPL) {       	
            data->bytes_xfered = host->dma.xfersz;
            complete(&host->xfer_done);           
        } 
        
        if (intsts & datsts) {         
            /* do basic reset, or stop command will sdc_busy */
            msdc_reset();
            msdc_clr_fifo();        
            msdc_clr_int();             
            atomic_set(&host->abort, 1);  /* For PIO mode exit */
            
            if (intsts & MSDC_INT_DATTMO){
               	//printk("XXX CMD<%d> MSDC_INT_DATTMO\n", host->mrq->cmd->opcode);
               	data->error = (unsigned int)-ETIMEDOUT;
            }
            else if (intsts & MSDC_INT_DATCRCERR){
                //printk("XXX CMD<%d> MSDC_INT_DATCRCERR, SDC_DCRC_STS<0x%x>\n", host->mrq->cmd->opcode, sdr_read32(SDC_DCRC_STS));
                data->error = (unsigned int)-EIO;
            }
                                    
            //if(sdr_read32(MSDC_INTEN) & MSDC_INT_XFER_COMPL) {  
            if (host->dma_xfer) {
                complete(&host->xfer_done); /* Read CRC come fast, XFER_COMPL not enabled */
            } /* PIO mode can't do complete, because not init */
        }
    }

    /* command interrupts */
    if ((cmd != NULL) && (intsts & cmdsts)) {
        if ((intsts & MSDC_INT_CMDRDY) || (intsts & MSDC_INT_ACMDRDY) || 
            (intsts & MSDC_INT_ACMD19_DONE)) {
            u32 *rsp = &cmd->resp[0];
            
            switch (host->cmd_rsp) {
            case RESP_NONE:
                break;
            case RESP_R2:
                *rsp++ = sdr_read32(SDC_RESP3); *rsp++ = sdr_read32(SDC_RESP2);
                *rsp++ = sdr_read32(SDC_RESP1); *rsp++ = sdr_read32(SDC_RESP0);
                break;
            default: /* Response types 1, 3, 4, 5, 6, 7(1b) */
                if ((intsts & MSDC_INT_ACMDRDY) || (intsts & MSDC_INT_ACMD19_DONE)) {
                    *rsp = sdr_read32(SDC_ACMD_RESP);
                } else {
                    *rsp = sdr_read32(SDC_RESP0);    
                }
                break;
            }
        } else if ((intsts & MSDC_INT_RSPCRCERR) || (intsts & MSDC_INT_ACMDCRCERR)) {
            if(intsts & MSDC_INT_ACMDCRCERR){
                //printk("XXX CMD<%d> MSDC_INT_ACMDCRCERR\n",cmd->opcode);
            } 
            else {
                //printk("XXX CMD<%d> MSDC_INT_RSPCRCERR\n",cmd->opcode);
            }
            cmd->error = (unsigned int)-EIO;
        } else if ((intsts & MSDC_INT_CMDTMO) || (intsts & MSDC_INT_ACMDTMO)) {
            if(intsts & MSDC_INT_ACMDTMO){
                //printk("XXX CMD<%d> MSDC_INT_ACMDTMO\n",cmd->opcode);
            }
            else {
                //printk("XXX CMD<%d> MSDC_INT_CMDTMO\n",cmd->opcode);
            }
            cmd->error = (unsigned int)-ETIMEDOUT;
            msdc_reset();
            msdc_clr_fifo();        
            msdc_clr_int();            
        }
        complete(&host->cmd_done);
    }

    /* mmc irq interrupts */
    if (intsts & MSDC_INT_MMCIRQ) {
        //printk(KERN_INFO "msdc[%d] MMCIRQ: SDC_CSTS=0x%.8x\r\n", host->id, sdr_read32(SDC_CSTS));    
    }
    
#ifdef MT6575_SD_DEBUG
    {
        msdc_int_reg *int_reg = (msdc_int_reg*)&intsts;
        /*printk("IRQ_EVT(0x%x): MMCIRQ(%d) CDSC(%d), ACRDY(%d), ACTMO(%d), ACCRE(%d) AC19DN(%d)\n", 
            intsts,
            int_reg->mmcirq,
            int_reg->cdsc,
            int_reg->atocmdrdy,
            int_reg->atocmdtmo,
            int_reg->atocmdcrc,
            int_reg->atocmd19done);
        printk("IRQ_EVT(0x%x): SDIO(%d) CMDRDY(%d), CMDTMO(%d), RSPCRC(%d), CSTA(%d)\n", 
            intsts,
            int_reg->sdioirq,
            int_reg->cmdrdy,
            int_reg->cmdtmo,
            int_reg->rspcrc,
            int_reg->csta);
        printk("IRQ_EVT(0x%x): XFCMP(%d) DXDONE(%d), DATTMO(%d), DATCRC(%d), DMAEMP(%d)\n", 
            intsts,
            int_reg->xfercomp,
            int_reg->dxferdone,
            int_reg->dattmo,
            int_reg->datcrc,
            int_reg->dmaqempty);*/

    }
#endif
    
    return IRQ_HANDLED;
}

/*--------------------------------------------------------------------------*/
/* platform_driver members                                                      */
/*--------------------------------------------------------------------------*/
/* called by msdc_drv_probe/remove */
static void msdc_enable_cd_irq(struct msdc_host *host, int enable)
{
	struct msdc_hw *hw = host->hw;
	u32 base = host->base;

	/* for sdio, not set */
	if ((hw->flags & MSDC_CD_PIN_EN) == 0) {
		/* Pull down card detection pin since it is not avaiable */
		/*
		   if (hw->config_gpio_pin) 
		   hw->config_gpio_pin(MSDC_CD_PIN, GPIO_PULL_DOWN);
		   */
		sdr_clr_bits(MSDC_PS, MSDC_PS_CDEN);
		sdr_clr_bits(MSDC_INTEN, MSDC_INTEN_CDSC);
		sdr_clr_bits(SDC_CFG, SDC_CFG_INSWKUP);
		return;
	}

	//printk("CD IRQ Eanable(%d)\n", enable);

	if (enable) {
	    if (hw->enable_cd_eirq) { /* not set, never enter */
		    hw->enable_cd_eirq();
	    } else {
		    /* card detection circuit relies on the core power so that the core power 
		     * shouldn't be turned off. Here adds a reference count to keep 
		     * the core power alive.
		     */
		    //msdc_vcore_on(host); //did in msdc_init_hw()

		    if (hw->config_gpio_pin) /* NULL */
			    hw->config_gpio_pin(MSDC_CD_PIN, GPIO_PULL_UP);

		    sdr_set_field(MSDC_PS, MSDC_PS_CDDEBOUNCE, DEFAULT_DEBOUNCE);
		    sdr_set_bits(MSDC_PS, MSDC_PS_CDEN);
		    sdr_set_bits(MSDC_INTEN, MSDC_INTEN_CDSC);
		    sdr_set_bits(SDC_CFG, SDC_CFG_INSWKUP);  /* not in document! Fix me */
	    }
    } else {
	    if (hw->disable_cd_eirq) {
		    hw->disable_cd_eirq();
	    } else {
		    if (hw->config_gpio_pin) /* NULL */
			    hw->config_gpio_pin(MSDC_CD_PIN, GPIO_PULL_DOWN);

		    sdr_clr_bits(SDC_CFG, SDC_CFG_INSWKUP);
		    sdr_clr_bits(MSDC_PS, MSDC_PS_CDEN);
		    sdr_clr_bits(MSDC_INTEN, MSDC_INTEN_CDSC);

		    /* Here decreases a reference count to core power since card 
		     * detection circuit is shutdown.
		     */
		    //msdc_vcore_off(host);
	    }
    }
}

/* called by msdc_drv_probe */
static void msdc_init_hw(struct msdc_host *host)
{
    u32 base = host->base;
    struct msdc_hw *hw = host->hw;

#ifdef MT6575_SD_DEBUG	
    msdc_reg[host->id] = (struct msdc_regs *)host->base;
#endif

    /* Power on */
#if 0 /* --- chhung */
    msdc_vcore_on(host);
    msdc_pin_reset(host, MSDC_PIN_PULL_UP);
    msdc_select_clksrc(host, hw->clk_src);
    enable_clock(PERI_MSDC0_PDN + host->id, "SD");
    msdc_vdd_on(host);
#endif /* end of --- */
    /* Configure to MMC/SD mode */
    sdr_set_field(MSDC_CFG, MSDC_CFG_MODE, MSDC_SDMMC); 
       
    /* Reset */
    msdc_reset();
    msdc_clr_fifo();

    /* Disable card detection */
    sdr_clr_bits(MSDC_PS, MSDC_PS_CDEN);

    /* Disable and clear all interrupts */
    sdr_clr_bits(MSDC_INTEN, sdr_read32(MSDC_INTEN));
    sdr_write32(MSDC_INT, sdr_read32(MSDC_INT));
    
#if 1
	/* reset tuning parameter */
    sdr_write32(MSDC_PAD_CTL0,   0x00090000);
    sdr_write32(MSDC_PAD_CTL1,   0x000A0000);
    sdr_write32(MSDC_PAD_CTL2,   0x000A0000);
    // sdr_write32(MSDC_PAD_TUNE,   0x00000000);
    sdr_write32(MSDC_PAD_TUNE,   0x84101010);		// for MT7620 E2 and afterward
    // sdr_write32(MSDC_DAT_RDDLY0, 0x00000000);
    sdr_write32(MSDC_DAT_RDDLY0, 0x10101010);		// for MT7620 E2 and afterward
    sdr_write32(MSDC_DAT_RDDLY1, 0x00000000);
    sdr_write32(MSDC_IOCON,      0x00000000);
#if 0 // use MT7620 default value: 0x403c004f
    sdr_write32(MSDC_PATCH_BIT0, 0x003C000F); /* bit0 modified: Rx Data Clock Source: 1 -> 2.0*/
#endif

    if (sdr_read32(MSDC_ECO_VER) >= 4) { 
        if (host->id == 1) {	
            sdr_set_field(MSDC_PATCH_BIT1, MSDC_PATCH_BIT1_WRDAT_CRCS, 1); 
            sdr_set_field(MSDC_PATCH_BIT1, MSDC_PATCH_BIT1_CMD_RSP,    1);
            
            /* internal clock: latch read data */  
            sdr_set_bits(MSDC_PATCH_BIT0, MSDC_PATCH_BIT_CKGEN_CK);  
        }       	
    }   
#endif    

    /* for safety, should clear SDC_CFG.SDIO_INT_DET_EN & set SDC_CFG.SDIO in 
       pre-loader,uboot,kernel drivers. and SDC_CFG.SDIO_INT_DET_EN will be only
       set when kernel driver wants to use SDIO bus interrupt */
    /* Configure to enable SDIO mode. it's must otherwise sdio cmd5 failed */
    sdr_set_bits(SDC_CFG, SDC_CFG_SDIO);

    /* disable detect SDIO device interupt function */
    sdr_clr_bits(SDC_CFG, SDC_CFG_SDIOIDE);

    /* eneable SMT for glitch filter */
    sdr_set_bits(MSDC_PAD_CTL0, MSDC_PAD_CTL0_CLKSMT);
    sdr_set_bits(MSDC_PAD_CTL1, MSDC_PAD_CTL1_CMDSMT);
    sdr_set_bits(MSDC_PAD_CTL2, MSDC_PAD_CTL2_DATSMT);

#if 1
    /* set clk, cmd, dat pad driving */
    sdr_set_field(MSDC_PAD_CTL0, MSDC_PAD_CTL0_CLKDRVN, hw->clk_drv);
    sdr_set_field(MSDC_PAD_CTL0, MSDC_PAD_CTL0_CLKDRVP, hw->clk_drv);
    sdr_set_field(MSDC_PAD_CTL1, MSDC_PAD_CTL1_CMDDRVN, hw->cmd_drv);
    sdr_set_field(MSDC_PAD_CTL1, MSDC_PAD_CTL1_CMDDRVP, hw->cmd_drv);
    sdr_set_field(MSDC_PAD_CTL2, MSDC_PAD_CTL2_DATDRVN, hw->dat_drv);
    sdr_set_field(MSDC_PAD_CTL2, MSDC_PAD_CTL2_DATDRVP, hw->dat_drv);
#else 
    sdr_set_field(MSDC_PAD_CTL0, MSDC_PAD_CTL0_CLKDRVN, 0);
    sdr_set_field(MSDC_PAD_CTL0, MSDC_PAD_CTL0_CLKDRVP, 0);
    sdr_set_field(MSDC_PAD_CTL1, MSDC_PAD_CTL1_CMDDRVN, 0);
    sdr_set_field(MSDC_PAD_CTL1, MSDC_PAD_CTL1_CMDDRVP, 0);
    sdr_set_field(MSDC_PAD_CTL2, MSDC_PAD_CTL2_DATDRVN, 0);
    sdr_set_field(MSDC_PAD_CTL2, MSDC_PAD_CTL2_DATDRVP, 0);
#endif

    /* set sampling edge */

    /* write crc timeout detection */
    sdr_set_field(MSDC_PATCH_BIT0, 1 << 30, 1);

    /* Configure to default data timeout */
    sdr_set_field(SDC_CFG, SDC_CFG_DTOC, DEFAULT_DTOC);

    msdc_set_buswidth(host, MMC_BUS_WIDTH_1);

    //printk("init hardware done!\n");
}

/* called by msdc_drv_remove */
static void msdc_deinit_hw(struct msdc_host *host)
{
    u32 base = host->base;

    /* Disable and clear all interrupts */
    sdr_clr_bits(MSDC_INTEN, sdr_read32(MSDC_INTEN));
    sdr_write32(MSDC_INT, sdr_read32(MSDC_INT));

    /* Disable card detection */
    msdc_enable_cd_irq(host, 0);
    // msdc_set_power_mode(host, MMC_POWER_OFF);   /* make sure power down */ /* --- by chhung */
}

/* init gpd and bd list in msdc_drv_probe */
static void msdc_init_gpd_bd(struct msdc_host *host, struct msdc_dma *dma)
{
    gpd_t *gpd = dma->gpd; 
    bd_t  *bd  = dma->bd; 	
    bd_t  *ptr, *prev;
    
    /* we just support one gpd */     
    int bdlen = MAX_BD_PER_GPD;   	

    /* init the 2 gpd */
    memset(gpd, 0, sizeof(gpd_t) * 2);
    //gpd->next = (void *)virt_to_phys(gpd + 1); /* pointer to a null gpd, bug! kmalloc <-> virt_to_phys */  
    //gpd->next = (dma->gpd_addr + 1);    /* bug */
    gpd->next = (void *)((u32)dma->gpd_addr + sizeof(gpd_t));    

    //gpd->intr = 0;
    gpd->bdp  = 1;   /* hwo, cs, bd pointer */      
    //gpd->ptr  = (void*)virt_to_phys(bd); 
    gpd->ptr = (void *)dma->bd_addr; /* physical address */
    
    memset(bd, 0, sizeof(bd_t) * bdlen);
    ptr = bd + bdlen - 1;
    //ptr->eol  = 1;  /* 0 or 1 [Fix me]*/
    //ptr->next = 0;    
    
    while (ptr != bd) {
        prev = ptr - 1;
        prev->next = (void *)(dma->bd_addr + sizeof(bd_t) *(ptr - bd));
        ptr = prev;
    }
}

static int msdc_drv_probe(struct platform_device *pdev)
{
	struct resource *res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	__iomem void *base;
    struct mmc_host *mmc;
    struct resource *mem;
    struct msdc_host *host;
    struct msdc_hw *hw;
    int ret, irq;
     pdev->dev.platform_data = &msdc0_hw;
 
    /* Allocate MMC host for this device */
    mmc = mmc_alloc_host(sizeof(struct msdc_host), &pdev->dev);
    if (!mmc) return -ENOMEM;

    hw   = (struct msdc_hw*)pdev->dev.platform_data;
    mem  = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    irq  = platform_get_irq(pdev, 0);

    //BUG_ON((!hw) || (!mem) || (irq < 0)); /* --- by chhung */
    
	base = devm_request_and_ioremap(&pdev->dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

/*    mem = request_mem_region(mem->start - 0xa0000000, (mem->end - mem->start + 1) - 0xa0000000, dev_name(&pdev->dev));
    if (mem == NULL) {
        mmc_free_host(mmc);
        return -EBUSY;
    }
*/
    /* Set host parameters to mmc */
    mmc->ops        = &mt_msdc_ops;
    mmc->f_min      = HOST_MIN_MCLK;
    mmc->f_max      = HOST_MAX_MCLK;
    mmc->ocr_avail  = MSDC_OCR_AVAIL;
    
    /* For sd card: MSDC_SYS_SUSPEND | MSDC_WP_PIN_EN | MSDC_CD_PIN_EN | MSDC_REMOVABLE | MSDC_HIGHSPEED, 
       For sdio   : MSDC_EXT_SDIO_IRQ | MSDC_HIGHSPEED */
    if (hw->flags & MSDC_HIGHSPEED) {
        mmc->caps   = MMC_CAP_MMC_HIGHSPEED | MMC_CAP_SD_HIGHSPEED;
    }
    if (hw->data_pins == 4) { /* current data_pins are all 4*/
        mmc->caps  |= MMC_CAP_4_BIT_DATA;
    } else if (hw->data_pins == 8) {
        mmc->caps  |= MMC_CAP_8_BIT_DATA;
    }
    if ((hw->flags & MSDC_SDIO_IRQ) || (hw->flags & MSDC_EXT_SDIO_IRQ))
        mmc->caps |= MMC_CAP_SDIO_IRQ;  /* yes for sdio */

    /* MMC core transfer sizes tunable parameters */
  //  mmc->max_hw_segs   = MAX_HW_SGMTS;
//    mmc->max_phys_segs = MAX_PHY_SGMTS;
    mmc->max_seg_size  = MAX_SGMT_SZ;
    mmc->max_blk_size  = HOST_MAX_BLKSZ;
    mmc->max_req_size  = MAX_REQ_SZ; 
    mmc->max_blk_count = mmc->max_req_size;

    host = mmc_priv(mmc);
    host->hw        = hw;
    host->mmc       = mmc;
    host->id        = pdev->id;
    host->error     = 0;
    host->irq       = irq;    
    host->base      = (unsigned long) base;
    host->mclk      = 0;                   /* mclk: the request clock of mmc sub-system */
    host->hclk      = hclks[hw->clk_src];  /* hclk: clock of clock source to msdc controller */
    host->sclk      = 0;                   /* sclk: the really clock after divition */
    host->pm_state  = PMSG_RESUME;
    host->suspend   = 0;
    host->core_clkon = 0;
    host->card_clkon = 0;    
    host->core_power = 0;
    host->power_mode = MMC_POWER_OFF;
//    host->card_inserted = hw->flags & MSDC_REMOVABLE ? 0 : 1;
    host->timeout_ns = 0;
    host->timeout_clks = DEFAULT_DTOC * 65536;
  
    host->mrq = NULL; 
    //init_MUTEX(&host->sem); /* we don't need to support multiple threads access */
   
    host->dma.used_gpd = 0;
    host->dma.used_bd = 0;

    /* using dma_alloc_coherent*/  /* todo: using 1, for all 4 slots */
    host->dma.gpd = dma_alloc_coherent(NULL, MAX_GPD_NUM * sizeof(gpd_t), &host->dma.gpd_addr, GFP_KERNEL); 
    host->dma.bd =  dma_alloc_coherent(NULL, MAX_BD_NUM  * sizeof(bd_t),  &host->dma.bd_addr,  GFP_KERNEL); 
    BUG_ON((!host->dma.gpd) || (!host->dma.bd));    
    msdc_init_gpd_bd(host, &host->dma);
    /*for emmc*/
    msdc_6575_host[pdev->id] = host;
    
    tasklet_init(&host->card_tasklet, msdc_tasklet_card, (ulong)host);
    spin_lock_init(&host->lock);
    msdc_init_hw(host);

    ret = request_irq((unsigned int)irq, msdc_irq, IRQF_TRIGGER_LOW, dev_name(&pdev->dev), host);
    if (ret) goto release;
    // mt65xx_irq_unmask(irq); /* --- by chhung */
    
    if (hw->flags & MSDC_CD_PIN_EN) { /* not set for sdio */
        if (hw->request_cd_eirq) { /* not set for MT6575 */
            hw->request_cd_eirq(msdc_eirq_cd, (void*)host); /* msdc_eirq_cd will not be used! */
        }
    }

    if (hw->request_sdio_eirq) /* set to combo_sdio_request_eirq() for WIFI */
        hw->request_sdio_eirq(msdc_eirq_sdio, (void*)host); /* msdc_eirq_sdio() will be called when EIRQ */

    if (hw->register_pm) {/* yes for sdio */
        if(hw->flags & MSDC_SYS_SUSPEND) { /* will not set for WIFI */
            //printk("MSDC_SYS_SUSPEND and register_pm both set\n");
        }
        //mmc->pm_flags |= MMC_PM_IGNORE_PM_NOTIFY; /* pm not controlled by system but by client. */ /* --- by chhung */
    }
    
    platform_set_drvdata(pdev, mmc);

    ret = mmc_add_host(mmc);
    if (ret) goto free_irq;

    /* Config card detection pin and enable interrupts */
    if (hw->flags & MSDC_CD_PIN_EN) {  /* set for card */
        msdc_enable_cd_irq(host, 1);
    } else {
        msdc_enable_cd_irq(host, 0);
    }  

    return 0;

free_irq:
    free_irq(irq, host);
release:
    platform_set_drvdata(pdev, NULL);
    msdc_deinit_hw(host);

    tasklet_kill(&host->card_tasklet);

/*    if (mem)
        release_mem_region(mem->start, mem->end - mem->start + 1);
*/
    mmc_free_host(mmc);

    return ret;
}

/* 4 device share one driver, using "drvdata" to show difference */
static int msdc_drv_remove(struct platform_device *pdev)
{
    struct mmc_host *mmc;
    struct msdc_host *host;
    struct resource *mem;


    mmc  = platform_get_drvdata(pdev);
    BUG_ON(!mmc);
    
    host = mmc_priv(mmc);   
    BUG_ON(!host);

    //printk("removed !!!\n");

    platform_set_drvdata(pdev, NULL);
    mmc_remove_host(host->mmc);
    msdc_deinit_hw(host);

    tasklet_kill(&host->card_tasklet);
    free_irq(host->irq, host);

    dma_free_coherent(NULL, MAX_GPD_NUM * sizeof(gpd_t), host->dma.gpd, host->dma.gpd_addr);
    dma_free_coherent(NULL, MAX_BD_NUM  * sizeof(bd_t),  host->dma.bd,  host->dma.bd_addr);

    mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);

    if (mem)
        release_mem_region(mem->start, mem->end - mem->start + 1);

    mmc_free_host(host->mmc);

    return 0;
}

static const struct of_device_id mt7620a_sdhci_match[] = {
	{ .compatible = "ralink,mt7620a-sdhci" },
	{},
};
MODULE_DEVICE_TABLE(of, rt288x_wdt_match);

/* Fix me: Power Flow */
static struct platform_driver mt_msdc_driver = {
    .probe   = msdc_drv_probe,
    .remove  = msdc_drv_remove,
    .driver  = {
        .name  = DRV_NAME,
        .owner = THIS_MODULE,
        .of_match_table = mt7620a_sdhci_match,

    },
};

static int __init mt_msdc_init(void)
{
    int ret;
/* +++ chhung */
    unsigned int reg;

    mtk_sd_device.dev.platform_data = &msdc0_hw;
    printk("MTK MSDC device init.\n");
    reg = sdr_read32((__iomem void *) 0xb0000060) & ~(0x3<<18);
    reg |= 0x1 << 18;
    sdr_write32((__iomem void *) 0xb0000060, reg);
/* end of +++ */
    ret = platform_driver_register(&mt_msdc_driver);
    if (ret) {
        printk(KERN_ERR DRV_NAME ": Can't register driver");
        return ret;
    }
    printk(KERN_INFO DRV_NAME ": MediaTek MT6575 MSDC Driver\n");

    //msdc_debug_proc_init();
    return 0;
}

static void __exit mt_msdc_exit(void)
{
    platform_driver_unregister(&mt_msdc_driver);
}

module_init(mt_msdc_init);
module_exit(mt_msdc_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MediaTek MT6575 SD/MMC Card Driver");
MODULE_AUTHOR("Infinity Chen <infinity.chen@mediatek.com>");

EXPORT_SYMBOL(msdc_6575_host);
