#include <linux/stddef.h>
#include <linux/module.h>
#include <linux/types.h>
#include <asm/byteorder.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/bitops.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <net/sch_generic.h>
#include <asm/unaligned.h>

#include "ag7100.h"
#include "ag7100_phy.h"
#include "ag7100_trc.h"
#ifdef CONFIG_BUFFALO
#include "rtl8366_smi.h"
RTL8366_FUNCS rtl_funcs = 
{
	NULL,
	NULL,
	NULL,
	NULL,
	NULL
};
#endif //CONFIG_BUFFALO //

unsigned int rx_hang_detect_pkt_cnt_all[2], rx_hang_detect_pkt_cnt_valid[2],rx_hang_detected[2];
int set_mac_from_link_flag = 0;
static ag7100_mac_t *ag7100_macs[2];
static void ag7100_hw_setup(ag7100_mac_t *mac);
static void ag7100_hw_stop(ag7100_mac_t *mac);
static void ag7100_oom_timer(unsigned long data);
static int  ag7100_check_link(ag7100_mac_t *mac);
static int  check_for_dma_hang(ag7100_mac_t *mac);
static int  ag7100_tx_alloc(ag7100_mac_t *mac);
static int  ag7100_rx_alloc(ag7100_mac_t *mac);
static void ag7100_rx_free(ag7100_mac_t *mac);
static void ag7100_tx_free(ag7100_mac_t *mac);
static int  ag7100_ring_alloc(ag7100_ring_t *r, int count);
static int  ag7100_rx_replenish(ag7100_mac_t *mac);
static int  ag7100_tx_reap(ag7100_mac_t *mac);
static void ag7100_ring_release(ag7100_mac_t *mac, ag7100_ring_t  *r);
static void ag7100_ring_free(ag7100_ring_t *r);
static void ag7100_tx_timeout_task(struct work_struct *work);
static void ag7100_get_default_macaddr(ag7100_mac_t *mac, u8 *mac_addr);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24)
static int ag7100_poll(struct napi_struct *napi, int budget);
#else
static int ag7100_poll(struct net_device *dev, int *budget);
#endif
static void ag7100_buffer_free(struct sk_buff *skb);
void ag7100_dma_reset(ag7100_mac_t *mac);
int board_version;
int  ag7100_recv_packets(struct net_device *dev, ag7100_mac_t *mac,
    int max_work, int *work_done);
static irqreturn_t ag7100_intr(int cpl, void *dev_id);
static struct sk_buff * ag7100_buffer_alloc(void);

char *mii_str[2][4] = {
    {"GMii", "Mii", "RGMii", "RMii"},
    {"RGMii", "RMii", "INVL1", "INVL2"}
};
char *spd_str[] = {"10Mbps", "100Mbps", "1000Mbps"};
char *dup_str[] = {"half duplex", "full duplex"};

#define MODULE_NAME "AG7100"

/* if 0 compute in init */
int tx_len_per_ds = 0;
#if defined(CONFIG_AR9100) && defined(CONFIG_AG7100_GE1_RMII)
void  ag7100_tx_flush(ag7100_mac_t *mac);
void howl_10baset_war(ag7100_mac_t *mac);
#endif
module_param(tx_len_per_ds, int, 0);
MODULE_PARM_DESC(tx_len_per_ds, "Size of DMA chunk");

/* if 0 compute in init */
int tx_max_desc_per_ds_pkt=0;

/* if 0 compute in init */
#ifdef CONFIG_AR9100
int fifo_3 = 0x780008;
#else
int fifo_3 = 0;
#endif
module_param(fifo_3, int, 0);
MODULE_PARM_DESC(fifo_3, "fifo cfg 3 settings");

int mii0_if = AG7100_MII0_INTERFACE;
module_param(mii0_if, int, 0);
MODULE_PARM_DESC(mii0_if, "mii0 connect");

int mii1_if = AG7100_MII1_INTERFACE;
module_param(mii1_if, int, 0);
MODULE_PARM_DESC(mii1_if, "mii1 connect");
#ifndef CONFIG_AR9100
#ifdef CONFIG_CAMEO_REALTEK_PHY
int gige_pll = 0x11110000;
int rtl_chip_type_select(void);
#else
int gige_pll = 0x0110000;
#endif
unsigned int e1000sr_pll[2]	= { 0x1e000100ul, 0x1e000100ul };
unsigned int e1000rb_pll[2]	= { 0x1f000000ul, 0x00000100ul };
unsigned int e100sr_pll[2]	= { 0x13000a44ul, 0x13000a44ul };
unsigned int e100rb_pll[2]	= { 0x13000a44ul, 0x13000a44ul };
unsigned int e10sr_pll[2]	= { 0x13000a44ul, 0x00441099ul };
unsigned int e10rb_pll[2]	= { 0x13000a44ul, 0x00441099ul };
unsigned int * e1000_pll;
unsigned int * e100_pll;
unsigned int * e10_pll;
#else
#ifdef	CONFIG_BUFFALO
unsigned int e1000sr_pll[2]	= { 0x1e000100ul, 0x1e000100ul };
#ifdef CONFIG_TPLINK
unsigned int e1000rb_pll[2]	= { 0x1a000000ul, 0x1a000000ul };
#else
unsigned int e1000rb_pll[2]	= { 0x1f000000ul, 0x00000100ul };
#endif

unsigned int e100sr_pll[2]	= { 0x13000a44ul, 0x13000a44ul };
unsigned int e100rb_pll[2]	= { 0x13000a44ul, 0x13000a44ul };
unsigned int e10sr_pll[2]	= { 0x13000a44ul, 0x00441099ul };
unsigned int e10rb_pll[2]	= { 0x13000a44ul, 0x00441099ul };
unsigned int * e1000_pll;
unsigned int * e100_pll;
unsigned int * e10_pll;
#endif	// CONFIG_BUFFALO //
#ifdef CONFIG_RTL8366RB_SMI
#define SW_PLL 0x1a000000ul
#elif defined(CONFIG_RTL8366_SMI_MODULE)
#define SW_PLL 0x1a000000ul
#else
#define SW_PLL 0x1f000000ul
#endif

int gige_pll = 0x1a000000;
#endif
module_param(gige_pll, int, 0);
MODULE_PARM_DESC(gige_pll, "Pll for (R)GMII if");

/*
* Cfg 5 settings
* Weed out junk frames (CRC errored, short collision'ed frames etc.)
*/
int fifo_5 = 0x7ffef;
module_param(fifo_5, int, 0);
MODULE_PARM_DESC(fifo_5, "fifo cfg 5 settings");

#define addr_to_words(addr, w1, w2)  {                                 \
    w1 = (addr[5] << 24) | (addr[4] << 16) | (addr[3] << 8) | addr[2]; \
    w2 = (addr[1] << 24) | (addr[0] << 16) | 0;                        \
}


/*
 * Defines specific to this implemention
 */

#ifndef CONFIG_AG7100_LEN_PER_TX_DS
#error Please run menuconfig and define CONFIG_AG7100_LEN_PER_TX_DS
#endif

#ifndef CONFIG_AG7100_NUMBER_TX_PKTS
#error Please run menuconfig and define CONFIG_AG7100_NUMBER_TX_PKTS
#endif

#ifndef CONFIG_AG7100_NUMBER_RX_PKTS
#error Please run menuconfig and define CONFIG_AG7100_NUMBER_RX_PKTS
#endif
#define AG7100_TX_FIFO_LEN          2048
#define AG7100_TX_MIN_DS_LEN        128
#define AG7100_TX_MAX_DS_LEN        AG7100_TX_FIFO_LEN

#define AG7100_TX_MTU_LEN           AG71XX_TX_MTU_LEN

#define AG7100_TX_DESC_CNT           CONFIG_AG7100_NUMBER_TX_PKTS*tx_max_desc_per_ds_pkt
#define AG7100_TX_REAP_THRESH        AG7100_TX_DESC_CNT/2
#define AG7100_TX_QSTART_THRESH      4*tx_max_desc_per_ds_pkt

#define AG7100_RX_DESC_CNT           CONFIG_AG7100_NUMBER_RX_PKTS

#define AG7100_NAPI_WEIGHT           64
#define AG7100_PHY_POLL_SECONDS      2
int dma_flag = 0;
static inline int ag7100_tx_reap_thresh(ag7100_mac_t *mac)
{
    ag7100_ring_t *r = &mac->mac_txring;
#if defined(CONFIG_AR9100) && defined(CONFIG_AG7100_GE1_RMII)
    if(mac->speed_10t) 
        return (ag7100_ndesc_unused(mac, r) < 2);
    else 
#endif
    return (ag7100_ndesc_unused(mac, r) < AG7100_TX_REAP_THRESH);
}

static inline int ag7100_tx_ring_full(ag7100_mac_t *mac)
{
    ag7100_ring_t *r = &mac->mac_txring;

    ag7100_trc_new(ag7100_ndesc_unused(mac, r),"tx ring full");
    return (ag7100_ndesc_unused(mac, r) < tx_max_desc_per_ds_pkt + 2);
}


static int
ag7100_open(struct net_device *dev)
{
    unsigned int w1 = 0, w2 = 0;
    ag7100_mac_t *mac = (ag7100_mac_t *)netdev_priv(dev);
    int st;
#if defined(CONFIG_AR9100) && defined(SWITCH_AHB_FREQ)
    u32 tmp_pll, pll;
#endif

    assert(mac);

    st = request_irq(mac->mac_irq, ag7100_intr, 0, dev->name, dev);
    if (st < 0)
    {
        printk(MODULE_NAME ": request irq %d failed %d\n", mac->mac_irq, st);
        return 1;
    }
    if (ag7100_tx_alloc(mac)) goto tx_failed;
    if (ag7100_rx_alloc(mac)) goto rx_failed;

    ag7100_hw_setup(mac);
#if defined(CONFIG_AR9100) && defined(SWITCH_AHB_FREQ)
    /* 
    * Reduce the AHB frequency to 100MHz while setting up the 
    * S26 phy. 
    */
    pll= ar7100_reg_rd(AR7100_PLL_CONFIG);
    tmp_pll = pll& ~((PLL_DIV_MASK << PLL_DIV_SHIFT) | (PLL_REF_DIV_MASK << PLL_REF_DIV_SHIFT));
    tmp_pll = tmp_pll | (0x64 << PLL_DIV_SHIFT) |
        (0x5 << PLL_REF_DIV_SHIFT) | (1 << AHB_DIV_SHIFT);

    ar7100_reg_wr_nf(AR7100_PLL_CONFIG, tmp_pll);
    udelay(100*1000);
#endif

#if defined(CONFIG_ATHRS26_PHY)
    /* if using header for register configuration, we have to     */
    /* configure s26 register after frame transmission is enabled */
    if (mac->mac_unit == 1) /* wan phy */
        athrs26_reg_init();
#elif defined(CONFIG_ATHRS16_PHY)
    if (mac->mac_unit == 1) 
        athrs16_reg_init();
#endif

    ag7100_phy_setup(mac->mac_unit);

#if defined(CONFIG_AR9100) && defined(SWITCH_AHB_FREQ)
    ar7100_reg_wr_nf(AR7100_PLL_CONFIG, pll);
    udelay(100*1000);
#endif
    /*
    * set the mac addr
    */
    addr_to_words(dev->dev_addr, w1, w2);
    ag7100_reg_wr(mac, AG7100_GE_MAC_ADDR1, w1);
    ag7100_reg_wr(mac, AG7100_GE_MAC_ADDR2, w2);

    /*
    * phy link mgmt
    */
    rx_hang_detect_pkt_cnt_all[mac->mac_unit] = ag7100_get_rx_count(mac);	    
    rx_hang_detect_pkt_cnt_valid[mac->mac_unit] = mac->net_rx_packets;
    rx_hang_detected[mac->mac_unit] = 0;

    init_timer(&mac->mac_phy_timer);
    mac->mac_phy_timer.data     = (unsigned long)mac;
    mac->mac_phy_timer.function = (void *)ag7100_check_link;
    ag7100_check_link(mac);

    dev->trans_start = jiffies;

    napi_enable(&mac->mac_napi);
    ag7100_int_enable(mac);
    ag7100_rx_start(mac);
    netif_start_queue(dev);

    ag7100_start_rx_count(mac);



    return 0;

rx_failed:
    ag7100_tx_free(mac);
tx_failed:
    free_irq(mac->mac_irq, dev);
    return 1;
}

static int
ag7100_stop(struct net_device *dev)
{
    ag7100_mac_t *mac = (ag7100_mac_t *)netdev_priv(dev);
    int flags;

    spin_lock_irqsave(&mac->mac_lock, flags);
    napi_disable(&mac->mac_napi);
    netif_stop_queue(dev);
    netif_carrier_off(dev);

    ag7100_hw_stop(mac);
    free_irq(mac->mac_irq, dev);

   /* 
    *  WAR for bug:32681 reduces the no of TX buffers to five from the
    *  actual number  of allocated buffers. Revert the value before freeing 
    *  them to avoid memory leak
    */
#if defined(CONFIG_AR9100) && defined(CONFIG_AG7100_GE1_RMII)
    mac->mac_txring.ring_nelem = AG7100_TX_DESC_CNT;
    mac->speed_10t = 0;
#endif

    ag7100_tx_free(mac);
    ag7100_rx_free(mac);


    del_timer(&mac->mac_phy_timer);
    spin_unlock_irqrestore(&mac->mac_lock, flags);

    /*ag7100_trc_dump();*/
    return 0;
}

#define FIFO_CFG0_WTM		BIT(0)	/* Watermark Module */
#define FIFO_CFG0_RXS		BIT(1)	/* Rx System Module */
#define FIFO_CFG0_RXF		BIT(2)	/* Rx Fabric Module */
#define FIFO_CFG0_TXS		BIT(3)	/* Tx System Module */
#define FIFO_CFG0_TXF		BIT(4)	/* Tx Fabric Module */
#define FIFO_CFG0_ALL	(FIFO_CFG0_WTM | FIFO_CFG0_RXS | FIFO_CFG0_RXF \
			| FIFO_CFG0_TXS | FIFO_CFG0_TXF)

#define FIFO_CFG0_ENABLE_SHIFT	8

#define FIFO_CFG4_DE		BIT(0)	/* Drop Event */
#define FIFO_CFG4_DV		BIT(1)	/* RX_DV Event */
#define FIFO_CFG4_FC		BIT(2)	/* False Carrier */
#define FIFO_CFG4_CE		BIT(3)	/* Code Error */
#define FIFO_CFG4_CR		BIT(4)	/* CRC error */
#define FIFO_CFG4_LM		BIT(5)	/* Length Mismatch */
#define FIFO_CFG4_LO		BIT(6)	/* Length out of range */
#define FIFO_CFG4_OK		BIT(7)	/* Packet is OK */
#define FIFO_CFG4_MC		BIT(8)	/* Multicast Packet */
#define FIFO_CFG4_BC		BIT(9)	/* Broadcast Packet */
#define FIFO_CFG4_DR		BIT(10)	/* Dribble */
#define FIFO_CFG4_LE		BIT(11)	/* Long Event */
#define FIFO_CFG4_CF		BIT(12)	/* Control Frame */
#define FIFO_CFG4_PF		BIT(13)	/* Pause Frame */
#define FIFO_CFG4_UO		BIT(14)	/* Unsupported Opcode */
#define FIFO_CFG4_VT		BIT(15)	/* VLAN tag detected */
#define FIFO_CFG4_FT		BIT(16)	/* Frame Truncated */
#define FIFO_CFG4_UC		BIT(17)	/* Unicast Packet */

#define FIFO_CFG5_DE		BIT(0)	/* Drop Event */
#define FIFO_CFG5_DV		BIT(1)	/* RX_DV Event */
#define FIFO_CFG5_FC		BIT(2)	/* False Carrier */
#define FIFO_CFG5_CE		BIT(3)	/* Code Error */
#define FIFO_CFG5_LM		BIT(4)	/* Length Mismatch */
#define FIFO_CFG5_LO		BIT(5)	/* Length Out of Range */
#define FIFO_CFG5_OK		BIT(6)	/* Packet is OK */
#define FIFO_CFG5_MC		BIT(7)	/* Multicast Packet */
#define FIFO_CFG5_BC		BIT(8)	/* Broadcast Packet */
#define FIFO_CFG5_DR		BIT(9)	/* Dribble */
#define FIFO_CFG5_CF		BIT(10)	/* Control Frame */
#define FIFO_CFG5_PF		BIT(11)	/* Pause Frame */
#define FIFO_CFG5_UO		BIT(12)	/* Unsupported Opcode */
#define FIFO_CFG5_VT		BIT(13)	/* VLAN tag detected */
#define FIFO_CFG5_LE		BIT(14)	/* Long Event */
#define FIFO_CFG5_FT		BIT(15)	/* Frame Truncated */
#define FIFO_CFG5_16		BIT(16)	/* unknown */
#define FIFO_CFG5_17		BIT(17)	/* unknown */
#define FIFO_CFG5_SF		BIT(18)	/* Short Frame */
#define FIFO_CFG5_BM		BIT(19)	/* Byte Mode */


#define FIFO_CFG0_INIT	(FIFO_CFG0_ALL << FIFO_CFG0_ENABLE_SHIFT)

#define FIFO_CFG4_INIT	(FIFO_CFG4_DE | FIFO_CFG4_DV | FIFO_CFG4_FC | \
			 FIFO_CFG4_CE | FIFO_CFG4_CR | FIFO_CFG4_LM | \
			 FIFO_CFG4_LO | FIFO_CFG4_OK | FIFO_CFG4_MC | \
			 FIFO_CFG4_BC | FIFO_CFG4_DR | FIFO_CFG4_LE | \
			 FIFO_CFG4_CF | FIFO_CFG4_PF | FIFO_CFG4_UO | \
			 FIFO_CFG4_VT)

#define FIFO_CFG5_INIT	(FIFO_CFG5_DE | FIFO_CFG5_DV | FIFO_CFG5_FC | \
			 FIFO_CFG5_CE | FIFO_CFG5_LO | FIFO_CFG5_OK | \
			 FIFO_CFG5_MC | FIFO_CFG5_BC | FIFO_CFG5_DR | \
			 FIFO_CFG5_CF | FIFO_CFG5_PF | FIFO_CFG5_VT | \
			 FIFO_CFG5_LE | FIFO_CFG5_FT | FIFO_CFG5_16 | \
			 FIFO_CFG5_17 | FIFO_CFG5_SF)


static void
ag7100_hw_setup(ag7100_mac_t *mac)
{
    ag7100_ring_t *tx = &mac->mac_txring, *rx = &mac->mac_rxring;
    ag7100_desc_t *r0, *t0;
#ifdef CONFIG_AR9100 
#ifndef CONFIG_PORT0_AS_SWITCH
    if(mac->mac_unit) {
#ifdef CONFIG_DUAL_F1E_PHY
    ag7100_reg_wr(mac, AG7100_MAC_CFG1, (AG7100_MAC_CFG1_RX_EN | AG7100_MAC_CFG1_TX_EN));
#else
    ag7100_reg_wr(mac, AG7100_MAC_CFG1, (AG7100_MAC_CFG1_RX_EN | AG7100_MAC_CFG1_TX_EN));
#endif
    }
    else {
	 ag7100_reg_wr(mac, AG7100_MAC_CFG1, (AG7100_MAC_CFG1_RX_EN | AG7100_MAC_CFG1_TX_EN));
   }
#else
   if(mac->mac_unit) {
    ag7100_reg_wr(mac, AG7100_MAC_CFG1, (AG7100_MAC_CFG1_RX_EN |AG7100_MAC_CFG1_TX_EN));
    }
    else {
         ag7100_reg_wr(mac, AG7100_MAC_CFG1, (AG7100_MAC_CFG1_RX_EN | AG7100_MAC_CFG1_TX_EN));
   }
#endif
#else
	ag7100_reg_wr(mac, AG7100_MAC_CFG1, (AG7100_MAC_CFG1_RX_EN |
        AG7100_MAC_CFG1_TX_EN));
#endif
    ag7100_reg_rmw_set(mac, AG7100_MAC_CFG2, (AG7100_MAC_CFG2_PAD_CRC_EN | AG7100_MAC_CFG2_LEN_CHECK));
    ag7100_reg_wr(mac, AG71XX_REG_MAC_MFL, AG71XX_TX_MTU_LEN);

    ag7100_reg_wr(mac, AG7100_MAC_FIFO_CFG_0, FIFO_CFG0_INIT);
    /*
    * set the mii if type - NB reg not in the gigE space
    */
    ar7100_reg_wr(mii_reg(mac), mii_if(mac));
    ag7100_reg_wr(mac, AG7100_MAC_MII_MGMT_CFG, AG7100_MGMT_CFG_CLK_DIV_20);

#ifdef CONFIG_AR7100_EMULATION
    ag7100_reg_rmw_set(mac, AG7100_MAC_FIFO_CFG_4, 0x3ffff);
    ag7100_reg_wr(mac, AG7100_MAC_FIFO_CFG_1, 0xfff0000);
    ag7100_reg_wr(mac, AG7100_MAC_FIFO_CFG_2, 0x1fff);
#else
    ag7100_reg_wr(mac, AG7100_MAC_FIFO_CFG_1, 0xfff0000);
    ag7100_reg_wr(mac, AG7100_MAC_FIFO_CFG_2, 0x1fff);
    /*
    * Weed out junk frames (CRC errored, short collision'ed frames etc.)
    */
    ag7100_reg_wr(mac, AG7100_MAC_FIFO_CFG_4, FIFO_CFG4_INIT);
    ag7100_reg_wr(mac, AG7100_MAC_FIFO_CFG_5, FIFO_CFG5_INIT);
#endif

    t0  =  &tx->ring_desc[0];
    r0  =  &rx->ring_desc[0];

    ag7100_reg_wr(mac, AG7100_DMA_TX_DESC, ag7100_desc_dma_addr(tx, t0));
    ag7100_reg_wr(mac, AG7100_DMA_RX_DESC, ag7100_desc_dma_addr(rx, r0));

//    printk(MODULE_NAME ": cfg1 %#x cfg2 %#x\n", ag7100_reg_rd(mac, AG7100_MAC_CFG1),
//        ag7100_reg_rd(mac, AG7100_MAC_CFG2));
}

static void
ag7100_hw_stop(ag7100_mac_t *mac)
{
    ag7100_rx_stop(mac);
    ag7100_tx_stop(mac);
    ag7100_int_disable(mac);
    /*
    * put everything into reset.
    */
#ifdef CONFIG_DUAL_F1E_PHY
	if(mac->mac_unit == 1)
#endif
    	ag7100_reg_rmw_set(mac, AG7100_MAC_CFG1, AG7100_MAC_CFG1_SOFT_RST);
}

/*
 * program the usb pll (misnomer) to genrate appropriate clock
 * Write 2 into control field
 * Write pll value 
 * Write 3 into control field 
 * Write 0 into control field 
 */
#ifdef CONFIG_AR9100
#define ag7100_pll_shift(_mac)      (((_mac)->mac_unit) ? 22: 20)
#define ag7100_pll_offset(_mac)     \
    (((_mac)->mac_unit) ? AR9100_ETH_INT1_CLK : \
                          AR9100_ETH_INT0_CLK)
#else
#define ag7100_pll_shift(_mac)      (((_mac)->mac_unit) ? 19: 17)
#define ag7100_pll_offset(_mac)     \
    (((_mac)->mac_unit) ? AR7100_USB_PLL_GE1_OFFSET : \
                          AR7100_USB_PLL_GE0_OFFSET)
#endif
static void
ag7100_set_pll(ag7100_mac_t *mac, unsigned int pll)
{
#ifdef CONFIG_AR9100
#define ETH_PLL_CONFIG AR9100_ETH_PLL_CONFIG
#else
#define ETH_PLL_CONFIG AR7100_USB_PLL_CONFIG
#endif 
    uint32_t shift, reg, val;

    shift = ag7100_pll_shift(mac);
    reg   = ag7100_pll_offset(mac);

    val  = ar7100_reg_rd(ETH_PLL_CONFIG);
    val &= ~(3 << shift);
    val |=  (2 << shift);
    ar7100_reg_wr(ETH_PLL_CONFIG, val);
    udelay(100);

    ar7100_reg_wr(reg, pll);

    val |=  (3 << shift);
    ar7100_reg_wr(ETH_PLL_CONFIG, val);
    udelay(100);

    val &= ~(3 << shift);
    ar7100_reg_wr(ETH_PLL_CONFIG, val);
    udelay(100);

//    printk(MODULE_NAME ": pll reg %#x: %#x  ", reg, ar7100_reg_rd(reg));
}

#if defined(CONFIG_AR9100) && defined(CONFIG_AG7100_GE1_RMII)


/* 
 * Flush from tail till the head and free all the socket buffers even if owned by DMA
 * before we change the size of the ring buffer to avoid memory leaks and reset the ring buffer.
 * 
 * WAR for Bug: 32681 
 */

void
ag7100_tx_flush(ag7100_mac_t *mac)
{
    ag7100_ring_t   *r     = &mac->mac_txring;
    int              head  = r->ring_nelem , tail = 0, flushed = 0, i;
    ag7100_desc_t   *ds;
    ag7100_buffer_t *bf;
    uint32_t    flags;


    ar7100_flush_ge(mac->mac_unit);

    while(flushed != head)
    {
        ds   = &r->ring_desc[tail];

        bf      = &r->ring_buffer[tail];
        if(bf->buf_pkt) {
            for(i = 0; i < bf->buf_nds; i++)
            {
                ag7100_intr_ack_tx(mac);
                ag7100_ring_incr(tail);
            }
        
            ag7100_buffer_free(bf->buf_pkt);
            bf->buf_pkt = NULL;
        } 
        else
            ag7100_ring_incr(tail);

        ag7100_tx_own(ds);
        flushed ++;
    }
    r->ring_head = r->ring_tail = 0;

    return;
}

/*
 * Work around to recover from Tx failure when connected to 10BASET.
 * Bug: 32681. 
 *
 * After AutoNeg to 10Mbps Half Duplex, under some un-identified circumstances
 * during the init sequence, the MAC is in some illegal state
 * that stops the TX and hence no TXCTL to the PHY. 
 * On Tx Timeout from the software, the reset sequence is done again which recovers the 
 * MAC and Tx goes through without any problem. 
 * Instead of waiting for the application to transmit and recover, we transmit 
 * 40 dummy Tx pkts on negogiating as 10BASET.
 * Reduce the number of TX buffers from 40 to 5 so that in case of TX failures we do
 * a immediate reset and retrasmit again till we successfully transmit all of them. 
 */

void
howl_10baset_war(ag7100_mac_t *mac)
{

    struct sk_buff *dummy_pkt;
    struct net_device *dev = mac->mac_dev;
    ag7100_desc_t *ds;
    ag7100_ring_t *r;
    int i=6;
   
    /*
     * Create dummy packet 
     */ 
    dummy_pkt = dev_alloc_skb(64);
    skb_put(dummy_pkt, 60);
    atomic_dec(&dummy_pkt->users);
    while(--i >= 0) {
        dummy_pkt->data[i] = 0xff;
    }
    ag7100_get_default_macaddr(mac,(dummy_pkt->data + 6));
    dummy_pkt->dev = dev;
    i = 40;

   /* 
    *  Reduce the no of TX buffers to five from the actual number
    *  of allocated buffers and link the fifth descriptor to first.
    *  WAR for Bug:32681 to cause early Tx Timeout in 10BASET.
    */
    ag7100_tx_flush(mac);
    ds = mac->mac_txring.ring_desc;
    r = &mac->mac_txring;
    r->ring_nelem = 5;
    ds[r->ring_nelem - 1].next_desc = ag7100_desc_dma_addr(r, &ds[0]);
    ag7100_reg_wr(mac, AG7100_MAC_FIFO_CFG_3, 0x300020);

    mac->speed_10t = 1;
    while(i-- && mac->speed_10t) {
        netif_carrier_on(dev);

        mdelay(100);
        ag7100_hard_start(dummy_pkt,dev); 

        netif_carrier_off(dev);
    }
    return ;
}
#endif
	
/*
 * Several fields need to be programmed based on what the PHY negotiated
 * Ideally we should quiesce everything before touching the pll, but:
 * 1. If its a linkup/linkdown, we dont care about quiescing the traffic.
 * 2. If its a single gigE PHY, this can only happen on lup/ldown.
 * 3. If its a 100Mpbs switch, the link will always remain at 100 (or nothing)
 * 4. If its a gigE switch then the speed should always be set at 1000Mpbs, 
 *    and the switch should provide buffering for slower devices.
 *
 * XXX Only gigE PLL can be changed as a parameter for now. 100/10 is hardcoded.
 * XXX Need defines for them -
 * XXX FIFO settings based on the mode
 */
#ifdef CONFIG_ATHRS16_PHY
static int is_setup_done = 0;
#endif
static void
ag7100_set_mac_from_link(ag7100_mac_t *mac, ag7100_phy_speed_t speed, int fdx)
{
#ifdef CONFIG_ATHRS26_PHY
    int change_flag = 0;

    if(mac->mac_speed !=  speed)
        change_flag = 1;

    if(change_flag)
    {
        athrs26_phy_off(mac);
        athrs26_mac_speed_set(mac, speed);
    }
#endif
#ifdef CONFIG_ATHRS16_PHY 
    if(!is_setup_done && 
#ifndef CONFIG_PORT0_AS_SWITCH
        mac->mac_unit == 0 && 
#else
        mac->mac_unit == 1 && 
#endif
        (mac->mac_speed !=  speed || mac->mac_fdx !=  fdx)) 
    {   
       /* workaround for PHY4 port thru RGMII */
       phy_mode_setup();
       is_setup_done = 1;
    }
#endif
   /*
    *  Flush TX descriptors , reset the MAC and relink all descriptors.
    *  WAR for Bug:32681 
    */

#if defined(CONFIG_AR9100) && defined(CONFIG_AG7100_GE1_RMII)
    if(mac->speed_10t && (speed != AG7100_PHY_SPEED_10T)) {
        mac->speed_10t = 0;
        ag7100_tx_flush(mac);
        mdelay(500);
	ag7100_dma_reset(mac);
    }
#endif

    mac->mac_speed =  speed;
    mac->mac_fdx   =  fdx;

    ag7100_set_mii_ctrl_speed(mac, speed);
    ag7100_set_mac_duplex(mac, fdx);
    ag7100_reg_wr(mac, AG7100_MAC_FIFO_CFG_3, fifo_3);
#ifndef CONFIG_AR9100
    ag7100_reg_wr(mac, AG7100_MAC_FIFO_CFG_5, fifo_5);
#endif

    switch (speed)
    {
    case AG7100_PHY_SPEED_1000T:
#ifdef CONFIG_AR9100
        ag7100_reg_wr(mac, AG7100_MAC_FIFO_CFG_3, 0x780fff);
#endif
        ag7100_set_mac_if(mac, 1);
#ifdef CONFIG_AR9100
#ifdef	CONFIG_BUFFALO
        ag7100_set_pll(mac, e1000_pll[mac->mac_unit ? 1 : 0]);
#else
        if (mac->mac_unit == 0)
        { /* eth0 */
            ag7100_set_pll(mac, gige_pll);
        }
        else
        {
#ifdef CONFIG_DUAL_F1E_PHY
            ag7100_set_pll(mac, gige_pll);
#else
            ag7100_set_pll(mac, SW_PLL);
#endif
        }
#endif
#else

#ifdef	CONFIG_BUFFALO
        ag7100_set_pll(mac, e1000_pll[mac->mac_unit ? 1 : 0]);
#else
        ag7100_set_pll(mac, gige_pll);
#endif
#endif
        ag7100_reg_rmw_set(mac, AG7100_MAC_FIFO_CFG_5, (1 << 19));
        break;

    case AG7100_PHY_SPEED_100TX:
        ag7100_set_mac_if(mac, 0);
        ag7100_set_mac_speed(mac, 1);
#ifndef CONFIG_AR7100_EMULATION
#ifdef CONFIG_AR9100
#ifdef	CONFIG_BUFFALO
        ag7100_set_pll(mac, e100_pll[mac->mac_unit ? 1 : 0]);
#else
        if (mac->mac_unit == 0)
        { /* eth0 */
            ag7100_set_pll(mac, 0x13000a44);
        }
        else
        {
#ifdef CONFIG_DUAL_F1E_PHY
            ag7100_set_pll(mac, 0x13000a44);
#else
            ag7100_set_pll(mac, SW_PLL);
#endif
        }
#endif
#else
#ifdef	CONFIG_BUFFALO
        ag7100_set_pll(mac, e100_pll[mac->mac_unit ? 1 : 0]);
#else
#ifdef CONFIG_CAMEO_REALTEK_PHY
        ag7100_set_pll(mac, 0x0001099);
#else
        ag7100_set_pll(mac, 0x0001099);
#endif
#endif
#endif
#endif
        ag7100_reg_rmw_clear(mac, AG7100_MAC_FIFO_CFG_5, (1 << 19));
        break;

    case AG7100_PHY_SPEED_10T:
        ag7100_set_mac_if(mac, 0);
        ag7100_set_mac_speed(mac, 0);
#ifdef CONFIG_AR9100
#ifdef	CONFIG_BUFFALO
        ag7100_set_pll(mac, e10_pll[mac->mac_unit ? 1 : 0]);
#else
        if (mac->mac_unit == 0)
        { /* eth0 */
            ag7100_set_pll(mac, 0x00441099);
        }
        else
        {
#ifdef CONFIG_DUAL_F1E_PHY
            ag7100_set_pll(mac, 0x00441099);
#else
            ag7100_set_pll(mac, SW_PLL);
#endif
        }
#endif
#else
#ifdef	CONFIG_BUFFALO
        ag7100_set_pll(mac, e10_pll[mac->mac_unit ? 1 : 0]);
#else
#ifdef CONFIG_CAMEO_REALTEK_PHY
        ag7100_set_pll(mac, 0x00991099);
#else
        ag7100_set_pll(mac, 0x00991099);
#endif
#endif
#endif
#if defined(CONFIG_AR9100) && defined(CONFIG_AG7100_GE1_RMII)
        if((speed == AG7100_PHY_SPEED_10T) && !mac->speed_10t) {
           howl_10baset_war(mac);
        }
#endif
        ag7100_reg_rmw_clear(mac, AG7100_MAC_FIFO_CFG_5, (1 << 19));
        break;

    default:
        assert(0);
    }

#ifdef CONFIG_ATHRS26_PHY
    if(change_flag) 
        athrs26_phy_on(mac);
#endif

/*    printk(MODULE_NAME ": CPU PhaseLockLoop      : %#x\n", *(volatile int *) 0xb8050000);  //set CPU PhaseLockLoop configuration
    printk(MODULE_NAME ": Secondary PhaseLockLoop: %#x\n", *(volatile int *) 0xb8050004);  //set secondary PhaseLockLoop configuration
    printk(MODULE_NAME ": Ethernet Internal Clock Control: %#x\n", *(volatile int *) 0xb8050010);  //set Ethernet Internal Clock Control
    printk(MODULE_NAME ": mii: %#x\n", ar7100_reg_rd(mii_reg(mac)));
    printk(MODULE_NAME ": cfg1: %#x\n", ag7100_reg_rd(mac, AG7100_MAC_CFG1));
    printk(MODULE_NAME ": cfg2: %#x\n", ag7100_reg_rd(mac, AG7100_MAC_CFG2));        
    printk(MODULE_NAME ": fcfg_0: %#x\n", ag7100_reg_rd(mac, AG7100_MAC_FIFO_CFG_0));
    printk(MODULE_NAME ": fcfg_1: %#x\n", ag7100_reg_rd(mac, AG7100_MAC_FIFO_CFG_1));
    printk(MODULE_NAME ": fcfg_2: %#x\n", ag7100_reg_rd(mac, AG7100_MAC_FIFO_CFG_2));
    printk(MODULE_NAME ": fcfg_3: %#x\n", ag7100_reg_rd(mac, AG7100_MAC_FIFO_CFG_3));
    printk(MODULE_NAME ": fcfg_4: %#x\n", ag7100_reg_rd(mac, AG7100_MAC_FIFO_CFG_4));
    printk(MODULE_NAME ": fcfg_5: %#x\n", ag7100_reg_rd(mac, AG7100_MAC_FIFO_CFG_5));*/
}

static void copy_txdescs(ag7100_mac_t *mac, int start, int end)
{
    ag7100_ring_t      *r   = &mac->mac_txring;
    ag7100_ring_t      *tr   = &mac->mac_txring_cache;
    ag7100_desc_t      *tds, *fds;

    if (end >= r->ring_nelem) end -= r->ring_nelem;
    while (start != end)
    {
        fds = &r->ring_desc[start];
        tds = &tr->ring_desc[start];
        memcpy(tds, fds, 8); /* just the first two words of the desc */
        ag7100_ring_incr(start);
    }
}

static int check_for_dma_hang(ag7100_mac_t *mac) {

    ag7100_ring_t   *r     = &mac->mac_txring;
    int              head  = r->ring_head, tail = r->ring_tail;
    ag7100_desc_t   *ds;
#if 1//DMA tx hang
    ag7100_buffer_t *bf;
#endif
    ag7100_buffer_t *bp;

    ar7100_flush_ge(mac->mac_unit);

    while (tail != head)
    {
        ds   = &r->ring_desc[tail];
        bp   =  &r->ring_buffer[tail];

        if(ag7100_tx_owned_by_dma(ds)) {
                        if ((jiffies - bp->trans_start) > (1 * HZ)) {
//                printk(MODULE_NAME ": Tx Dma status : %s\n",
//                ag7100_tx_stopped(mac) ? "inactive" : "active");
#if 0
//                printk(MODULE_NAME ": timestamp:%u jiffies:%u diff:%d\n",bp->trans_start,jiffies,
//                             (jiffies - bp->trans_start));
#endif
               ag7100_dma_reset(mac);
                           return 1;
           }
        }
        ag7100_ring_incr(tail);
    }
    return 0;
}


/*
 * phy link state management
 */
static int
ag7100_check_link(ag7100_mac_t *mac)
{
    struct net_device  *dev     = mac->mac_dev;
    int                 carrier = netif_carrier_ok(dev), fdx, phy_up=1;
    ag7100_phy_speed_t  speed;
    int                 rc;

    /* workaround for dma hang, seen on DIR-825 */
//    if(check_for_dma_hang(mac))
//        goto done;

    /* The vitesse switch uses an indirect method to communicate phy status
    * so it is best to limit the number of calls to what is necessary.
    * However a single call returns all three pieces of status information.
    * 
    * This is a trivial change to the other PHYs ergo this change.
    *
    */
    
    /*
    ** If this is not connected, let's just jump out
    */
    
    if(mii_if(mac) > 3)
        goto done;

    rc = ag7100_get_link_status(mac->mac_unit, &phy_up, &fdx, &speed);
    if (rc < 0)
        goto done;

    if (!phy_up)
    {
        if (carrier)
        {
//            printk(MODULE_NAME ": unit %d: phy not up carrier %d\n", mac->mac_unit, carrier);
            netif_carrier_off(dev);
        }
        goto done;
    }

    /*
    * phy is up. Either nothing changed or phy setttings changed while we 
    * were sleeping.
    */

    if ((fdx < 0) || (speed < 0))
    {
        printk(MODULE_NAME ": phy not connected?\n");
        return 0;
    }

    if (carrier && (speed == mac->mac_speed) && (fdx == mac->mac_fdx)) 
        goto done;

//    printk(MODULE_NAME ": unit %d phy is up...", mac->mac_unit);
//    printk("%s %s %s\n", mii_str[mac->mac_unit][mii_if(mac)], 
//        spd_str[speed], dup_str[fdx]);

    ag7100_set_mac_from_link(mac, speed, fdx);

//    printk(MODULE_NAME ": done cfg2 %#x ifctl %#x miictrl %#x \n", 
//        ag7100_reg_rd(mac, AG7100_MAC_CFG2), 
//        ag7100_reg_rd(mac, AG7100_MAC_IFCTL),
//        ar7100_reg_rd(mii_reg(mac)));
    /*
    * in business
    */
    netif_carrier_on(dev);

done:
#if defined(CONFIG_ATHRS26_PHY) || defined(CONFIG_ATHRS16_PHY)    
    if(!phy_up)    
        mod_timer(&mac->mac_phy_timer, jiffies + AG7100_PHY_POLL_SECONDS*HZ/4);
    else
#endif        
    mod_timer(&mac->mac_phy_timer, jiffies + AG7100_PHY_POLL_SECONDS*HZ);

/* "Hydra WAN + RealTek PHY with a specific NetGear Hub" Rx hang workaround */
#if 0//ndef CONFIG_AR9100 //1//DMA mac hang
     {
        unsigned int perf_cnt = ag7100_get_rx_count(mac);
        if (perf_cnt == 0xffffffff) {
            /* we have saturated the counter. let it overflow to 0 */
            if (mac->mac_unit == 0) {
                ar7100_reg_wr(AR7100_PERF0_COUNTER, 0);
            }
            else {
                ar7100_reg_wr(AR7100_PERF1_COUNTER, 0);
            }
        }
		int status;
		status = ag7100_reg_rd(mac, AG7100_DMA_RX_STATUS);
        /* perf_cnt increments on every rx pkt including runts.
         * so, the rx hang occurred when perf_cnt incremented, but
         * valid rx pkts didn't get incremented. this could result
         * in a false positive but the likelihood that over a 2sec
         * period all pkts received were runts appears to me
         * to be very low -JK.
         */
	
		if ((perf_cnt > rx_hang_detect_pkt_cnt_all[mac->mac_unit]) &&
            (mac->net_rx_packets == rx_hang_detect_pkt_cnt_valid[mac->mac_unit]) &&
	    (!(status & AG7100_RX_STATUS_PKT_RCVD)) &&
	    (!((status & AG7100_RX_STATUS_PKTCNT_MASK )>>16))) {
	     	rx_hang_detected[mac->mac_unit] += 1;
//	     	if ( mac->mac_unit == 1 )	     
//            	printk(MODULE_NAME ": WAN Rx Hang Detected %d times!\n",rx_hang_detected[mac->mac_unit]);
//	    	 else
//				printk(MODULE_NAME ": LAN Rx Hang Detected %d times!\n",rx_hang_detected[mac->mac_unit]);
            rx_hang_detect_pkt_cnt_all[mac->mac_unit] = perf_cnt;
	    	rx_hang_detect_pkt_cnt_valid[mac->mac_unit] = mac->net_rx_packets;

	    	if (rx_hang_detected[mac->mac_unit] >= 2)
		     	ag7100_dma_reset(mac);
        }
        else {
            rx_hang_detect_pkt_cnt_all[mac->mac_unit] = perf_cnt;
            rx_hang_detect_pkt_cnt_valid[mac->mac_unit] = mac->net_rx_packets;
	    	rx_hang_detected[mac->mac_unit] = 0;
        }
    }
#endif

    return 0;
}

static void
ag7100_choose_phy(uint32_t phy_addr)
{
#ifdef CONFIG_AR7100_EMULATION
    if (phy_addr == 0x10)
    {
        ar7100_reg_rmw_set(AR7100_MII0_CTRL, (1 << 6));
    }
    else
    {
        ar7100_reg_rmw_clear(AR7100_MII0_CTRL, (1 << 6));
    }
#endif
}

uint16_t
ag7100_mii_read(int unit, uint32_t phy_addr, uint8_t reg)
{
    ag7100_mac_t *mac   = ag7100_unit2mac(0);
    uint16_t      addr  = (phy_addr << AG7100_ADDR_SHIFT) | reg, val;
    volatile int           rddata;
    uint16_t      ii = 0x1000;

    ag7100_choose_phy(phy_addr);

    ag7100_reg_wr(mac, AG7100_MII_MGMT_CMD, 0x0);
    ag7100_reg_wr(mac, AG7100_MII_MGMT_ADDRESS, addr);
    ag7100_reg_wr(mac, AG7100_MII_MGMT_CMD, AG7100_MGMT_CMD_READ);

    do
    {
        udelay(5);
        rddata = ag7100_reg_rd(mac, AG7100_MII_MGMT_IND) & 0x1;
    }while(rddata && --ii);

    val = ag7100_reg_rd(mac, AG7100_MII_MGMT_STATUS);
    ag7100_reg_wr(mac, AG7100_MII_MGMT_CMD, 0x0);

    return val;
}

void
ag7100_mii_write(int unit, uint32_t phy_addr, uint8_t reg, uint16_t data)
{
    ag7100_mac_t *mac   = ag7100_unit2mac(0);
    uint16_t      addr  = (phy_addr << AG7100_ADDR_SHIFT) | reg;
    volatile int rddata;
    uint16_t      ii = 0x1000;

    ag7100_choose_phy(phy_addr);

    ag7100_reg_wr(mac, AG7100_MII_MGMT_ADDRESS, addr);
    ag7100_reg_wr(mac, AG7100_MII_MGMT_CTRL, data);

    do
    {
        rddata = ag7100_reg_rd(mac, AG7100_MII_MGMT_IND) & 0x1;
    }while(rddata && --ii);
}

/*
 * Tx operation:
 * We do lazy reaping - only when the ring is "thresh" full. If the ring is 
 * full and the hardware is not even done with the first pkt we q'd, we turn
 * on the tx interrupt, stop all q's and wait for h/w to
 * tell us when its done with a "few" pkts, and then turn the Qs on again.
 *
 * Locking:
 * The interrupt only touches the ring when Q's stopped  => Tx is lockless, 
 * except when handling ring full.
 *
 * Desc Flushing: Flushing needs to be handled at various levels, broadly:
 * - The DDr FIFOs for desc reads.
 * - WB's for desc writes.
 */
static void
ag7100_handle_tx_full(ag7100_mac_t *mac)
{
    u32         flags;
#if defined(CONFIG_AR9100) && defined(CONFIG_AG7100_GE1_RMII)
    if(!mac->speed_10t)
#endif
    assert(!netif_queue_stopped(mac->mac_dev));

    mac->mac_net_stats.tx_fifo_errors ++;

    netif_stop_queue(mac->mac_dev);

    spin_lock_irqsave(&mac->mac_lock, flags);
    ag7100_intr_enable_tx(mac);
    spin_unlock_irqrestore(&mac->mac_lock, flags);
}

/* ******************************
 * 
 * Code under test - do not use
 *
 * ******************************
 */

static ag7100_desc_t *
ag7100_get_tx_ds(ag7100_mac_t *mac, int *len, unsigned char **start)
{
    ag7100_desc_t      *ds;
    int                len_this_ds;
    ag7100_ring_t      *r   = &mac->mac_txring;
    ag7100_buffer_t    *bp;

    /* force extra pkt if remainder less than 4 bytes */
    if (*len > tx_len_per_ds)
        if (*len <= (tx_len_per_ds + 4))
            len_this_ds = tx_len_per_ds - 4;
        else
            len_this_ds = tx_len_per_ds;
    else
        len_this_ds    = *len;

    ds = &r->ring_desc[r->ring_head];

    ag7100_trc_new(ds,"ds addr");
    ag7100_trc_new(ds,"ds len");
    if(ag7100_tx_owned_by_dma(ds))
        ag7100_dma_reset(mac);

    ds->pkt_size       = len_this_ds;
    ds->pkt_start_addr = virt_to_phys(*start);
    ds->more           = 1;

    *len   -= len_this_ds;
    *start += len_this_ds;

     bp = &r->ring_buffer[r->ring_head];
     bp->trans_start = jiffies; /*Time stamp each packet */

    ag7100_ring_incr(r->ring_head);

    return ds;
}

#if defined(CONFIG_ATHRS26_PHY)
int
#else
static int
#endif
ag7100_hard_start(struct sk_buff *skb, struct net_device *dev)
{
    ag7100_mac_t       *mac = (ag7100_mac_t *)netdev_priv(dev);
    ag7100_ring_t      *r   = &mac->mac_txring;
    ag7100_buffer_t    *bp;
    ag7100_desc_t      *ds, *fds;
    unsigned char      *start;
    int                len;
    int                nds_this_pkt;

#ifdef VSC73XX_DEBUG
    {
        static int vsc73xx_dbg;
        if (vsc73xx_dbg == 0) {
            vsc73xx_get_link_status_dbg();
            vsc73xx_dbg = 1;
        }
        vsc73xx_dbg = (vsc73xx_dbg + 1) % 10;
    }
#endif

#if defined(CONFIG_ATHRS26_PHY) && defined(HEADER_EN)
    /* add header to normal frames */
    /* check if normal frames */
    if ((mac->mac_unit == 0) && (!((skb->cb[0] == 0x7f) && (skb->cb[1] == 0x5d))))
    {
        skb_push(skb, HEADER_LEN);
        skb->data[0] = 0x10; /* broadcast = 0; from_cpu = 0; reserved = 1; port_num = 0 */
        skb->data[1] = 0x80; /* reserved = 0b10; priority = 0; type = 0 (normal) */
    }

#if defined(CONFIG_VLAN_8021Q) || defined(CONFIG_VLAN_8021Q_MODULE)
    if(unlikely((skb->len <= 0) 
        || (skb->len > (dev->mtu + ETH_HLEN + HEADER_LEN + 4))))
    { /*vlan tag length = 4*/
        printk(MODULE_NAME ": [%d] bad skb, dev->mtu=%d,ETH_HLEN=%d len %d\n", mac->mac_unit, dev->mtu, ETH_HLEN,  skb->len);
        goto dropit;
    }
#else
    if(unlikely((skb->len <= 0) 
        || (skb->len > (dev->mtu + ETH_HLEN + HEADER_LEN))))
    {
        printk(MODULE_NAME ": [%d] bad skb, dev->mtu=%d,ETH_HLEN=%d len %d\n", mac->mac_unit, dev->mtu, ETH_HLEN,  skb->len);
        goto dropit;
    }
#endif  

#else
#if defined(CONFIG_VLAN_8021Q) || defined(CONFIG_VLAN_8021Q_MODULE)
    if(unlikely((skb->len <= 0) || (skb->len > (dev->mtu + ETH_HLEN + 4))))
    {  /*vlan tag length = 4*/
        printk(MODULE_NAME ": bad skb, len %d\n", skb->len);
        goto dropit;
    }
#else
    if(unlikely((skb->len <= 0) || (skb->len > (dev->mtu + ETH_HLEN))))
    {
        printk(MODULE_NAME ": bad skb, len %d\n", skb->len);
        goto dropit;
    }
#endif    
#endif

    if (ag7100_tx_reap_thresh(mac)) 
        ag7100_tx_reap(mac);

    ag7100_trc_new(r->ring_head,"hard-stop hd");
    ag7100_trc_new(r->ring_tail,"hard-stop tl");

    ag7100_trc_new(skb->len,    "len this pkt");
    ag7100_trc_new(skb->data,   "ptr 2 pkt");

    dma_cache_sync(NULL, (void *)skb->data, skb->len, DMA_TO_DEVICE);

    bp          = &r->ring_buffer[r->ring_head];
    bp->buf_pkt = skb;
    len         = skb->len;
    start       = skb->data;

    assert(len>4);

    nds_this_pkt = 1;
    fds = ds = ag7100_get_tx_ds(mac, &len, &start);

    while (len>0)
    {
        ds = ag7100_get_tx_ds(mac, &len, &start);
        nds_this_pkt++;
        ag7100_tx_give_to_dma(ds);
    }

    ds->more        = 0;
    ag7100_tx_give_to_dma(fds);

    bp->buf_lastds  = ds;
    bp->buf_nds     = nds_this_pkt;

    ag7100_trc_new(ds,"last ds");
    ag7100_trc_new(nds_this_pkt,"nmbr ds for this pkt");

    wmb();

    mac->net_tx_packets ++;
    mac->net_tx_bytes += skb->len;

    ag7100_trc(ag7100_reg_rd(mac, AG7100_DMA_TX_CTRL),"dma idle");

    ag7100_tx_start(mac);

    if (unlikely(ag7100_tx_ring_full(mac)))
        ag7100_handle_tx_full(mac);

    dev->trans_start = jiffies;

    return NETDEV_TX_OK;

dropit:
    printk(MODULE_NAME ": dropping skb %p\n", skb);
    kfree_skb(skb);
    return NETDEV_TX_OK;
}

/*
 * Interrupt handling:
 * - Recv NAPI style (refer to Documentation/networking/NAPI)
 *
 *   2 Rx interrupts: RX and Overflow (OVF).
 *   - If we get RX and/or OVF, schedule a poll. Turn off _both_ interurpts. 
 *
 *   - When our poll's called, we
 *     a) Have one or more packets to process and replenish
 *     b) The hardware may have stopped because of an OVF.
 *
 *   - We process and replenish as much as we can. For every rcvd pkt 
 *     indicated up the stack, the head moves. For every such slot that we
 *     replenish with an skb, the tail moves. If head catches up with the tail
 *     we're OOM. When all's done, we consider where we're at:
 *
 *      if no OOM:
 *      - if we're out of quota, let the ints be disabled and poll scheduled.
 *      - If we've processed everything, enable ints and cancel poll.
 *
 *      If OOM:
 *      - Start a timer. Cancel poll. Ints still disabled. 
 *        If the hardware's stopped, no point in restarting yet. 
 *
 *      Note that in general, whether we're OOM or not, we still try to
 *      indicate everything recvd, up.
 *
 * Locking: 
 * The interrupt doesnt touch the ring => Rx is lockless
 *
 */
static irqreturn_t
ag7100_intr(int cpl, void *dev_id)
{
    struct net_device *dev  = (struct net_device *)dev_id;
    ag7100_mac_t      *mac  = (ag7100_mac_t *)netdev_priv(dev);
    int   isr, imr, handled = 0;

    isr   = ag7100_get_isr(mac);
    imr   = ag7100_reg_rd(mac, AG7100_DMA_INTR_MASK);

    ag7100_trc(isr,"isr");
    ag7100_trc(imr,"imr");

    assert(isr == (isr & imr));

    if (likely(isr & (AG7100_INTR_RX | AG7100_INTR_RX_OVF)))
    {
        handled = 1;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24)
	if (likely(napi_schedule_prep(&mac->mac_napi)))
#else
	if (likely(netif_rx_schedule_prep(dev)))
#endif
        {
            ag7100_intr_disable_recv(mac);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24)
            __napi_schedule(&mac->mac_napi);
#else
            __netif_rx_schedule(dev);
#endif
        }
        else
        {
            printk(MODULE_NAME ": driver bug! interrupt while in poll\n");
            assert(0);
            ag7100_intr_disable_recv(mac);
        }
        /*ag7100_recv_packets(dev, mac, 200, &budget);*/
    }
    if (likely(isr & AG7100_INTR_TX))
    {
        handled = 1;
        ag7100_intr_ack_tx(mac);
        ag7100_tx_reap(mac);
    }
    if (unlikely(isr & AG7100_INTR_RX_BUS_ERROR))
    {
        assert(0);
        handled = 1;
        ag7100_intr_ack_rxbe(mac);
    }
    if (unlikely(isr & AG7100_INTR_TX_BUS_ERROR))
    {
        assert(0);
        handled = 1;
        ag7100_intr_ack_txbe(mac);
    }

    if (!handled)
    {
        assert(0);
        printk(MODULE_NAME ": unhandled intr isr %#x\n", isr);
    }

    return IRQ_HANDLED;
}

 /*
  * Rx and Tx DMA hangs and goes to an invalid state in HOWL boards 
  * when the link partner is forced to 10/100 Mode.By resetting the MAC
  * we are able to recover from this state.This is a software  WAR and
  * will be removed once we have a hardware fix. 
  */

#if 1//def CONFIG_AR9100

void ag7100_dma_reset(ag7100_mac_t *mac)
{
    uint32_t mask;

    if(mac->mac_unit)
        mask = AR7100_RESET_GE1_MAC;
    else
        mask = AR7100_RESET_GE0_MAC;

    ar7100_reg_rmw_set(AR7100_RESET, mask);
    mdelay(100);
    ar7100_reg_rmw_clear(AR7100_RESET, mask);
    mdelay(100);

    ag7100_intr_disable_recv(mac);
#if defined(CONFIG_AR9100) && defined(CONFIG_AG7100_GE1_RMII)
    mac->speed_10t = 0;
#endif
    schedule_work(&mac->mac_tx_timeout);
}

#endif

static int
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24)
ag7100_poll(struct napi_struct *napi, int budget)
#else
ag7100_poll(struct net_device *dev, int *budget)
#endif
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24)
	ag7100_mac_t *mac = container_of(napi, ag7100_mac_t, mac_napi);
	struct net_device *dev = mac->mac_dev;
	int work_done=0,      max_work  = budget, status = 0;
#else
	ag7100_mac_t       *mac       = (ag7100_mac_t *)netdev_priv(dev);
	int work_done=0,      max_work  = min(*budget, dev->quota), status = 0;
#endif
    ag7100_rx_status_t  ret;
    u32                 flags;
    spin_lock_irqsave(&mac->mac_lock, flags);

    ret = ag7100_recv_packets(dev, mac, max_work, &work_done);


#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24)
	if (likely(ret == AG7100_RX_STATUS_DONE) && work_done < budget)
		{
    		napi_complete(napi);
    		ag7100_intr_enable_recv(mac);
    		}
#else
    dev->quota  -= work_done;
    *budget     -= work_done;
    if (likely(ret == AG7100_RX_STATUS_DONE))
    {
    netif_rx_complete(dev);
    }
#endif
    if(ret == AG7100_RX_DMA_HANG)
    {
        status = 0;
        ag7100_dma_reset(mac);
    }

    if (likely(ret == AG7100_RX_STATUS_NOT_DONE))
    {
        /*
        * We have work left
        */
        status = 1;
    	napi_complete(napi);
    	napi_reschedule(napi);
    }
    else if (ret == AG7100_RX_STATUS_OOM)
    {
        printk(MODULE_NAME ": oom..?\n");
        /* 
        * Start timer, stop polling, but do not enable rx interrupts.
        */
        mod_timer(&mac->mac_oom_timer, jiffies+1);
    }
    spin_unlock_irqrestore(&mac->mac_lock, flags);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24)
	return work_done;
#else
	return status;
#endif
}

int
ag7100_recv_packets(struct net_device *dev, ag7100_mac_t *mac, 
    int quota, int *work_done)
{
    ag7100_ring_t       *r     = &mac->mac_rxring;
    ag7100_desc_t       *ds;
    ag7100_buffer_t     *bp;
    struct sk_buff      *skb;
    ag7100_rx_status_t   ret   = AG7100_RX_STATUS_DONE;
    int head = r->ring_head, len, status, iquota = quota, more_pkts, rep;
    int i;
    ag7100_trc(iquota,"iquota");
#if !defined(CONFIG_AR9100)
    status = ag7100_reg_rd(mac, AG7100_DMA_RX_STATUS);
#endif

process_pkts:
    ag7100_trc(status,"status");
#if !defined(CONFIG_AR9100)
    /*
    * Under stress, the following assertion fails.
    *
    * On investigation, the following `appears' to happen.
    *   - pkts received
    *   - rx intr
    *   - poll invoked
    *   - process received pkts
    *   - replenish buffers
    *   - pkts received
    *
    *   - NO RX INTR & STATUS REG NOT UPDATED <---
    *
    *   - s/w doesn't process pkts since no intr
    *   - eventually, no more buffers for h/w to put
    *     future rx pkts
    *   - RX overflow intr
    *   - poll invoked
    *   - since status reg is not getting updated
    *     following assertion fails..
    *
    * Ignore the status register.  Regardless of this
    * being a rx or rx overflow, we have packets to process.
    * So, we go ahead and receive the packets..
    */
//    assert((status & AG7100_RX_STATUS_PKT_RCVD));
//    assert((status >> 16));
#endif
    /*
    * Flush the DDR FIFOs for our gmac
    */
    ar7100_flush_ge(mac->mac_unit);

    assert(quota > 0); /* WCL */

    while(quota)
    {
        ds    = &r->ring_desc[head];

        ag7100_trc(head,"hd");
        ag7100_trc(ds,  "ds");

        if (ag7100_rx_owned_by_dma(ds))
        {
    	    break;
#if 0
            if(quota == iquota)
            {
                *work_done = quota = 0;
                return AG7100_RX_DMA_HANG;
            }
            break;
#endif
        }
        ag7100_intr_ack_rx(mac);

        bp                  = &r->ring_buffer[head];
        len                 = ds->pkt_size;
        skb                 = bp->buf_pkt;
        assert(skb);
        skb_put(skb, len - ETHERNET_FCS_SIZE);

#if defined(CONFIG_ATHRS26_PHY) && defined(HEADER_EN)
        uint8_t type;
        uint16_t def_vid;

        if(mac->mac_unit == 0)
        {
            type = (skb->data[1]) & 0xf;

            if (type == NORMAL_PACKET)
            {
#if defined(CONFIG_VLAN_8021Q) || defined(CONFIG_VLAN_8021Q_MODULE)            	
                /*cpu egress tagged*/
                if (is_cpu_egress_tagged())
                {
                    if ((skb->data[12 + HEADER_LEN] != 0x81) || (skb->data[13 + HEADER_LEN] != 0x00))
                    {
                        def_vid = athrs26_defvid_get(skb->data[0] & 0xf);
                        skb_push(skb, 2); /* vid lenghth - header length */
                        memmove(&skb->data[0], &skb->data[4], 12); /*remove header and add vlan tag*/

                        skb->data[12] = 0x81;
                        skb->data[13] = 0x00;
                        skb->data[14] = (def_vid >> 8) & 0xf;
                        skb->data[15] = def_vid & 0xff;
                    }
                }
                else
#endif                
                    skb_pull(skb, 2); /* remove attansic header */

    		dma_cache_sync(NULL, (void *)skb->data,  skb->len, DMA_FROM_DEVICE);
                mac->net_rx_packets ++;
                mac->net_rx_bytes += skb->len;
#if 0//def CONFIG_CAMEO_REALTEK_PHY
		/* align the data to the ip header - should be faster than copying the entire packet */
		for (i = len - (len % 4); i >= 0; i -= 4) {
			put_unaligned(*((u32 *) (skb->data + i)), (u32 *) (skb->data + i + 2));
		}
		skb->data += 2;
		skb->tail += 2;
#endif

                /*
                * also pulls the ether header
                */
                skb->protocol       = eth_type_trans(skb, dev);
                skb->dev            = dev;
                bp->buf_pkt         = NULL;
                dev->last_rx        = jiffies;
                quota--;

                netif_receive_skb(skb);
            }
            else
            {
    		dma_cache_sync(NULL, (void *)skb->data,  skb->len, DMA_FROM_DEVICE);
                mac->net_rx_packets ++;
                mac->net_rx_bytes += skb->len;
                bp->buf_pkt         = NULL;
                dev->last_rx        = jiffies;
                quota--;

                if (type == READ_WRITE_REG_ACK)
                {
                    header_receive_skb(skb);
                }
                else
                {
                    kfree_skb(skb);
                }
            }
        }else
        {
    	    dma_cache_sync(NULL, (void *)skb->data,  skb->len, DMA_FROM_DEVICE);
            mac->net_rx_packets ++;
            mac->net_rx_bytes += skb->len;
            /*
            * also pulls the ether header
            */
            skb->protocol       = eth_type_trans(skb, dev);
            skb->dev            = dev;
            bp->buf_pkt         = NULL;
            dev->last_rx        = jiffies;
            quota--;

            netif_receive_skb(skb);
        }

#else
    	dma_cache_sync(NULL, (void *)skb->data,  skb->len, DMA_FROM_DEVICE);
        mac->net_rx_packets ++;
        mac->net_rx_bytes += skb->len;
        /*
        * also pulls the ether header
        */
        skb->dev            = dev;
        bp->buf_pkt         = NULL;
        dev->last_rx        = jiffies;

        work_done[0]++;
        quota--;

#if defined(CONFIG_PHY_LAYER)
	if (mac->rx)
	    mac->rx(skb);
	else
#endif
	    {
    	    skb->protocol       = eth_type_trans(skb, dev);
            netif_receive_skb(skb);
            }
#endif

        ag7100_ring_incr(head);
    }

#if 0
    if(quota == iquota)
    {
        *work_done = quota = 0;
        return AG7100_RX_DMA_HANG;
    }
#endif
    r->ring_head   =  head;
    rep = ag7100_rx_replenish(mac);
#if 0
    if(rep < 0)
    {
        *work_done =0 ;
        return AG7100_RX_DMA_HANG;
    }
#endif
    /*
    * let's see what changed while we were slogging.
    * ack Rx in the loop above is no flush version. It will get flushed now.
    */
    status       =  ag7100_reg_rd(mac, AG7100_DMA_RX_STATUS);
    more_pkts    =  (status & AG7100_RX_STATUS_PKT_RCVD);

    ag7100_trc(more_pkts,"more_pkts");

    if (!more_pkts) goto done;
    /*
    * more pkts arrived; if we have quota left, get rolling again
    */
//    if (quota)      goto process_pkts;
    /*
    * out of quota
    */
    ret = AG7100_RX_STATUS_NOT_DONE;

done:
//    *work_done   = (iquota - quota);

    if (unlikely(ag7100_rx_ring_full(mac))) 
        return AG7100_RX_STATUS_OOM;
    /*
    * !oom; if stopped, restart h/w
    */

    if (unlikely(status & AG7100_RX_STATUS_OVF))
    {
        mac->net_rx_over_errors ++;
        ag7100_intr_ack_rxovf(mac);
        ag7100_rx_start(mac);
    }

    return ret;
}

static struct sk_buff *
    ag7100_buffer_alloc(void)
{
    struct sk_buff *skb;

#if 0//def CONFIG_CAMEO_REALTEK_PHY
    skb = dev_alloc_skb(AG7100_RX_BUF_SIZE+4);
#else
    skb = dev_alloc_skb(AG7100_RX_BUF_SIZE + AG7100_RX_RESERVE);
#endif
    if (unlikely(!skb))
        return NULL;
    skb_reserve(skb, AG7100_RX_RESERVE);

    return skb;
}

static void
ag7100_buffer_free(struct sk_buff *skb)
{
    if (in_irq())
        dev_kfree_skb_irq(skb);
    else
        dev_kfree_skb(skb);
}

/*
 * Head is the first slot with a valid buffer. Tail is the last slot 
 * replenished. Tries to refill buffers from tail to head.
 */
static int
ag7100_rx_replenish(ag7100_mac_t *mac)
{
    ag7100_ring_t   *r     = &mac->mac_rxring;
    int              head  = r->ring_head, tail = r->ring_tail, refilled = 0;
    ag7100_desc_t   *ds;
    ag7100_buffer_t *bf;

    ag7100_trc(head,"hd");
    ag7100_trc(tail,"tl");

    do
    {
        bf                  = &r->ring_buffer[tail];
        ds                  = &r->ring_desc[tail];

        ag7100_trc(ds,"ds");

        if(ag7100_rx_owned_by_dma(ds))
        {
            return -1;
        }
        assert(!bf->buf_pkt);

        bf->buf_pkt         = ag7100_buffer_alloc();
        if (!bf->buf_pkt)
        {
            printk(MODULE_NAME ": outta skbs!\n");
            break;
        }
        dma_cache_sync(NULL, (void *)bf->buf_pkt->data, AG7100_RX_BUF_SIZE, DMA_FROM_DEVICE);
        ds->pkt_start_addr  = virt_to_phys(bf->buf_pkt->data);

        ag7100_rx_give_to_dma(ds);
        refilled ++;

        ag7100_ring_incr(tail);

    } while(tail != head);
    /*
    * Flush descriptors
    */
    wmb();

    r->ring_tail = tail;
    ag7100_trc(refilled,"refilled");

    return refilled;
}

/* 
 * Reap from tail till the head or whenever we encounter an unxmited packet.
 */
static int
ag7100_tx_reap(ag7100_mac_t *mac)
{
    ag7100_ring_t   *r     = &mac->mac_txring;
    int              head  = r->ring_head, tail = r->ring_tail, reaped = 0, i;
    ag7100_desc_t   *ds;
    ag7100_buffer_t *bf;
    uint32_t    flags;

    ag7100_trc_new(head,"hd");
    ag7100_trc_new(tail,"tl");

    ar7100_flush_ge(mac->mac_unit);
    spin_lock_irqsave(&mac->mac_lock, flags);
    while(tail != head)
    {
        ds   = &r->ring_desc[tail];

        ag7100_trc_new(ds,"ds");

        if(ag7100_tx_owned_by_dma(ds))
            break;

        bf      = &r->ring_buffer[tail];
        assert(bf->buf_pkt);

        ag7100_trc_new(bf->buf_lastds,"lastds");

        if(ag7100_tx_owned_by_dma(bf->buf_lastds))
            break;

        for(i = 0; i < bf->buf_nds; i++)
        {
            ag7100_intr_ack_tx(mac);
            ag7100_ring_incr(tail);
        }

        ag7100_buffer_free(bf->buf_pkt);
        bf->buf_pkt = NULL;

        reaped ++;
    }
    spin_unlock_irqrestore(&mac->mac_lock, flags);

    r->ring_tail = tail;

    if (netif_queue_stopped(mac->mac_dev) &&
        (ag7100_ndesc_unused(mac, r) >= AG7100_TX_QSTART_THRESH) &&
        netif_carrier_ok(mac->mac_dev))
    {
        if (ag7100_reg_rd(mac, AG7100_DMA_INTR_MASK) & AG7100_INTR_TX)
        {
            spin_lock_irqsave(&mac->mac_lock, flags);
            ag7100_intr_disable_tx(mac);
            spin_unlock_irqrestore(&mac->mac_lock, flags);
        }
        netif_wake_queue(mac->mac_dev);
    }

    return reaped;
}

/*
 * allocate and init rings, descriptors etc.
 */
static int
ag7100_tx_alloc(ag7100_mac_t *mac)
{
    ag7100_ring_t *r = &mac->mac_txring;
    ag7100_desc_t *ds;
    int i, next;

    if (ag7100_ring_alloc(r, AG7100_TX_DESC_CNT))
        return 1;

    ag7100_trc(r->ring_desc,"ring_desc");

    ds = r->ring_desc;
    for(i = 0; i < r->ring_nelem; i++ )
    {
        ag7100_trc_new(ds,"tx alloc ds");
        next                =   (i == (r->ring_nelem - 1)) ? 0 : (i + 1);
        ds[i].next_desc     =   ag7100_desc_dma_addr(r, &ds[next]);
        ag7100_tx_own(&ds[i]);
    }

    return 0;
}

static int
ag7100_rx_alloc(ag7100_mac_t *mac)
{
    ag7100_ring_t *r  = &mac->mac_rxring;
    ag7100_desc_t *ds;
    int i, next, tail = r->ring_tail;
    ag7100_buffer_t *bf;

    if (ag7100_ring_alloc(r, AG7100_RX_DESC_CNT))
        return 1;

    ag7100_trc(r->ring_desc,"ring_desc");

    ds = r->ring_desc;
    for(i = 0; i < r->ring_nelem; i++ )
    {
        next                =   (i == (r->ring_nelem - 1)) ? 0 : (i + 1);
        ds[i].next_desc     =   ag7100_desc_dma_addr(r, &ds[next]);
    }

    for (i = 0; i < AG7100_RX_DESC_CNT; i++)
    {
        bf                  = &r->ring_buffer[tail];
        ds                  = &r->ring_desc[tail];

        bf->buf_pkt         = ag7100_buffer_alloc();
        if (!bf->buf_pkt) 
            goto error;

        dma_cache_sync(NULL, (void *)bf->buf_pkt->data, AG7100_RX_BUF_SIZE, DMA_FROM_DEVICE);
        ds->pkt_start_addr  = virt_to_phys(bf->buf_pkt->data);

        ag7100_rx_give_to_dma(ds);
        ag7100_ring_incr(tail);
    }

    return 0;
error:
    printk(MODULE_NAME ": unable to allocate rx\n");
    ag7100_rx_free(mac);
    return 1;
}

static void
ag7100_tx_free(ag7100_mac_t *mac)
{
    ag7100_ring_release(mac, &mac->mac_txring);
    ag7100_ring_free(&mac->mac_txring);
}

static void
ag7100_rx_free(ag7100_mac_t *mac)
{
    ag7100_ring_release(mac, &mac->mac_rxring);
    ag7100_ring_free(&mac->mac_rxring);
}

static int
ag7100_ring_alloc(ag7100_ring_t *r, int count)
{
    int desc_alloc_size, buf_alloc_size;

    desc_alloc_size = sizeof(ag7100_desc_t)   * count;
    buf_alloc_size  = sizeof(ag7100_buffer_t) * count;

    memset(r, 0, sizeof(ag7100_ring_t));

    r->ring_buffer = (ag7100_buffer_t *)kmalloc(buf_alloc_size, GFP_KERNEL);
//    printk("%s Allocated %d at 0x%lx\n",__func__,buf_alloc_size,(unsigned long) r->ring_buffer);
    if (!r->ring_buffer)
    {
        printk(MODULE_NAME ": unable to allocate buffers\n");
        return 1;
    }

    r->ring_desc  =  (ag7100_desc_t *)dma_alloc_coherent(NULL, 
        desc_alloc_size,
        &r->ring_desc_dma, 
        GFP_DMA);
    if (! r->ring_desc)
    {
//        printk(MODULE_NAME ": unable to allocate coherent descs\n");
        kfree(r->ring_buffer);
///        printk("%s Freeing at 0x%lx\n",__func__,(unsigned long) r->ring_buffer);
        return 1;
    }

    memset(r->ring_buffer, 0, buf_alloc_size);
    memset(r->ring_desc,   0, desc_alloc_size);
    r->ring_nelem   = count;

    return 0;
}

static void
ag7100_ring_release(ag7100_mac_t *mac, ag7100_ring_t  *r)
{
    int i;

    for(i = 0; i < r->ring_nelem; i++)
        if (r->ring_buffer[i].buf_pkt)
            ag7100_buffer_free(r->ring_buffer[i].buf_pkt);
}

static void
ag7100_ring_free(ag7100_ring_t *r)
{
    dma_free_coherent(NULL, sizeof(ag7100_desc_t)*r->ring_nelem, r->ring_desc,
        r->ring_desc_dma);
    kfree(r->ring_buffer);
    printk("%s Freeing at 0x%lx\n",__func__,(unsigned long) r->ring_buffer);
}

/*
 * Error timers
 */
static void
ag7100_oom_timer(unsigned long data)
{
    ag7100_mac_t *mac = (ag7100_mac_t *)data;
    int val;

    ag7100_trc(data,"data");
//    ag7100_rx_replenish(mac);
    if (ag7100_rx_ring_full(mac))
    {
        val = mod_timer(&mac->mac_oom_timer, jiffies+1);
        assert(!val);
    }
    else
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24)
	napi_schedule(&mac->mac_napi);
#else
	netif_rx_schedule(mac->mac_dev);
#endif
}

static void
ag7100_tx_timeout(struct net_device *dev)
{
    ag7100_mac_t *mac = (ag7100_mac_t *)netdev_priv(dev);
    ag7100_trc(dev,"dev");
    printk("%s\n",__func__);
    /* 
    * Do the reset outside of interrupt context 
    */
    schedule_work(&mac->mac_tx_timeout);
}


static void
ag7100_tx_timeout_task(struct work_struct *work)
{
    ag7100_mac_t *mac = container_of(work, ag7100_mac_t, mac_tx_timeout);
    ag7100_trc(mac,"mac");
    ag7100_stop(mac->mac_dev);
    ag7100_open(mac->mac_dev);
}

static void
ag7100_get_default_macaddr(ag7100_mac_t *mac, u8 *mac_addr)
{
    /*
    ** Use MAC address stored in Flash.  If CONFIG_AG7100_MAC_LOCATION is defined,
    ** it provides the physical address of where the MAC addresses are located.
    ** This can be a board specific location, so it's best to be part of the
    ** defconfig for that board.
    **
    ** The default locations assume the last sector in flash.
    */
    
#ifdef CONFIG_AG7100_MAC_LOCATION
    u8 *eep_mac_addr = (u8 *)( CONFIG_AG7100_MAC_LOCATION + (mac->mac_unit)*6);
#else
    u8 *eep_mac_addr = (mac->mac_unit) ? AR7100_EEPROM_GE1_MAC_ADDR:
        AR7100_EEPROM_GE0_MAC_ADDR;
#endif

//    printk(MODULE_NAME "CHH: Mac address for unit %d\n",mac->mac_unit);
//    printk(MODULE_NAME "CHH: %02x:%02x:%02x:%02x:%02x:%02x \n",
//        eep_mac_addr[0],eep_mac_addr[1],eep_mac_addr[2],
//        eep_mac_addr[3],eep_mac_addr[4],eep_mac_addr[5]);    
    memcpy(mac_addr,eep_mac_addr,6);    
}

static int
ag7100_do_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
#if !defined(CONFIG_ATHRS26_PHY) && !defined(CONFIG_ATHRS16_PHY)
    printk(MODULE_NAME ": unsupported ioctl\n");
    return -EOPNOTSUPP;
#else
    return athr_ioctl(ifr->ifr_data, cmd);
#endif
}

static struct net_device_stats 
    *ag7100_get_stats(struct net_device *dev)
{
    ag7100_mac_t *mac = netdev_priv(dev);
    int i;

//    sch = rcu_dereference(dev->qdisc);
//    mac->mac_net_stats.tx_dropped = sch->qstats.drops;

    i = ag7100_get_rx_count(mac) - mac->net_rx_packets;
    if (i<0)
        i=0;

    mac->mac_net_stats.rx_missed_errors = i;

    return &mac->mac_net_stats;
}

static void
ag7100_vet_tx_len_per_pkt(unsigned int *len)
{
    unsigned int l;

    /* make it into words */
    l = *len & ~3;

    /* 
    * Not too small 
    */
    if (l < AG7100_TX_MIN_DS_LEN)
        l = AG7100_TX_MIN_DS_LEN;
    else
    /* Avoid len where we know we will deadlock, that
    * is the range between fif_len/2 and the MTU size
    */
    if (l > (AG7100_TX_FIFO_LEN/2))
    {
        if (l < AG7100_TX_MTU_LEN)
    	    {
            l = AG7100_TX_MTU_LEN;
    	    }
        else if (l > AG7100_TX_MAX_DS_LEN)
            {
            l = AG7100_TX_MAX_DS_LEN;
            }
	*len = l;
    }
}

#ifdef CONFIG_NET_DSA_MV88E6060

#include "dev-dsa.h"

static struct dsa_chip_data tl_wr941nd_dsa_chip = {
	.port_names[0]  = "wan",
	.port_names[1]  = "lan1",
	.port_names[2]  = "lan2",
	.port_names[3]  = "lan3",
	.port_names[4]  = "lan4",
	.port_names[5]  = "cpu",
};

static struct dsa_platform_data tl_wr941nd_dsa_data = {
	.nr_chips	= 1,
	.chip		= &tl_wr941nd_dsa_chip,
};
#endif

static struct net_device_ops mac_net_ops;


static int ag7100_change_mtu(struct net_device *dev, int new_mtu)
{
	if (new_mtu<=(AG71XX_TX_MTU_LEN-18))
	    dev->mtu = new_mtu;
	return 0;
}

/*
 * All allocations (except irq and rings).
 */
static int __init
ag7100_init(void)
{
    int i;
    struct net_device *dev;
    ag7100_mac_t      *mac;
    uint32_t mask;

    /* 
    * tx_len_per_ds is the number of bytes per data transfer in word increments.
    * 
    * If the value is 0 than we default the value to a known good value based
    * on benchmarks. Otherwise we use the value specified - within some 
    * cosntraints of course.
    *
    * Tested working values are 256, 512, 768, 1024 & 1536.
    *
    * A value of 256 worked best in all benchmarks. That is the default.
    *
    */

    /* Tested 256, 512, 768, 1024, 1536 OK, 1152 and 1280 failed*/
    if (0 == tx_len_per_ds)
        tx_len_per_ds = CONFIG_AG7100_LEN_PER_TX_DS;

    ag7100_vet_tx_len_per_pkt( &tx_len_per_ds);

//    printk(MODULE_NAME ": Length per segment %d\n", tx_len_per_ds);

    /* 
    * Compute the number of descriptors for an MTU 
    */
#ifndef CONFIG_AR9100
    tx_max_desc_per_ds_pkt = AG7100_TX_MAX_DS_LEN / tx_len_per_ds;
    if (AG7100_TX_MAX_DS_LEN % tx_len_per_ds) tx_max_desc_per_ds_pkt++;
#else
    tx_max_desc_per_ds_pkt =1;
#endif

//    printk(MODULE_NAME ": Max segments per packet %d\n", tx_max_desc_per_ds_pkt);
//    printk(MODULE_NAME ": Max tx descriptor count    %d\n", AG7100_TX_DESC_CNT);
//    printk(MODULE_NAME ": Max rx descriptor count    %d\n", AG7100_RX_DESC_CNT);

    /* 
    * Let hydra know how much to put into the fifo in words (for tx) 
    */
    if (0 == fifo_3)
        fifo_3 = 0x000001ff | ((AG7100_TX_FIFO_LEN-tx_len_per_ds)/4)<<16;

//    printk(MODULE_NAME ": fifo cfg 3 %08x\n", fifo_3);

    /* 
    ** Do the rest of the initializations 
    */

    for(i = 0; i < AG7100_NMACS; i++)
    {
        dev = alloc_etherdev(sizeof(ag7100_mac_t));
        mac =  (ag7100_mac_t *)netdev_priv(dev);
        if (!mac)
        {
            printk(MODULE_NAME ": unable to allocate mac\n");
            return 1;
        }
        memset(mac, 0, sizeof(ag7100_mac_t));

        mac->mac_unit               =  i;
        mac->mac_base               =  ag7100_mac_base(i);
        mac->mac_irq                =  ag7100_mac_irq(i);
        ag7100_macs[i]              =  mac;
        spin_lock_init(&mac->mac_lock);
        /*
        * out of memory timer
        */
        init_timer(&mac->mac_oom_timer);
        mac->mac_oom_timer.data     = (unsigned long)mac;
        mac->mac_oom_timer.function = ag7100_oom_timer;
        /*
        * watchdog task
        */
        INIT_WORK(&mac->mac_tx_timeout, ag7100_tx_timeout_task);

        if (!dev)
        {
            kfree(mac);
//            printk("%s Freeing at 0x%lx\n",__func__,(unsigned long) mac);
            printk(MODULE_NAME ": unable to allocate etherdev\n");
            return 1;
        }

        mac_net_ops.ndo_open      = ag7100_open;
        mac_net_ops.ndo_stop      = ag7100_stop;
        mac_net_ops.ndo_start_xmit= ag7100_hard_start;
        mac_net_ops.ndo_get_stats = ag7100_get_stats;
        mac_net_ops.ndo_tx_timeout= ag7100_tx_timeout;
#if defined(CONFIG_ATHRS26_PHY) || defined(CONFIG_ATHRS16_PHY) 
        mac_net_ops.ndo_do_ioctl        =  ag7100_do_ioctl;
#else
        mac_net_ops.ndo_do_ioctl        =  NULL;
#endif
	mac_net_ops.ndo_change_mtu		= ag7100_change_mtu;
	mac_net_ops.ndo_set_mac_address	= eth_mac_addr;
	mac_net_ops.ndo_validate_addr	= eth_validate_addr;
        dev->netdev_ops = (const struct net_device_ops *)&mac_net_ops;             
	netif_napi_add(dev, &mac->mac_napi, ag7100_poll, AG7100_NAPI_WEIGHT);

        mac->mac_dev         =  dev;        
#ifdef CONFIG_BUFFALO   // { append by BUFFALO 2008.09.19

#ifdef CONFIG_TPLINK
	switch(rtl_chip_type_select())
	{
		case CHIP_TYPE_RTL8366SR:
			rtl_funcs.phy_setup = rtl8366sr_phy_setup;
			rtl_funcs.phy_is_up = rtl8366sr_phy_is_up;
			rtl_funcs.phy_speed = rtl8366sr_phy_speed;
			rtl_funcs.phy_is_fdx = rtl8366sr_phy_is_fdx;
			rtl_funcs.get_link_status = rtl8366sr_get_link_status;
			e1000_pll = e1000sr_pll;
			e100_pll = e100sr_pll;
			e10_pll = e10sr_pll;
			break;
		case CHIP_TYPE_RTL8366RB:
		default:
			rtl_funcs.phy_setup = rtl8366rb_phy_setup;
			rtl_funcs.phy_is_up = rtl8366rb_phy_is_up;
			rtl_funcs.phy_speed = rtl8366rb_phy_speed;
			rtl_funcs.phy_is_fdx = rtl8366rb_phy_is_fdx;
			rtl_funcs.get_link_status = rtl8366rb_get_link_status;
			e1000_pll = e1000rb_pll;
			e100_pll = e100rb_pll;
			e10_pll = e10rb_pll;
			break;
	}
#else
	switch(rtl_chip_type_select())
	{
		case CHIP_TYPE_RTL8366RB:
			rtl_funcs.phy_setup = rtl8366rb_phy_setup;
			rtl_funcs.phy_is_up = rtl8366rb_phy_is_up;
			rtl_funcs.phy_speed = rtl8366rb_phy_speed;
			rtl_funcs.phy_is_fdx = rtl8366rb_phy_is_fdx;
			rtl_funcs.get_link_status = rtl8366rb_get_link_status;
			e1000_pll = e1000rb_pll;
			e100_pll = e100rb_pll;
			e10_pll = e10rb_pll;
			break;
		case CHIP_TYPE_RTL8366SR:
		default:
			rtl_funcs.phy_setup = rtl8366sr_phy_setup;
			rtl_funcs.phy_is_up = rtl8366sr_phy_is_up;
			rtl_funcs.phy_speed = rtl8366sr_phy_speed;
			rtl_funcs.phy_is_fdx = rtl8366sr_phy_is_fdx;
			rtl_funcs.get_link_status = rtl8366sr_get_link_status;
			e1000_pll = e1000sr_pll;
			e100_pll = e100sr_pll;
			e10_pll = e10sr_pll;
			break;
	}


#endif
#else //CONFIG_BUFFALO //
#ifdef CONFIG_CAMEO_REALTEK_PHY
    rtl_chip_type_select();
#endif
#endif
        ag7100_get_default_macaddr(mac, dev->dev_addr);

        if (register_netdev(dev))
        {
            printk(MODULE_NAME ": register netdev failed\n");
            goto failed;
        }

#ifdef CONFIG_NET_DSA_MV88E6060
	if (i==0)
		{
    		struct mii_bus *bus = ag7100_mdiobus_setup(i,dev);
		ar71xx_add_device_dsa(0, &tl_wr941nd_dsa_data,dev,bus);
		}
#elif CONFIG_PHY_LAYER
        ag7100_mdiobus_setup(i,dev);
#endif


	netif_carrier_off(dev);

#ifdef CONFIG_AR9100
        ag7100_reg_rmw_set(mac, AG7100_MAC_CFG1, AG7100_MAC_CFG1_SOFT_RST 
				| AG7100_MAC_CFG1_RX_RST | AG7100_MAC_CFG1_TX_RST);
#else
        ag7100_reg_rmw_set(mac, AG7100_MAC_CFG1, AG7100_MAC_CFG1_SOFT_RST);
#endif
        udelay(20);
        mask = ag7100_reset_mask(mac->mac_unit);

        /*
        * put into reset, hold, pull out.
        */
        ar7100_reg_rmw_set(AR7100_RESET, mask);
        mdelay(100);
        ar7100_reg_rmw_clear(AR7100_RESET, mask);
        mdelay(100);



    }
    ag7100_trc_init();

#ifdef CONFIG_AR9100
#define AP83_BOARD_NUM_ADDR ((char *)0xbf7f1244)

	board_version = (AP83_BOARD_NUM_ADDR[0] - '0') +
			((AP83_BOARD_NUM_ADDR[1] - '0') * 10);
#endif

#if defined(CONFIG_ATHRS26_PHY)
    athrs26_reg_dev(ag7100_macs);
#endif

    return 0;

failed:
    for(i = 0; i < AG7100_NMACS; i++)
    {
        if (!ag7100_macs[i]) 
            continue;
        if (ag7100_macs[i]->mac_dev) 
            free_netdev(ag7100_macs[i]->mac_dev);
        kfree(ag7100_macs[i]);
//        printk("%s Freeing at 0x%lx\n",__func__,(unsigned long) ag7100_macs[i]);
    }
    return 1;
}

static void __exit
ag7100_cleanup(void)
{
    int i;

    for(i = 0; i < AG7100_NMACS; i++)
    {
        unregister_netdev(ag7100_macs[i]->mac_dev);
        free_netdev(ag7100_macs[i]->mac_dev);
        kfree(ag7100_macs[i]);
//        printk("%s Freeing at 0x%lx\n",__func__,(unsigned long) ag7100_macs[i]);
    }
//    printk(MODULE_NAME ": cleanup done\n");
}

module_init(ag7100_init);
module_exit(ag7100_cleanup);
