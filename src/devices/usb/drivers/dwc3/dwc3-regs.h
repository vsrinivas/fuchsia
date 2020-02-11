// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <hwreg/bitfields.h>
#include <zircon/types.h>

// Global Core Control Register
class GCTL : public hwreg::RegisterBase<GCTL, uint32_t> {
 public:
  DEF_FIELD(31, 19, PWRDNSCALE);
  DEF_BIT(18, MASTERFILTBYPASS);
  DEF_BIT(17, BYPSSETADDR);
  DEF_BIT(16, U2RSTECN);
  DEF_FIELD(15, 14, FRMSCLDWN);
  DEF_FIELD(13, 12, PRTCAPDIR);
  DEF_BIT(11, CORESOFTRESET);
  DEF_BIT(9, U1U2TimerScale);
  DEF_BIT(8, DEBUGATTACH);
  DEF_FIELD(7, 6, RAMCLKSEL);
  DEF_FIELD(5, 4, SCALEDOWN);
  DEF_BIT(3, DISSCRAMBLE);
  DEF_BIT(2, U2EXIT_LFPS);
  DEF_BIT(1, GblHibernationEn);
  DEF_BIT(0, DSBLCLKGTNG);
  static auto Get() { return hwreg::RegisterAddr<GCTL>(0xc110); }

  static constexpr uint32_t PRTCAPDIR_HOST = 1;
  static constexpr uint32_t PRTCAPDIR_DEVICE = 2;
  static constexpr uint32_t PRTCAPDIR_OTG = 3;
};

// Global Status Register
class GSTS : public hwreg::RegisterBase<GSTS, uint32_t> {
 public:
  DEF_FIELD(31, 20, CBELT);
  DEF_BIT(11, SSIC_IP);
  DEF_BIT(10, OTG_IP);
  DEF_BIT(9, BC_IP);
  DEF_BIT(8, ADP_IP);
  DEF_BIT(7, Host_IP);
  DEF_BIT(6, Device_IP);
  DEF_BIT(5, CSRTimeout);
  DEF_BIT(4, BUSERRADDRVLD);
  DEF_FIELD(1, 0, CURMOD);
  static auto Get() { return hwreg::RegisterAddr<GSTS>(0xc118); }
};

// Global USB2 PHY Configuration Register
class GUSB2PHYCFG : public hwreg::RegisterBase<GUSB2PHYCFG, uint32_t> {
 public:
  DEF_BIT(31, PHYSOFTRST);
  DEF_BIT(29, ULPI_LPM_WITH_OPMODE_CHK);
  DEF_FIELD(28, 27, HSIC_CON_WIDTH_ADJ);
  DEF_BIT(26, INV_SEL_HSIC);
  DEF_FIELD(24, 22, LSTRD);
  DEF_FIELD(21, 19, LSIPD);
  DEF_BIT(18, ULPIEXTVBUSINDICATOR);
  DEF_BIT(17, ULPIEXTVBUSDRV);
  DEF_BIT(15, ULPIAUTORES);
  DEF_FIELD(13, 10, USBTRDTIM);
  DEF_BIT(9, XCVRDLY);
  DEF_BIT(8, ENBLSLPM);
  DEF_BIT(7, PHYSEL);
  DEF_BIT(6, SUSPENDUSB20);
  DEF_BIT(5, FSINTF);
  DEF_BIT(4, ULPI_UTMI_Sel);
  DEF_BIT(3, PHYIF);
  DEF_FIELD(2, 0, TOutCal);
  static auto Get(uint32_t index) { return hwreg::RegisterAddr<GUSB2PHYCFG>(0xc118 + index * 0x4); }
};

// Global USB 3.1 PIPE Control Register
class GUSB3PIPECTL : public hwreg::RegisterBase<GUSB3PIPECTL, uint32_t> {
 public:
  DEF_BIT(31, PHYSoftRst);
  DEF_BIT(30, HstPrtCmpl);
  DEF_BIT(28, DisRxDetP3);
  DEF_BIT(27, Ux_exit_in_Px);
  DEF_BIT(26, ping_enhancement_en);
  DEF_BIT(25, u1u2exitfail_to_recov);
  DEF_BIT(24, request_p1p2p3);
  DEF_BIT(23, StartRxDetU3RxDet);
  DEF_BIT(22, DisRxDetU3RxDet);
  DEF_FIELD(21, 19, DelayP1P2P3);
  DEF_BIT(18, DELAYP1TRANS);
  DEF_BIT(17, SUSPENDENABLE);
  DEF_FIELD(16, 15, DATWIDTH);
  DEF_BIT(14, AbortRxDetInU2);
  DEF_BIT(13, SkipRxDet);
  DEF_BIT(12, LFPSP0Algn);
  DEF_BIT(11, P3P2TranOK);
  DEF_BIT(10, P3ExSigP2);
  DEF_BIT(9, LFPSFILTER);
  DEF_BIT(8, RX_DETECT_to_Polling_LFPS_Control);
  DEF_BIT(7, SSICEn);
  DEF_BIT(6, TX_SWING);
  DEF_FIELD(5, 3, TX_MARGIN);
  DEF_FIELD(2, 1, SS_TX_DE_EMPHASIS);
  DEF_BIT(0, ELASTIC_BUFFER_MODE);
  static auto Get(uint32_t index) {
    return hwreg::RegisterAddr<GUSB3PIPECTL>(0xc2c0 + index * 0x4);
  }
};

// Global Event Buffer Address Register
class GEVNTADR : public hwreg::RegisterBase<GEVNTADR, uint64_t> {
 public:
  DEF_FIELD(63, 0, EVNTADR);
  static auto Get(uint32_t index) { return hwreg::RegisterAddr<GEVNTADR>(0xc400 + index * 0x10); }
};

// Global Event Buffer Size Register
class GEVNTSIZ : public hwreg::RegisterBase<GEVNTSIZ, uint32_t> {
 public:
  DEF_BIT(31, EVNTINTRPTMASK);
  DEF_FIELD(15, 0, EVENTSIZ);
  static auto Get(uint32_t index) { return hwreg::RegisterAddr<GEVNTSIZ>(0xc408 + index * 0x10); }
};

// Global Event Buffer Count Register
class GEVNTCOUNT : public hwreg::RegisterBase<GEVNTCOUNT, uint32_t> {
 public:
  DEF_BIT(31, EVNT_HANDLER_BUSY);
  DEF_FIELD(15, 0, EVNTCOUNT);
  static auto Get(uint32_t index) { return hwreg::RegisterAddr<GEVNTCOUNT>(0xc40c + index * 0x10); }
};

// Device Configuration Register
class DCFG : public hwreg::RegisterBase<DCFG, uint32_t> {
 public:
  DEF_BIT(24, StopOnDisconnect);
  DEF_BIT(23, IgnStrmPP);
  DEF_BIT(22, LPMCAP);
  DEF_FIELD(21, 17, NUMP);
  DEF_FIELD(16, 12, INTRNUM);
  DEF_FIELD(9, 3, DEVADDR);
  DEF_FIELD(2, 0, DEVSPD);
  static auto Get() { return hwreg::RegisterAddr<DCFG>(0xc700); }

  static constexpr uint32_t DEVSPD_HIGH = 0;
  static constexpr uint32_t DEVSPD_FULL = 1;
  static constexpr uint32_t DEVSPD_LOW = 2;
  static constexpr uint32_t DEVSPD_SUPER = 4;
};

// Device Control Register
class DCTL : public hwreg::RegisterBase<DCTL, uint32_t> {
 public:
  DEF_BIT(31, RUN_STOP);
  DEF_BIT(30, CSFTRST);
  DEF_FIELD(28, 24, HIRDTHRES);
  DEF_FIELD(23, 20, LPM_NYET_thres);
  DEF_BIT(19, KeepConnect);
  DEF_BIT(18, L1HibernationEn);
  DEF_BIT(17, CRS);
  DEF_BIT(16, CSS);
  DEF_BIT(12, INITU2ENA);
  DEF_BIT(11, ACCEPTU2ENA);
  DEF_BIT(10, INITU1ENA);
  DEF_BIT(9, ACCEPTU1ENA);
  DEF_FIELD(8, 5, ULSTCHNGREQ);
  DEF_FIELD(4, 1, TSTCTL);
  static auto Get() { return hwreg::RegisterAddr<DCTL>(0xc704); }
};

// Device Event Enable Register
class DEVTEN : public hwreg::RegisterBase<DEVTEN, uint32_t> {
 public:
  DEF_BIT(15, LDMEVTEN);
  DEF_BIT(14, L1WKUPEVTEN);
  DEF_BIT(13, StopOnDisconnectEn);
  DEF_BIT(12, VENDEVTSTRCVDEN);
  DEF_BIT(9, ERRTICERREVTEN);
  DEF_BIT(8, L1SUSPEN);
  DEF_BIT(7, SOFTEVTEN);
  DEF_BIT(6, U3L2L1SuspEn);
  DEF_BIT(5, HibernationReqEvtEn);
  DEF_BIT(4, WKUPEVTEN);
  DEF_BIT(3, ULSTCNGEN);
  DEF_BIT(2, CONNECTDONEEVTEN);
  DEF_BIT(1, USBRSTEVTEN);
  DEF_BIT(0, DISSCONNEVTEN);
  static auto Get() { return hwreg::RegisterAddr<DEVTEN>(0xc708); }
};

// Device Status Register
class DSTS : public hwreg::RegisterBase<DSTS, uint32_t> {
 public:
  DEF_BIT(29, DCNRD);
  DEF_BIT(28, SRE);
  DEF_BIT(25, RSS);
  DEF_BIT(24, SSS);
  DEF_BIT(23, COREIDLE);
  DEF_BIT(22, DEVCTRLHLT);
  DEF_FIELD(21, 18, USBLNKST);
  DEF_BIT(17, RXFIFOEMPTY);
  DEF_FIELD(16, 3, SOFFN);
  DEF_FIELD(2, 0, CONNECTSPD);
  static auto Get() { return hwreg::RegisterAddr<DSTS>(0xc70c); }

  // Link state in SS node
  static constexpr uint32_t USBLNKST_U0 = 0x0;
  static constexpr uint32_t USBLNKST_U1 = 0x1;
  static constexpr uint32_t USBLNKST_U2 = 0x2;
  static constexpr uint32_t USBLNKST_U3 = 0x3;
  static constexpr uint32_t USBLNKST_ESS_DIS = 0x4;
  static constexpr uint32_t USBLNKST_RX_DET = 0x5;
  static constexpr uint32_t USBLNKST_ESS_INACT = 0x6;
  static constexpr uint32_t USBLNKST_POLL = 0x7;
  static constexpr uint32_t USBLNKST_RECOV = 0x8;
  static constexpr uint32_t USBLNKST_HRESET = 0x9;
  static constexpr uint32_t USBLNKST_CMPLY = 0xa;
  static constexpr uint32_t USBLNKST_LPBK = 0xb;
  static constexpr uint32_t USBLNKST_RESUME_RESET = 0xf;

  // Link state in HS/FS/LS node
  static constexpr uint32_t USBLNKST_ON = 0x0;
  static constexpr uint32_t USBLNKST_SLEEP = 0x2;
  static constexpr uint32_t USBLNKST_SUSPEND = 0x3;
  static constexpr uint32_t USBLNKST_DISCONNECTED = 0x4;
  static constexpr uint32_t USBLNKST_EARLY_SUSPEND = 0x5;
  static constexpr uint32_t USBLNKST_RESET = 0xe;
  static constexpr uint32_t USBLNKST_RESUME = 0xf;

  // Connection speed
  static constexpr uint32_t CONNECTSPD_HIGH = 0;
  static constexpr uint32_t CONNECTSPD_FULL = 1;
  static constexpr uint32_t CONNECTSPD_SUPER = 4;
  static constexpr uint32_t CONNECTSPD_ENHANCED_SUPER = 5;
};

// Device Active USB Endpoint Enable Register
class DALEPENA : public hwreg::RegisterBase<DALEPENA, uint32_t> {
 public:
  DEF_FIELD(31, 0, USBACTEP);
  static auto Get() { return hwreg::RegisterAddr<DALEPENA>(0xc720); }

  DALEPENA& EnableEp(uint32_t ep) {
    *reg_value_ptr() |= (1 << ep);
    return *this;
  }

  DALEPENA& DisableEp(uint32_t ep) {
    *reg_value_ptr() &= ~(1 << ep);
    return *this;
  }
};

// Device Physical Endpoint-n Command Parameter 2 Register
class DEPCMDPAR2 : public hwreg::RegisterBase<DEPCMDPAR2, uint32_t> {
 public:
  DEF_FIELD(31, 0, PARAMETER);
  static auto Get(uint32_t index) { return hwreg::RegisterAddr<DEPCMDPAR2>(0xc800 + index * 0x10); }
};

// Device Physical Endpoint-n Command Parameter 1 Register
class DEPCMDPAR1 : public hwreg::RegisterBase<DEPCMDPAR1, uint32_t> {
 public:
  DEF_FIELD(31, 0, PARAMETER);
  static auto Get(uint32_t index) { return hwreg::RegisterAddr<DEPCMDPAR1>(0xc804 + index * 0x10); }
};

// Device Physical Endpoint-n Command Parameter 0 Register
class DEPCMDPAR0 : public hwreg::RegisterBase<DEPCMDPAR0, uint32_t> {
 public:
  DEF_FIELD(31, 0, PARAMETER);
  static auto Get(uint32_t index) { return hwreg::RegisterAddr<DEPCMDPAR0>(0xc808 + index * 0x10); }
};

// Variant of DEPCMDPAR1 for the DEPCFG command
class DEPCFG_DEPCMDPAR1 : public hwreg::RegisterBase<DEPCFG_DEPCMDPAR1, uint32_t> {
 public:
  DEF_BIT(31, FIFO_BASED);
  DEF_FIELD(29, 25, EP_NUMBER);
  DEF_BIT(24, STREAM_CAPABLE);
  DEF_FIELD(23, 16, INTERVAL);
  DEF_BIT(15, EBC);                // External Buffer Control
  DEF_BIT(14, EBC_NO_WRITE_BACK);  // Don't write back HWO bit to the TRB descriptor
  DEF_BIT(13, STREAM_EVT_EN);
  DEF_BIT(10, XFER_NOT_READY_EN);
  DEF_BIT(9, XFER_IN_PROGRESS_EN);
  DEF_BIT(8, XFER_COMPLETE_EN);
  DEF_FIELD(4, 0, INTR_NUM);
  static auto Get(uint32_t index) {
    return hwreg::RegisterAddr<DEPCFG_DEPCMDPAR1>(0xc804 + index * 0x10);
  }
};

// Variant of DEPCMDPAR0 for the DEPCFG command
class DEPCFG_DEPCMDPAR0 : public hwreg::RegisterBase<DEPCFG_DEPCMDPAR0, uint32_t> {
 public:
  DEF_FIELD(31, 30, ACTION);
  DEF_FIELD(25, 22, BURST_SIZE);  // subtract one
  DEF_FIELD(21, 17, FIFO_NUM);
  DEF_BIT(15, INTERNAL_RETRY);
  DEF_FIELD(13, 3, MAX_PACKET_SIZE);
  DEF_FIELD(2, 1, EP_TYPE);
  static auto Get(uint32_t index) {
    return hwreg::RegisterAddr<DEPCFG_DEPCMDPAR0>(0xc808 + index * 0x10);
  }

  static constexpr uint32_t ACTION_INITIALIZE = 0;
  static constexpr uint32_t ACTION_RESTORE = 1;
  static constexpr uint32_t ACTION_MODIFY = 2;
};

// Device Physical Endpoint-n Command Register
class DEPCMD : public hwreg::RegisterBase<DEPCMD, uint32_t> {
 public:
  DEF_FIELD(31, 16, COMMANDPARAM);
  DEF_FIELD(15, 12, CMDSTATUS);
  DEF_BIT(11, HIPRI_FORCERM);
  DEF_BIT(10, CMDACT);
  DEF_BIT(8, CMDIOC);
  DEF_FIELD(3, 0, CMDTYP);
  static auto Get(uint32_t index) { return hwreg::RegisterAddr<DEPCMD>(0xc80c + index * 0x10); }

  // Command Types
  static constexpr uint32_t DEPCFG = 1;       // Set Endpoint Configuration
  static constexpr uint32_t DEPXFERCFG = 2;   // Set Endpoint Transfer Resource Configuration
  static constexpr uint32_t DEPGETSTATE = 3;  // Get Endpoint State
  static constexpr uint32_t DEPSSTALL = 4;    // Set Stall
  static constexpr uint32_t DEPCSTALL = 5;    // Clear Stall
  static constexpr uint32_t DEPSTRTXFER = 6;  // Start Transfer
  static constexpr uint32_t DEPUPDXFER = 7;   // Update Transfer
  static constexpr uint32_t DEPENDXFER = 8;   // End Transfer
  static constexpr uint32_t DEPSTARTCFG = 9;  // Start New Configuration
};
