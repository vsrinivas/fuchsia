// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_ETHERNET_DRIVERS_DWMAC_DW_GMAC_DMA_H_
#define SRC_CONNECTIVITY_ETHERNET_DRIVERS_DWMAC_DW_GMAC_DMA_H_

// clang-format off

#define CONFIG_DW_GMAC_DEFAULT_DMA_PBL 8

/* Bus mode register definitions */
#define X8PBL              (1 << 24)
#define FIXEDBURST         (1 << 16)
#define PRIORXTX_41        (3 << 14)
#define PRIORXTX_31        (2 << 14)
#define PRIORXTX_21        (1 << 14)
#define PRIORXTX_11        (0 << 14)
#define DMA_PBL            (CONFIG_DW_GMAC_DEFAULT_DMA_PBL << 8)
#define RXHIGHPRIO         (1 << 1)
#define DMAMAC_SRST        (1 << 0)

#define DMA_OPMODE_RSF       (1 << 25)
#define DMA_OPMODE_TSF       (1 << 21)
#define DMA_OPMODE_FTF       (1 << 20)
#define DMA_OPMODE_ST        (1 << 13)
#define DMA_OPMODE_EFC       (1 << 8)
#define DMA_OPMODE_FEF       (1 << 7)
#define DMA_OPMODE_SR        (1 << 1)

#define DMA_OPMODE_RFD(val)  (((val & 0x3) << 11) | ((val & 0x4) << 20))
#define DMA_OPMODE_RFA(val)  (((val & 0x3) <<  9) | ((val & 0x4) << 21))
#define DMA_OPMODE_TTC(val)   ((val & 0x7) << 14)
#define DMA_OPMODE_RTC(val)   ((val & 0x3) <<  3)


#define DESC_TXSTS_OWNBYDMA            (1 << 31)
#define DESC_TXSTS_MSK                 (0x1FFFF << 0)

/* rx status bits definitions */
#define DESC_RXSTS_OWNBYDMA            (1 << 31)
#define DESC_RXSTS_DAFILTERFAIL        (1 << 30)
#define DESC_RXSTS_FRMLENMSK           (0x3FFF << 16)
#define DESC_RXSTS_FRMLENSHFT          (16)

#define DESC_RXSTS_ERROR               (1 << 15)
#define DESC_RXSTS_RXTRUNCATED         (1 << 14)
#define DESC_RXSTS_SAFILTERFAIL        (1 << 13)
#define DESC_RXSTS_RXIPC_GIANTFRAME    (1 << 12)
#define DESC_RXSTS_RXDAMAGED           (1 << 11)
#define DESC_RXSTS_RXVLANTAG           (1 << 10)
#define DESC_RXSTS_RXFIRST             (1 <<  9)
#define DESC_RXSTS_RXLAST              (1 <<  8)
#define DESC_RXSTS_RXIPC_GIANT         (1 <<  7)
#define DESC_RXSTS_RXCOLLISION         (1 <<  6)
#define DESC_RXSTS_RXFRAMEETHER        (1 <<  5)
#define DESC_RXSTS_RXWATCHDOG          (1 <<  4)
#define DESC_RXSTS_RXMIIERROR          (1 <<  3)
#define DESC_RXSTS_RXDRIBBLING         (1 <<  2)
#define DESC_RXSTS_RXCRC               (1 <<  1)

/* tx control bits definitions */
#define DESC_TXCTRL_TXINT              (1 << 31)
#define DESC_TXCTRL_TXLAST             (1 << 30)
#define DESC_TXCTRL_TXFIRST            (1 << 29)
#define DESC_TXCTRL_TXCHECKINSCTRL     (2 << 27)
#define DESC_TXCTRL_TXCRCDIS           (1 << 26)
#define DESC_TXCTRL_TXRINGEND          (1 << 25)
#define DESC_TXCTRL_TXCHAIN            (1 << 24)

#define DESC_TXCTRL_SIZE1MASK          (0x7FF << 0)
#define DESC_TXCTRL_SIZE1SHFT          (0)
#define DESC_TXCTRL_SIZE2MASK          (0x7FF << 11)
#define DESC_TXCTRL_SIZE2SHFT          (11)

/* rx control bits definitions */
#define DESC_RXCTRL_RXINTDIS           (1 << 31)
#define DESC_RXCTRL_RXRINGEND          (1 << 25)
#define DESC_RXCTRL_RXCHAIN            (1 << 24)

#define DESC_RXCTRL_SIZE1MASK          (0x7FF << 0)
#define DESC_RXCTRL_SIZE1SHFT          (0)
#define DESC_RXCTRL_SIZE2MASK          (0x7FF << 11)
#define DESC_RXCTRL_SIZE2SHFT          (11)

/*DMA interrupt enable bits */
#define DMA_INT_NIE                    (1 << 16)  /* Normal Interrupt Enable */
#define DMA_INT_AIE                    (1 << 15)  /* Abnormal Interrupt Enable */
#define DMA_INT_ERE                    (1 << 14)  /* Early Rx */
#define DMA_INT_FBE                    (1 << 13)  /* Fatal Bus Error */
#define DMA_INT_ETE                    (1 << 10)  /* Early Tx */
#define DMA_INT_RWE                    (1 <<  9)  /* Rx Watchdog */
#define DMA_INT_RSE                    (1 <<  8)  /* Rx Stopped */
#define DMA_INT_RUE                    (1 <<  7)  /* Rx Buffer Unavailable */
#define DMA_INT_RIE                    (1 <<  6)
#define DMA_INT_UNE                    (1 <<  5)
#define DMA_INT_OVE                    (1 <<  4)
#define DMA_INT_TJE                    (1 <<  3)
#define DMA_INT_TUE                    (1 <<  2)
#define DMA_INT_TSE                    (1 <<  1)
#define DMA_INT_TIE                    (1 <<  0)


#define DMA_STATUS_GLI                (1 << 26)  /*GMAC Line interface interrupt*/
#define DMA_STATUS_AIS                (1 << 15)  /*GMAC Abnormal activity*/
#define DMA_STATUS_RI                 (1 <<  6)  /*GMAC Rx Complete*/
#define DMA_STATUS_TI                 (1 <<  0)  /*Tx Complete */
#define DMA_STATUS_RS_POS             (17)
#define DMA_STATUS_RS_MASK            (0x7 << DMA_STATUS_RS_POS)

#define DMA_RS_STATUS_STOPPED          (0)
#define DMA_RS_STATUS_FETCHING_DESC    (1)
#define DMA_RS_STATUS_WAITING          (3)
#define DMA_RS_STATUS_SUSPENDED        (4)
#define DMA_RS_STATUS_CLOSING_DESC     (5)
#define DMA_RS_STATUS_TRANSFERRING     (7)

#define GMAC_RGMII_STATUS_LNKSTS       (1 << 3)

/* GMAC Configuration defines */
#define GMAC_CONF_2K 0x08000000  /* IEEE 802.3as 2K packets */
#define GMAC_CONF_TC 0x01000000  /* Transmit Conf. in RGMII/SGMII */
#define GMAC_CONF_WD 0x00800000  /* Disable Watchdog on receive */
#define GMAC_CONF_JD 0x00400000  /* Jabber disable */
#define GMAC_CONF_BE 0x00200000  /* Frame Burst Enable */
#define GMAC_CONF_JE 0x00100000  /* Jumbo frame */
enum inter_frame_gap {
    GMAC_CONF_IFG_88 = 0x00040000,
    GMAC_CONF_IFG_80 = 0x00020000,
    GMAC_CONF_IFG_40 = 0x000e0000,
};
#define GMAC_CONF_DCRS   0x00010000  /* Disable carrier sense */
#define GMAC_CONF_PS     0x00008000  /* Port Select 0:GMI 1:MII */
#define GMAC_CONF_FES    0x00004000  /* Speed 0:10 1:100 */
#define GMAC_CONF_DO     0x00002000  /* Disable Rx Own */
#define GMAC_CONF_LM     0x00001000  /* Loop-back mode */
#define GMAC_CONF_DM     0x00000800  /* Duplex Mode */
#define GMAC_CONF_IPC    0x00000400  /* Checksum Offload */
#define GMAC_CONF_DR     0x00000200  /* Disable Retry */
#define GMAC_CONF_LUD    0x00000100  /* Link up/down */
#define GMAC_CONF_ACS    0x00000080  /* Auto Pad/FCS Stripping */
#define GMAC_CONF_DC     0x00000010  /* Deferral Check */
#define GMAC_CONF_TE     0x00000008  /* Transmitter Enable */
#define GMAC_CONF_RE     0x00000004  /* Receiver Enable */

#define GMAC_CORE_INIT (GMAC_CONF_JD | \
            GMAC_CONF_DM)


#define GMAC_FLOW_TFE    (1 <<  1)
#define GMAC_FLOW_RFE    (1 <<  2)
#define GMAC_FLOW_UP     (1 <<  3)



//ETH_REG0 definitions
//        (ending in _POS indicates bit position)
#define ETH_REG0_RGMII_SEL      (1 << 0)
#define ETH_REG0_DATA_ENDIAN    (1 << 1)
#define ETH_REG0_DESC_ENDIAN    (1 << 2)
#define ETH_REG0_RX_CLK_INV     (1 << 3)
#define ETH_REG0_TX_CLK_SRC     (1 << 4)
#define ETH_REG0_TX_CLK_PH_POS       (5)
#define ETH_REG0_TX_CLK_RATIO_POS    (7)
#define ETH_REG0_REF_CLK_ENA    (1 << 10)
#define ETH_REG0_RMII_INV       (1 << 11)
#define ETH_REG0_CLK_ENA        (1 << 12)
#define ETH_REG0_ADJ_ENA        (1 << 13)
#define ETH_REG0_ADJ_SETUP      (1 << 14)
#define ETH_REG0_ADJ_DELAY_POS  (15)
#define ETH_REG0_ADJ_SKEW_POS   (20)
#define ETH_REG0_CALI_START     (1 << 25)
#define ETH_REG0_CALI_RISE      (1 << 26)
#define ETH_REG0_CALI_SEL_POS   (27)
#define ETH_REG0_RX_REUSE       (1 << 30)
#define ETH_REG0_URGENT         (1 << 31)

// miiaddr register definitions
#define MII_BUSY                 (1 << 0)
#define MII_WRITE                (1 << 1)
#define MII_CLKRANGE_60_100M     (0)
#define MII_CLKRANGE_100_150M    (0x4)
#define MII_CLKRANGE_20_35M      (0x8)
#define MII_CLKRANGE_35_60M      (0xC)
#define MII_CLKRANGE_150_250M    (0x10)
#define MII_CLKRANGE_250_300M    (0x14)

#define MIIADDRSHIFT        (11)
#define MIIREGSHIFT         (6)
#define MII_REGMSK          (0x1F << 6)
#define MII_ADDRMSK         (0x1F << 11)

#define MAC_MAX_FRAME_SZ    (1600)


#define MII_BMCR        0x00    /* Basic mode control register */
#define MII_BMSR        0x01    /* Basic mode status register  */
#define MII_PHYSID1     0x02    /* PHYS ID 1               */
#define MII_PHYSID2     0x03    /* PHYS ID 2               */
#define MII_ADVERTISE   0x04    /* Advertisement control reg   */
#define MII_LPA         0x05    /* Link partner ability reg    */
#define MII_EXPANSION   0x06    /* Expansion register           */
#define MII_GBCR        0x09    /* 1000BASE-T control           */
#define MII_GBSR        0x0a    /* 1000BASE-T status           */
#define MII_ESTATUS     0x0f    /* Extended Status */
#define MII_DCOUNTER    0x12    /* Disconnect counter           */
#define MII_FCSCOUNTER  0x13    /* False carrier counter       */
#define MII_NWAYTEST    0x14    /* N-way auto-neg test reg     */
#define MII_RERRCOUNTER 0x15    /* Receive error counter       */
#define MII_SREVISION   0x16    /* Silicon revision           */
#define MII_RESV1       0x17    /* Reserved...               */
#define MII_LBRERROR    0x18    /* Lpback, rx, bypass error    */
#define MII_PHYADDR     0x19    /* PHY address               */
#define MII_RESV2       0x1a    /* Reserved...               */
#define MII_TPISTATUS   0x1b    /* TPI status for 10mbps       */
#define MII_NCONFIG     0x1c    /* Network interface config    */
#define MII_EPAGSR      0x1f    /* Page Select register */

/* Basic mode control register. */
#define BMCR_RESV           0x003f  /* Unused...                   */
#define BMCR_SPEED1000      0x0040  /* MSB of Speed (1000)         */
#define BMCR_CTST           0x0080  /* Collision test              */
#define BMCR_FULLDPLX       0x0100  /* Full duplex                 */
#define BMCR_ANRESTART      0x0200  /* Auto negotiation restart    */
#define BMCR_ISOLATE        0x0400  /* Isolate data paths from MII */
#define BMCR_PDOWN          0x0800  /* Enable low power state      */
#define BMCR_ANENABLE       0x1000  /* Enable auto negotiation     */
#define BMCR_SPEED100       0x2000  /* Select 100Mbps              */
#define BMCR_LOOPBACK       0x4000  /* TXD loopback bits           */
#define BMCR_RESET          0x8000  /* Reset to default state      */
#define BMCR_SPEED10        0x0000  /* Select 10Mbps               */

/* Basic mode status register. */
#define BMSR_ERCAP          0x0001  /* Ext-reg capability          */
#define BMSR_JCD            0x0002  /* Jabber detected             */
#define BMSR_LSTATUS        0x0004  /* Link status                 */
#define BMSR_ANEGCAPABLE    0x0008  /* Able to do auto-negotiation */
#define BMSR_RFAULT         0x0010  /* Remote fault detected       */
#define BMSR_ANEGCOMPLETE   0x0020  /* Auto-negotiation complete   */
#define BMSR_RESV           0x00c0  /* Unused...                   */
#define BMSR_ESTATEN        0x0100  /* Extended Status in R15      */
#define BMSR_100HALF2       0x0200  /* Can do 100BASE-T2 HDX       */
#define BMSR_100FULL2       0x0400  /* Can do 100BASE-T2 FDX       */
#define BMSR_10HALF         0x0800  /* Can do 10mbps, half-duplex  */
#define BMSR_10FULL         0x1000  /* Can do 10mbps, full-duplex  */
#define BMSR_100HALF        0x2000  /* Can do 100mbps, half-duplex */
#define BMSR_100FULL        0x4000  /* Can do 100mbps, full-duplex */
#define BMSR_100BASE4       0x8000  /* Can do 100mbps, 4k packets  */


#endif  // SRC_CONNECTIVITY_ETHERNET_DRIVERS_DWMAC_DW_GMAC_DMA_H_
