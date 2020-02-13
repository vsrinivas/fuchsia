// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_USB_DRIVERS_DWC2_USB_DWC_REGS_H_
#define SRC_DEVICES_USB_DRIVERS_DWC2_USB_DWC_REGS_H_

#include <zircon/hw/usb.h>

#include <hwreg/bitfields.h>

constexpr uint32_t MAX_EPS_CHANNELS = 16;
constexpr uint32_t DWC_MAX_EPS = 32;

constexpr uint8_t DWC_EP0_IN = 0;
constexpr uint8_t DWC_EP0_OUT = 16;

constexpr uint32_t DWC_EP_IN_SHIFT = 0;
constexpr uint32_t DWC_EP_OUT_SHIFT = 16;

constexpr uint32_t DWC_EP_IN_MASK = 0x0000ffff;
constexpr uint32_t DWC_EP_OUT_MASK = 0xffff0000;

constexpr bool DWC_EP_IS_IN(uint8_t ep) { return ep < 16; }
constexpr bool DWC_EP_IS_OUT(uint8_t ep) { return ep >= 16; }

// converts a USB endpoint address to 0 - 31 index
// in endpoints -> 0 - 15
// out endpoints -> 17 - 31 (16 is unused)
constexpr uint8_t DWC_ADDR_TO_INDEX(uint8_t addr) {
  return static_cast<uint8_t>((addr & 0xF) + (16 * !(addr & USB_DIR_IN)));
}

class GOTGCTL : public hwreg::RegisterBase<GOTGCTL, uint32_t> {
 public:
  DEF_BIT(0, sesreqscs);
  DEF_BIT(1, sesreq);
  DEF_BIT(2, vbvalidoven);
  DEF_BIT(3, vbvalidovval);
  DEF_BIT(4, avalidoven);
  DEF_BIT(5, avalidovval);
  DEF_BIT(6, bvalidoven);
  DEF_BIT(7, bvalidovval);
  DEF_BIT(8, hstnegscs);
  DEF_BIT(9, hnpreq);
  DEF_BIT(10, hstsethnpen);
  DEF_BIT(11, devhnpen);
  DEF_BIT(16, conidsts);
  DEF_BIT(17, dbnctime);
  DEF_BIT(18, asesvld);
  DEF_BIT(19, bsesvld);
  DEF_BIT(20, otgver);
  DEF_FIELD(26, 22, hburstlen);
  DEF_BIT(27, chirpen);
  static auto Get() { return hwreg::RegisterAddr<GOTGCTL>(0x0); }
};

class GOTGINT : public hwreg::RegisterBase<GOTGINT, uint32_t> {
 public:
  DEF_BIT(2, sesenddet);
  DEF_BIT(8, sesreqsucstschng);
  DEF_BIT(9, hstnegsucstschng);
  DEF_BIT(17, hstnegdet);
  DEF_BIT(18, adevtoutchng);
  DEF_BIT(19, debdone);
  DEF_BIT(20, mvic);
  static auto Get() { return hwreg::RegisterAddr<GOTGINT>(0x4); }
};

class GAHBCFG : public hwreg::RegisterBase<GAHBCFG, uint32_t> {
 public:
  DEF_BIT(0, glblintrmsk);
  DEF_FIELD(4, 1, hburstlen);
  DEF_BIT(5, dmaenable);
  DEF_BIT(7, nptxfemplvl_txfemplvl);
  DEF_BIT(8, ptxfemplvl);
  DEF_BIT(21, remmemsupp);
  DEF_BIT(22, notialldmawrit);
  DEF_BIT(23, ahbsingle);
  static auto Get() { return hwreg::RegisterAddr<GAHBCFG>(0x8); }
};

class GUSBCFG : public hwreg::RegisterBase<GUSBCFG, uint32_t> {
 public:
  DEF_FIELD(2, 0, toutcal);
  DEF_BIT(3, phyif);
  DEF_BIT(4, ulpi_utmi_sel);
  DEF_BIT(5, fsintf);
  DEF_BIT(6, physel);
  DEF_BIT(7, ddrsel);
  DEF_BIT(8, srpcap);
  DEF_BIT(9, hnpcap);
  DEF_FIELD(13, 10, usbtrdtim);
  DEF_BIT(15, phylpwrclksel);
  DEF_BIT(16, otgutmifssel);
  DEF_BIT(17, ulpi_fsls);
  DEF_BIT(18, ulpi_auto_res);
  DEF_BIT(19, ulpi_clk_sus_m);
  DEF_BIT(20, ulpi_ext_vbus_drv);
  DEF_BIT(21, ulpi_int_vbus_indicator);
  DEF_BIT(22, term_sel_dl_pulse);
  DEF_BIT(23, indicator_complement);
  DEF_BIT(24, indicator_pass_through);
  DEF_BIT(25, ulpi_int_prot_dis);
  DEF_BIT(26, ic_usb_cap);
  DEF_BIT(27, ic_traffic_pull_remove);
  DEF_BIT(28, tx_end_delay);
  DEF_BIT(29, force_host_mode);
  DEF_BIT(30, force_dev_mode);
  static auto Get() { return hwreg::RegisterAddr<GUSBCFG>(0xC); }
};

class GRSTCTL : public hwreg::RegisterBase<GRSTCTL, uint32_t> {
 public:
  DEF_BIT(0, csftrst);
  DEF_BIT(1, hsftrst);
  DEF_BIT(2, hstfrm);
  DEF_BIT(3, intknqflsh);
  DEF_BIT(4, rxfflsh);
  DEF_BIT(5, txfflsh);
  DEF_FIELD(10, 6, txfnum);
  DEF_BIT(30, dmareq);
  DEF_BIT(31, ahbidle);
  static auto Get() { return hwreg::RegisterAddr<GRSTCTL>(0x10); }
};

class GINTSTS : public hwreg::RegisterBase<GINTSTS, uint32_t> {
 public:
  DEF_BIT(0, curmode);
  DEF_BIT(1, modemismatch);
  DEF_BIT(2, otgintr);
  DEF_BIT(3, sof_intr);
  DEF_BIT(4, rxstsqlvl);
  DEF_BIT(5, nptxfempty);
  DEF_BIT(6, ginnakeff);
  DEF_BIT(7, goutnakeff);
  DEF_BIT(8, ulpickint);
  DEF_BIT(9, i2cintr);
  DEF_BIT(10, erlysuspend);
  DEF_BIT(11, usbsuspend);
  DEF_BIT(12, usbreset);
  DEF_BIT(13, enumdone);
  DEF_BIT(14, isooutdrop);
  DEF_BIT(15, eopframe);
  DEF_BIT(16, restoredone);
  DEF_BIT(17, epmismatch);
  DEF_BIT(18, inepintr);
  DEF_BIT(19, outepintr);
  DEF_BIT(20, incomplisoin);
  DEF_BIT(21, incomplisoout);
  DEF_BIT(22, fetsusp);
  DEF_BIT(23, resetdet);
  DEF_BIT(24, port_intr);
  DEF_BIT(25, host_channel_intr);
  DEF_BIT(26, ptxfempty);
  DEF_BIT(27, lpmtranrcvd);
  DEF_BIT(28, conidstschng);
  DEF_BIT(29, disconnect);
  DEF_BIT(30, sessreqintr);
  DEF_BIT(31, wkupintr);
  static auto Get() { return hwreg::RegisterAddr<GINTSTS>(0x14); }
};

class GINTMSK : public hwreg::RegisterBase<GINTMSK, uint32_t> {
 public:
  DEF_BIT(0, curmode);
  DEF_BIT(1, modemismatch);
  DEF_BIT(2, otgintr);
  DEF_BIT(3, sof_intr);
  DEF_BIT(4, rxstsqlvl);
  DEF_BIT(5, nptxfempty);
  DEF_BIT(6, ginnakeff);
  DEF_BIT(7, goutnakeff);
  DEF_BIT(8, ulpickint);
  DEF_BIT(9, i2cintr);
  DEF_BIT(10, erlysuspend);
  DEF_BIT(11, usbsuspend);
  DEF_BIT(12, usbreset);
  DEF_BIT(13, enumdone);
  DEF_BIT(14, isooutdrop);
  DEF_BIT(15, eopframe);
  DEF_BIT(16, restoredone);
  DEF_BIT(17, epmismatch);
  DEF_BIT(18, inepintr);
  DEF_BIT(19, outepintr);
  DEF_BIT(20, incomplisoin);
  DEF_BIT(21, incomplisoout);
  DEF_BIT(22, fetsusp);
  DEF_BIT(23, resetdet);
  DEF_BIT(24, port_intr);
  DEF_BIT(25, host_channel_intr);
  DEF_BIT(26, ptxfempty);
  DEF_BIT(27, lpmtranrcvd);
  DEF_BIT(28, conidstschng);
  DEF_BIT(29, disconnect);
  DEF_BIT(30, sessreqintr);
  DEF_BIT(31, wkupintr);
  static auto Get() { return hwreg::RegisterAddr<GINTMSK>(0x18); }
};

class GRXSTSP : public hwreg::RegisterBase<GRXSTSP, uint32_t> {
 public:
  DEF_FIELD(3, 0, epnum);
  DEF_FIELD(14, 4, bcnt);
  DEF_FIELD(16, 15, dpid);
  DEF_FIELD(20, 17, pktsts);
  DEF_FIELD(24, 21, fn);
  static auto Get() { return hwreg::RegisterAddr<GRXSTSP>(0x20); }
};

class GRXFSIZ : public hwreg::RegisterBase<GRXFSIZ, uint32_t> {
 public:
  DEF_FIELD(31, 0, size);
  static auto Get() { return hwreg::RegisterAddr<GRXFSIZ>(0x24); }
};

class GNPTXFSIZ : public hwreg::RegisterBase<GNPTXFSIZ, uint32_t> {
 public:
  DEF_FIELD(15, 0, startaddr);
  DEF_FIELD(31, 16, depth);
  static auto Get() { return hwreg::RegisterAddr<GNPTXFSIZ>(0x28); }
};

class GNPTXSTS : public hwreg::RegisterBase<GNPTXSTS, uint32_t> {
 public:
  DEF_FIELD(15, 0, nptxfspcavail);
  DEF_FIELD(23, 16, nptxqspcavail);
  DEF_BIT(24, nptxqtop_terminate);
  DEF_FIELD(26, 25, nptxqtop_token);
  DEF_FIELD(30, 27, nptxqtop_chnep);
  static auto Get() { return hwreg::RegisterAddr<GNPTXSTS>(0x2C); }
};

class GSNPSID : public hwreg::RegisterBase<GSNPSID, uint32_t> {
 public:
  DEF_FIELD(31, 0, id);
  static auto Get() { return hwreg::RegisterAddr<GSNPSID>(0x40); }
};

class GHWCFG1 : public hwreg::RegisterBase<GHWCFG1, uint32_t> {
 public:
  DEF_FIELD(1, 0, ep_dir0);
  DEF_FIELD(3, 2, ep_dir1);
  DEF_FIELD(5, 4, ep_dir2);
  DEF_FIELD(7, 6, ep_dir3);
  DEF_FIELD(9, 8, ep_dir4);
  DEF_FIELD(11, 10, ep_dir5);
  DEF_FIELD(13, 12, ep_dir6);
  DEF_FIELD(15, 14, ep_dir7);
  DEF_FIELD(17, 16, ep_dir8);
  DEF_FIELD(19, 18, ep_dir9);
  DEF_FIELD(21, 20, ep_dir10);
  DEF_FIELD(23, 22, ep_dir11);
  DEF_FIELD(25, 24, ep_dir12);
  DEF_FIELD(27, 26, ep_dir13);
  DEF_FIELD(29, 28, ep_dir14);
  DEF_FIELD(31, 30, ep_dir15);
  static auto Get() { return hwreg::RegisterAddr<GHWCFG1>(0x44); }
};

class GHWCFG2 : public hwreg::RegisterBase<GHWCFG2, uint32_t> {
 public:
  DEF_FIELD(2, 0, op_mode);
  DEF_FIELD(4, 3, architecture);
  DEF_BIT(5, point2point);
  DEF_FIELD(7, 6, hs_phy_type);
  DEF_FIELD(9, 8, fs_phy_type);
  DEF_FIELD(13, 10, num_dev_ep);
  DEF_FIELD(17, 14, num_host_chan);
  DEF_BIT(18, perio_ep_supported);
  DEF_BIT(19, dynamic_fifo);
  DEF_BIT(20, multi_proc_int);
  DEF_FIELD(23, 22, nonperio_tx_q_depth);
  DEF_FIELD(25, 24, host_perio_tx_q_depth);
  DEF_FIELD(30, 26, dev_token_q_depth);
  DEF_BIT(31, otg_enable_ic_usb);
  static auto Get() { return hwreg::RegisterAddr<GHWCFG2>(0x48); }
};

class GHWCFG3 : public hwreg::RegisterBase<GHWCFG3, uint32_t> {
 public:
  DEF_FIELD(3, 0, xfer_size_cntr_width);
  DEF_FIELD(6, 4, packet_size_cntr_width);
  DEF_BIT(7, otg_func);
  DEF_BIT(8, i2c);
  DEF_BIT(9, vendor_ctrl_if);
  DEF_BIT(10, optional_features);
  DEF_BIT(11, synch_reset_type);
  DEF_BIT(12, adp_supp);
  DEF_BIT(13, otg_enable_hsic);
  DEF_BIT(14, bc_support);
  DEF_BIT(15, otg_lpm_en);
  DEF_FIELD(31, 16, dfifo_depth);
  static auto Get() { return hwreg::RegisterAddr<GHWCFG3>(0x4C); }
};

class GHWCFG4 : public hwreg::RegisterBase<GHWCFG4, uint32_t> {
 public:
  DEF_FIELD(3, 0, num_dev_perio_in_ep);
  DEF_BIT(4, power_optimiz);
  DEF_BIT(5, min_ahb_freq);
  DEF_BIT(6, part_power_down);
  DEF_FIELD(15, 14, utmi_phy_data_width);
  DEF_FIELD(19, 16, num_dev_mode_ctrl_ep);
  DEF_BIT(20, iddig_filt_en);
  DEF_BIT(21, vbus_valid_filt_en);
  DEF_BIT(22, a_valid_filt_en);
  DEF_BIT(23, b_valid_filt_en);
  DEF_BIT(24, session_end_filt_en);
  DEF_BIT(25, ded_fifo_en);
  DEF_FIELD(29, 26, num_in_eps);
  DEF_BIT(30, desc_dma);
  DEF_BIT(31, desc_dma_dyn);
  static auto Get() { return hwreg::RegisterAddr<GHWCFG4>(0x50); }
};

class GDFIFOCFG : public hwreg::RegisterBase<GDFIFOCFG, uint32_t> {
 public:
  DEF_FIELD(15, 0, gdfifocfg);
  DEF_FIELD(31, 16, epinfobase);
  static auto Get() { return hwreg::RegisterAddr<GDFIFOCFG>(0x5C); }
};

class DTXFSIZ : public hwreg::RegisterBase<DTXFSIZ, uint32_t> {
 public:
  DEF_FIELD(15, 0, startaddr);
  DEF_FIELD(31, 16, depth);
  static auto Get(unsigned i) { return hwreg::RegisterAddr<DTXFSIZ>(0x104 + 4 * (i - 1)); }
};

class DCFG : public hwreg::RegisterBase<DCFG, uint32_t> {
 public:
  enum PeriodicFrameInterval {
    PERCENT_80 = 0,
    PERCENT_85 = 1,
    PERCENT_90 = 2,
    PERCENT_95 = 3,
  };

  DEF_FIELD(1, 0, devspd);
  DEF_BIT(2, nzstsouthshk);
  DEF_BIT(3, ena32khzs);
  DEF_FIELD(10, 4, devaddr);
  DEF_ENUM_FIELD(PeriodicFrameInterval, 12, 11, perfrint);
  DEF_BIT(13, endevoutnak);
  DEF_FIELD(22, 18, epmscnt);
  DEF_BIT(23, descdma);
  DEF_FIELD(25, 24, perschintvl);
  DEF_FIELD(31, 26, resvalid);
  static auto Get() { return hwreg::RegisterAddr<DCFG>(0x800); }
};

class DCTL : public hwreg::RegisterBase<DCTL, uint32_t> {
 public:
  DEF_BIT(0, rmtwkupsig);
  DEF_BIT(1, sftdiscon);
  DEF_BIT(2, gnpinnaksts);
  DEF_BIT(3, goutnaksts);
  DEF_FIELD(6, 4, tstctl);
  DEF_BIT(7, sgnpinnak);
  DEF_BIT(8, cgnpinnak);
  DEF_BIT(9, sgoutnak);
  DEF_BIT(10, cgoutnak);
  DEF_BIT(11, pwronprgdone);
  DEF_FIELD(14, 13, gmc);
  DEF_BIT(15, ifrmnum);
  DEF_BIT(16, nakonbble);
  DEF_BIT(17, encontonbna);
  DEF_BIT(18, besl_reject);
  static auto Get() { return hwreg::RegisterAddr<DCTL>(0x804); }
};

class DSTS : public hwreg::RegisterBase<DSTS, uint32_t> {
 public:
  DEF_BIT(0, suspsts);
  DEF_FIELD(2, 1, enumspd);
  DEF_BIT(3, errticerr);
  DEF_FIELD(21, 8, soffn);
  static auto Get() { return hwreg::RegisterAddr<DSTS>(0x808); }
};

class DIEPMSK : public hwreg::RegisterBase<DIEPMSK, uint32_t> {
 public:
  DEF_BIT(0, xfercompl);
  DEF_BIT(1, epdisabled);
  DEF_BIT(2, ahberr);
  DEF_BIT(3, timeout);
  DEF_BIT(4, intktxfemp);
  DEF_BIT(5, intknepmis);
  DEF_BIT(6, inepnakeff);
  DEF_BIT(8, txfifoundrn);
  DEF_BIT(9, bna);
  DEF_BIT(13, nak);
  static auto Get() { return hwreg::RegisterAddr<DIEPMSK>(0x810); }
};

class DOEPMSK : public hwreg::RegisterBase<DOEPMSK, uint32_t> {
 public:
  DEF_BIT(0, xfercompl);
  DEF_BIT(1, epdisabled);
  DEF_BIT(2, ahberr);
  DEF_BIT(3, setup);
  DEF_BIT(4, outtknepdis);
  DEF_BIT(5, stsphsercvd);
  DEF_BIT(6, back2backsetup);
  DEF_BIT(8, outpkterr);
  DEF_BIT(9, bna);
  DEF_BIT(12, babble);
  DEF_BIT(13, nak);
  DEF_BIT(14, nyet);
  static auto Get() { return hwreg::RegisterAddr<DOEPMSK>(0x814); }
};

class DAINT : public hwreg::RegisterBase<DAINT, uint32_t> {
 public:
  DEF_FIELD(31, 0, enable);
  static auto Get() { return hwreg::RegisterAddr<DAINT>(0x818); }
};

class DAINTMSK : public hwreg::RegisterBase<DAINTMSK, uint32_t> {
 public:
  DEF_FIELD(31, 0, mask);
  static auto Get() { return hwreg::RegisterAddr<DAINTMSK>(0x81C); }
};

class DEPCTL : public hwreg::RegisterBase<DEPCTL, uint32_t> {
 public:
  DEF_FIELD(10, 0, mps);
  DEF_FIELD(14, 11, nextep);
  DEF_BIT(15, usbactep);
  DEF_BIT(16, dpid);
  DEF_BIT(17, naksts);
  DEF_FIELD(19, 18, eptype);
  DEF_BIT(20, snp);
  DEF_BIT(21, stall);
  DEF_FIELD(25, 22, txfnum);
  DEF_BIT(26, cnak);
  DEF_BIT(27, snak);
  DEF_BIT(28, setd0pid);
  DEF_BIT(29, setd1pid);
  DEF_BIT(30, epdis);
  DEF_BIT(31, epena);
  static auto Get(unsigned i) { return hwreg::RegisterAddr<DEPCTL>(0x900 + 0x20 * i); }
};

// Variant of DEPCTL used for endpoint zero
class DEPCTL0 : public hwreg::RegisterBase<DEPCTL0, uint32_t> {
 public:
  enum MaxPacketSize {
    MPS_64 = 0,
    MPS_32 = 1,
    MPS_16 = 2,
    MPS_8 = 3,
  };

  DEF_ENUM_FIELD(MaxPacketSize, 2, 0, mps);
  DEF_FIELD(14, 11, nextep);
  DEF_BIT(15, usbactep);
  DEF_BIT(16, dpid);
  DEF_BIT(17, naksts);
  DEF_FIELD(19, 18, eptype);
  DEF_BIT(20, snp);
  DEF_BIT(21, stall);
  DEF_FIELD(25, 22, txfnum);
  DEF_BIT(26, cnak);
  DEF_BIT(27, snak);
  DEF_BIT(28, setd0pid);
  DEF_BIT(29, setd1pid);
  DEF_BIT(30, epdis);
  DEF_BIT(31, epena);
  static auto Get(unsigned i) { return hwreg::RegisterAddr<DEPCTL0>(0x900 + 0x20 * i); }
};

class DIEPINT : public hwreg::RegisterBase<DIEPINT, uint32_t> {
 public:
  DEF_BIT(0, xfercompl);
  DEF_BIT(1, epdisabled);
  DEF_BIT(2, ahberr);
  DEF_BIT(3, timeout);
  DEF_BIT(4, intktxfemp);
  DEF_BIT(5, intknepmis);
  DEF_BIT(6, inepnakeff);
  DEF_BIT(8, txfifoundrn);
  DEF_BIT(9, bna);
  DEF_BIT(13, nak);
  DEF_BIT(14, nyet);
  static auto Get(unsigned i) { return hwreg::RegisterAddr<DIEPINT>(0x908 + 0x20 * i); }
};

class DOEPINT : public hwreg::RegisterBase<DOEPINT, uint32_t> {
 public:
  DEF_BIT(0, xfercompl);
  DEF_BIT(1, epdisabled);
  DEF_BIT(2, ahberr);
  DEF_BIT(3, setup);
  DEF_BIT(4, outtknepdis);
  DEF_BIT(5, stsphsercvd);
  DEF_BIT(6, back2backsetup);
  DEF_BIT(8, outpkterr);
  DEF_BIT(9, bna);
  DEF_BIT(11, pktdrpsts);
  DEF_BIT(12, babble);
  DEF_BIT(13, nak);
  DEF_BIT(14, nyet);
  DEF_BIT(15, sr);
  static auto Get(unsigned i) { return hwreg::RegisterAddr<DOEPINT>(0x908 + 0x20 * i); }
};

class DEPTSIZ : public hwreg::RegisterBase<DEPTSIZ, uint32_t> {
 public:
  DEF_FIELD(18, 0, xfersize);
  DEF_FIELD(28, 19, pktcnt);
  DEF_FIELD(30, 29, mc);
  static auto Get(unsigned i) { return hwreg::RegisterAddr<DEPTSIZ>(0x910 + 0x20 * i); }
};

class DEPTSIZ0 : public hwreg::RegisterBase<DEPTSIZ0, uint32_t> {
 public:
  DEF_FIELD(6, 0, xfersize);
  DEF_FIELD(20, 19, pktcnt);
  DEF_FIELD(30, 29, supcnt);
  static auto Get(unsigned i) { return hwreg::RegisterAddr<DEPTSIZ0>(0x910 + 0x20 * i); }
};

class DEPDMA : public hwreg::RegisterBase<DEPDMA, uint32_t> {
 public:
  DEF_FIELD(31, 0, addr);
  static auto Get(unsigned i) { return hwreg::RegisterAddr<DEPDMA>(0x914 + 0x20 * i); }
};

class PCGCCTL : public hwreg::RegisterBase<PCGCCTL, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<PCGCCTL>(0xE00); }
};

#endif  // SRC_DEVICES_USB_DRIVERS_DWC2_USB_DWC_REGS_H_
