// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_MT8167_INCLUDE_SOC_MT8167_MT8167_USB_H_
#define SRC_DEVICES_LIB_MT8167_INCLUDE_SOC_MT8167_MT8167_USB_H_

#include <zircon/types.h>

#include <hwreg/bitfields.h>

namespace board_mt8167 {

// Function Address Register (peripheral mode)
class FADDR : public hwreg::RegisterBase<FADDR, uint8_t> {
 public:
  DEF_FIELD(6, 0, function_address);
  static auto Get() { return hwreg::RegisterAddr<FADDR>(0x00); }
};

// Power Management Register (peripheral mode)
class POWER_PERI : public hwreg::RegisterBase<POWER_PERI, uint8_t, hwreg::EnablePrinter> {
 public:
  DEF_BIT(7, isoupdate);
  DEF_BIT(6, softconn);
  DEF_BIT(5, hsenab);
  DEF_BIT(4, hsmode);
  DEF_BIT(3, reset);
  DEF_BIT(2, resume);
  DEF_BIT(1, suspendmode);
  DEF_BIT(0, enablesuspendm);
  static auto Get() { return hwreg::RegisterAddr<POWER_PERI>(0x01); }
};

// Power Management Register (host mode)
class POWER_HOST : public hwreg::RegisterBase<POWER_HOST, uint8_t> {
 public:
  DEF_BIT(5, hsenab);
  DEF_BIT(4, hsmode);
  DEF_BIT(3, reset);
  DEF_BIT(2, resume);
  DEF_BIT(1, suspendmode);
  DEF_BIT(0, enablesuspendm);
  static auto Get() { return hwreg::RegisterAddr<POWER_HOST>(0x01); }
};

// TX Interrupt Status Register
class INTRTX : public hwreg::RegisterBase<INTRTX, uint16_t, hwreg::EnablePrinter> {
 public:
  // bit field, one bit per TX endpoint
  DEF_FIELD(15, 0, ep_tx);
  static auto Get() { return hwreg::RegisterAddr<INTRTX>(0x02); }
};

// RX Interrupt Status Register
class INTRRX : public hwreg::RegisterBase<INTRRX, uint16_t, hwreg::EnablePrinter> {
 public:
  // bit field, one bit per RX endpoint (endpoints 1 - 15)
  DEF_FIELD(15, 0, ep_rx);
  static auto Get() { return hwreg::RegisterAddr<INTRRX>(0x4); }
};

// TX Interrupt Enable Register
class INTRTXE : public hwreg::RegisterBase<INTRTXE, uint16_t, hwreg::EnablePrinter> {
 public:
  // bit field, one bit per TX endpoint
  DEF_FIELD(15, 0, ep_tx);
  static auto Get() { return hwreg::RegisterAddr<INTRTXE>(0x06); }
};

// RX Interrupt Enable Register
class INTRRXE : public hwreg::RegisterBase<INTRRXE, uint16_t, hwreg::EnablePrinter> {
 public:
  // bit field, one bit per RX endpoint (endpoints 1 - 15)
  DEF_FIELD(15, 0, ep_rx);
  static auto Get() { return hwreg::RegisterAddr<INTRRXE>(0x8); }
};

// Common USB Interrupt Register
class INTRUSB : public hwreg::RegisterBase<INTRUSB, uint8_t, hwreg::EnablePrinter> {
 public:
  DEF_BIT(7, vbuserror);
  DEF_BIT(6, sessreq);
  DEF_BIT(5, discon);
  DEF_BIT(4, conn);
  DEF_BIT(3, sof);
  DEF_BIT(2, reset);
  DEF_BIT(1, resume);
  DEF_BIT(0, suspend);
  static auto Get() { return hwreg::RegisterAddr<INTRUSB>(0x0a); }
};

// Common USB Interrupt Enable Register
class INTRUSBE : public hwreg::RegisterBase<INTRUSBE, uint8_t> {
 public:
  DEF_BIT(7, vbuserror_e);
  DEF_BIT(6, sessreq_e);
  DEF_BIT(5, discon_e);
  DEF_BIT(4, conn_e);
  DEF_BIT(3, sof_e);
  DEF_BIT(2, reset_e);
  DEF_BIT(1, resume_e);
  DEF_BIT(0, suspend_e);
  static auto Get() { return hwreg::RegisterAddr<INTRUSBE>(0x0b); }
};

// Frame Number Register
class FRAME : public hwreg::RegisterBase<FRAME, uint16_t> {
 public:
  DEF_FIELD(10, 0, frame_number);
  static auto Get() { return hwreg::RegisterAddr<FRAME>(0x0c); }
};

// Endpoint Selection Index Register
class INDEX : public hwreg::RegisterBase<INDEX, uint8_t> {
 public:
  DEF_FIELD(3, 0, selected_endpoint);
  static auto Get() { return hwreg::RegisterAddr<INDEX>(0x0e); }
};

// Endpoint Selection Index Register
class TESTMODE : public hwreg::RegisterBase<TESTMODE, uint8_t> {
 public:
  DEF_BIT(7, force_host);
  DEF_BIT(6, fifo_access);
  DEF_BIT(5, force_fs);
  DEF_BIT(4, force_hs);
  DEF_BIT(3, test_packet);
  DEF_BIT(2, test_k);
  DEF_BIT(1, test_j);
  DEF_BIT(0, test_se0_nak);
  static auto Get() { return hwreg::RegisterAddr<TESTMODE>(0x0f); }
};

// USB Endpoint n FIFO Register (32 bit access)
class FIFO : public hwreg::RegisterBase<FIFO, uint32_t> {
 public:
  DEF_FIELD(31, 0, fifo_data);
  static auto Get(uint32_t ep) { return hwreg::RegisterAddr<FIFO>(0x20 + ep * 4); }
};

// USB Endpoint n FIFO Register (8 bit access)
class FIFO_8 : public hwreg::RegisterBase<FIFO_8, uint8_t> {
 public:
  DEF_FIELD(7, 0, fifo_data);
  static auto Get(uint32_t ep) { return hwreg::RegisterAddr<FIFO_8>(0x20 + ep * 4); }
};

// Device Control Register
class DEVCTL : public hwreg::RegisterBase<DEVCTL, uint8_t> {
 public:
  DEF_BIT(7, b_device);
  DEF_BIT(6, fsdev);
  DEF_BIT(5, lsdev);
  DEF_FIELD(4, 3, vbus);
  DEF_BIT(2, hostmode);
  DEF_BIT(1, hostreq);
  DEF_BIT(0, session);
  static auto Get() { return hwreg::RegisterAddr<DEVCTL>(0x60); }
};

// Power Up Counter Register
class PWRUPCNT : public hwreg::RegisterBase<PWRUPCNT, uint8_t> {
 public:
  DEF_FIELD(3, 0, pwrupcnt);
  static auto Get() { return hwreg::RegisterAddr<PWRUPCNT>(0x61); }
};

// FIFO sizes for TXFIFOSZ and RXFIFOSZ
static constexpr uint8_t FIFO_SIZE_8 = 0;
static constexpr uint8_t FIFO_SIZE_16 = 1;
static constexpr uint8_t FIFO_SIZE_32 = 2;
static constexpr uint8_t FIFO_SIZE_64 = 3;
static constexpr uint8_t FIFO_SIZE_128 = 4;
static constexpr uint8_t FIFO_SIZE_256 = 5;
static constexpr uint8_t FIFO_SIZE_512 = 6;
static constexpr uint8_t FIFO_SIZE_1024 = 7;
static constexpr uint8_t FIFO_SIZE_2048 = 8;
static constexpr uint8_t FIFO_SIZE_4096 = 9;

// TX FIFO Size Register
class TXFIFOSZ : public hwreg::RegisterBase<TXFIFOSZ, uint8_t> {
 public:
  DEF_BIT(4, txdpb);
  DEF_FIELD(3, 0, txsz);
  static auto Get() { return hwreg::RegisterAddr<TXFIFOSZ>(0x62); }
};

// RX FIFO Size Register
class RXFIFOSZ : public hwreg::RegisterBase<RXFIFOSZ, uint8_t> {
 public:
  DEF_BIT(4, rxdpb);
  DEF_FIELD(3, 0, rxsz);
  static auto Get() { return hwreg::RegisterAddr<RXFIFOSZ>(0x63); }
};

// TX FIFO Address Register
class TXFIFOADD : public hwreg::RegisterBase<TXFIFOADD, uint16_t> {
 public:
  DEF_FIELD(12, 0, txfifoadd);
  static auto Get() { return hwreg::RegisterAddr<TXFIFOADD>(0x64); }
};

// RX FIFO Address Register
class RXFIFOADD : public hwreg::RegisterBase<RXFIFOADD, uint16_t> {
 public:
  DEF_BIT(15, data_err_intr_en);
  DEF_BIT(14, overrun_intr_en);
  DEF_FIELD(12, 0, rxfifoadd);
  static auto Get() { return hwreg::RegisterAddr<RXFIFOADD>(0x66); }
};

// Hardware Capability Register
class HWCAPS : public hwreg::RegisterBase<HWCAPS, uint16_t> {
 public:
  DEF_BIT(15, qmu_support);
  DEF_BIT(14, hub_support);
  DEF_BIT(13, usb20_support);
  DEF_BIT(12, usb11_support);
  DEF_FIELD(11, 10, mstr_wrap_intfx);
  DEF_FIELD(9, 8, slave_wrap_intfx);
  DEF_FIELD(5, 0, usb_version_code);
  static auto Get() { return hwreg::RegisterAddr<HWCAPS>(0x6c); }
};

// Version Register
class HWSVERS : public hwreg::RegisterBase<HWSVERS, uint16_t> {
 public:
  DEF_FIELD(7, 0, usb_sub_version_code);
  static auto Get() { return hwreg::RegisterAddr<HWSVERS>(0x6e); }
};

// Bus Performance Register 3
class BUSPERF3 : public hwreg::RegisterBase<BUSPERF3, uint16_t> {
 public:
  DEF_BIT(11, vbuserr_mode);
  DEF_BIT(9, flush_fifo_en);
  DEF_BIT(7, noise_still_sof);
  DEF_BIT(6, bab_cl_en);
  DEF_BIT(3, undo_srpfix);
  DEF_BIT(2, otg_deglitch_disable);
  DEF_BIT(1, ep_swrst);
  DEF_BIT(0, disusbreset);
  static auto Get() { return hwreg::RegisterAddr<BUSPERF3>(0x74); }
};

// Number of TX and RX Register
class EPINFO : public hwreg::RegisterBase<EPINFO, uint8_t, hwreg::EnablePrinter> {
 public:
  DEF_FIELD(7, 4, rxendpoints);
  DEF_FIELD(3, 0, txendpoints);
  static auto Get() { return hwreg::RegisterAddr<EPINFO>(0x78); }
};

// Version Register
class RAMINFO : public hwreg::RegisterBase<RAMINFO, uint8_t, hwreg::EnablePrinter> {
 public:
  DEF_FIELD(7, 4, dmachans);
  DEF_FIELD(3, 0, rambits);
  static auto Get() { return hwreg::RegisterAddr<RAMINFO>(0x79); }
};

// RX Toggle State Register
class RXTOG : public hwreg::RegisterBase<RXTOG, uint16_t> {
 public:
  DEF_BIT(8, ep8rxtog);
  DEF_BIT(7, ep7rxtog);
  DEF_BIT(6, ep6rxtog);
  DEF_BIT(5, ep5rxtog);
  DEF_BIT(4, ep4rxtog);
  DEF_BIT(3, ep3rxtog);
  DEF_BIT(2, ep2rxtog);
  DEF_BIT(1, ep1rxtog);
  static auto Get() { return hwreg::RegisterAddr<RXTOG>(0x80); }
};

// RX Toggle Write-Enable Register
class RXTOGEN : public hwreg::RegisterBase<RXTOGEN, uint16_t> {
 public:
  DEF_BIT(8, ep8rxtogen);
  DEF_BIT(7, ep7rxtogen);
  DEF_BIT(6, ep6rxtogen);
  DEF_BIT(5, ep5rxtogen);
  DEF_BIT(4, ep4rxtogen);
  DEF_BIT(3, ep3rxtogen);
  DEF_BIT(2, ep2rxtogen);
  DEF_BIT(1, ep1rxtogen);
  static auto Get() { return hwreg::RegisterAddr<RXTOGEN>(0x82); }
};

// TX Toggle State Register
class TXTOG : public hwreg::RegisterBase<TXTOG, uint16_t> {
 public:
  DEF_BIT(8, ep8txtog);
  DEF_BIT(7, ep7txtog);
  DEF_BIT(6, ep6txtog);
  DEF_BIT(5, ep5txtog);
  DEF_BIT(4, ep4txtog);
  DEF_BIT(3, ep3txtog);
  DEF_BIT(2, ep2txtog);
  DEF_BIT(1, ep1txtog);
  static auto Get() { return hwreg::RegisterAddr<TXTOG>(0x84); }
};

// TX Toggle Write-Enable Register
class TXTOGEN : public hwreg::RegisterBase<TXTOGEN, uint16_t> {
 public:
  DEF_BIT(8, ep8txtogen);
  DEF_BIT(7, ep7txtogen);
  DEF_BIT(6, ep6txtogen);
  DEF_BIT(5, ep5txtogen);
  DEF_BIT(4, ep4txtogen);
  DEF_BIT(3, ep3txtogen);
  DEF_BIT(2, ep2txtogen);
  DEF_BIT(1, ep1txtogen);
  static auto Get() { return hwreg::RegisterAddr<TXTOGEN>(0x86); }
};

// USB Level 1 Interrupt Status Register
class USB_L1INTS : public hwreg::RegisterBase<USB_L1INTS, uint32_t, hwreg::EnablePrinter> {
 public:
  DEF_BIT(11, powerdwn);
  DEF_BIT(10, drvvbus);
  DEF_BIT(9, iddig);
  DEF_BIT(8, vbusvalid);
  DEF_BIT(7, dpdm);
  DEF_BIT(6, qhif);
  DEF_BIT(5, qint);
  DEF_BIT(4, psr);
  DEF_BIT(3, dma);
  DEF_BIT(2, usbcom);
  DEF_BIT(1, rx);
  DEF_BIT(0, tx);
  static auto Get() { return hwreg::RegisterAddr<USB_L1INTS>(0xa0); }
};

// USB Level 1 Interrupt Mask Register
class USB_L1INTM : public hwreg::RegisterBase<USB_L1INTM, uint32_t> {
 public:
  DEF_BIT(11, powerdwn);
  DEF_BIT(10, drvvbus);
  DEF_BIT(9, iddig);
  DEF_BIT(8, vbusvalid);
  DEF_BIT(7, dpdm);
  DEF_BIT(6, qhif);
  DEF_BIT(5, qint);
  DEF_BIT(4, psr);
  DEF_BIT(3, dma);
  DEF_BIT(2, usbcom);
  DEF_BIT(1, rx);
  DEF_BIT(0, tx);
  static auto Get() { return hwreg::RegisterAddr<USB_L1INTM>(0xa4); }
};

// USB Level 1 Interrupt Polarity Register
class USB_L1INTP : public hwreg::RegisterBase<USB_L1INTP, uint32_t> {
 public:
  DEF_BIT(11, powerdwn);
  DEF_BIT(10, drvvbus);
  DEF_BIT(9, iddig);
  DEF_BIT(8, vbusvalid);
  static auto Get() { return hwreg::RegisterAddr<USB_L1INTP>(0xa8); }
};

// USB Level 1 Interrupt Control Register
class USB_L1INTC : public hwreg::RegisterBase<USB_L1INTC, uint32_t> {
 public:
  DEF_BIT(0, usb_int_sync);
  static auto Get() { return hwreg::RegisterAddr<USB_L1INTC>(0xac); }
};

// EP0 Control Status Register (peripheral mode)
class CSR0_PERI : public hwreg::RegisterBase<CSR0_PERI, uint16_t, hwreg::EnablePrinter> {
 public:
  DEF_BIT(8, flushfifo);
  DEF_BIT(7, serviced_setupend);
  DEF_BIT(6, serviced_rxpktrdy);
  DEF_BIT(5, sendstall);
  DEF_BIT(4, setupend);
  DEF_BIT(3, dataend);
  DEF_BIT(2, sentstall);
  DEF_BIT(1, txpktrdy);
  DEF_BIT(0, rxpktrdy);
  static auto Get() { return hwreg::RegisterAddr<CSR0_PERI>(0x102); }
};

// EP0 Control Status Register (host mode)
class CSR0_HOST : public hwreg::RegisterBase<CSR0_HOST, uint16_t> {
 public:
  DEF_BIT(11, disping);
  DEF_BIT(8, flushfifo);
  DEF_BIT(7, naktimeout);
  DEF_BIT(6, statuspkt);
  DEF_BIT(5, reqpkt);
  DEF_BIT(4, error);
  DEF_BIT(3, setuppkt);
  DEF_BIT(2, rxstall);
  DEF_BIT(1, txpktrdy);
  DEF_BIT(0, rxpktrdy);
  static auto Get() { return hwreg::RegisterAddr<CSR0_HOST>(0x102); }
};

// TXMAP Register
class TXMAP : public hwreg::RegisterBase<TXMAP, uint16_t> {
 public:
  DEF_FIELD(12, 11, m_1);
  DEF_FIELD(10, 0, maximum_payload_transaction);
  static auto Get(uint32_t ep) { return hwreg::RegisterAddr<TXMAP>(0x100 + ep * 0x10); }
};

// TX CSR Register (peripheral mode)
class TXCSR_PERI : public hwreg::RegisterBase<TXCSR_PERI, uint16_t, hwreg::EnablePrinter> {
 public:
  DEF_BIT(15, autoset);
  DEF_BIT(14, iso);
  DEF_BIT(12, dmareqen);
  DEF_BIT(11, frcdatatog);
  DEF_BIT(10, dmareqmode);
  DEF_BIT(8, settxpktrdy_twice);
  DEF_BIT(7, incomptx);
  DEF_BIT(6, clrdatatog);
  DEF_BIT(5, sentstall);
  DEF_BIT(4, sendstall);
  DEF_BIT(3, flushfifo);
  DEF_BIT(2, underrun);
  DEF_BIT(1, fifo_not_empty);
  DEF_BIT(0, txpktrdy);
  static auto Get(uint32_t ep) { return hwreg::RegisterAddr<TXCSR_PERI>(0x102 + ep * 0x10); }
};

// TX CSR Register (host mode)
class TXCSR_HOST : public hwreg::RegisterBase<TXCSR_HOST, uint16_t> {
 public:
  DEF_BIT(15, autoset);
  DEF_BIT(12, dmareqen);
  DEF_BIT(11, frcdatatog);
  DEF_BIT(10, dmareqmode);
  DEF_BIT(8, settxpktrdy_twice);
  DEF_BIT(7, naktimeout_incomptx);
  DEF_BIT(6, clrdatatog);
  DEF_BIT(5, rxstall);
  DEF_BIT(3, flushfifo);
  DEF_BIT(2, error);
  DEF_BIT(1, fifonotempty);
  DEF_BIT(0, txpktrdy);
  static auto Get(uint32_t ep) { return hwreg::RegisterAddr<TXCSR_HOST>(0x102 + ep * 0x10); }
};

// RXMAP Register
class RXMAP : public hwreg::RegisterBase<RXMAP, uint16_t> {
 public:
  DEF_FIELD(12, 11, m_1);
  DEF_FIELD(10, 0, maximum_payload_transaction);
  static auto Get(uint32_t ep) { return hwreg::RegisterAddr<RXMAP>(0x104 + ep * 0x10); }
};

// RX CSR Register (peripheral mode)
class RXCSR_PERI : public hwreg::RegisterBase<RXCSR_PERI, uint16_t, hwreg::EnablePrinter> {
 public:
  DEF_BIT(15, autoclear);
  DEF_BIT(14, iso);
  DEF_BIT(13, dmareqen);
  DEF_BIT(12, disnyet_piderr);
  DEF_BIT(11, dmareqmode);
  DEF_BIT(9, keeperrstatus);
  DEF_BIT(8, incomprx);
  DEF_BIT(7, clrdatatog);
  DEF_BIT(6, sentstall);
  DEF_BIT(5, sendstall);
  DEF_BIT(4, flushfifo);
  DEF_BIT(3, dataerr);
  DEF_BIT(2, overrun);
  DEF_BIT(1, fifofull);
  DEF_BIT(0, rxpktrdy);
  static auto Get(uint32_t ep) { return hwreg::RegisterAddr<RXCSR_PERI>(0x106 + ep * 0x10); }
};

// RX CSR Register (host mode)
class RXCSR_HOST : public hwreg::RegisterBase<RXCSR_HOST, uint16_t> {
 public:
  DEF_BIT(15, autoclear);
  DEF_BIT(14, autoreq);
  DEF_BIT(13, dmareqenab);
  DEF_BIT(12, piderror);
  DEF_BIT(11, dmareqmode);
  DEF_BIT(10, setreqpkt_twice);
  DEF_BIT(9, keeperrstatus);
  DEF_BIT(8, incomprx);
  DEF_BIT(7, clrdatatog);
  DEF_BIT(6, rxstall);
  DEF_BIT(5, reqpkt);
  DEF_BIT(4, flushfifo);
  DEF_BIT(3, dataerr_naktimeout);
  DEF_BIT(2, error);
  DEF_BIT(1, fifofull);
  DEF_BIT(0, rxpktrdy);
  static auto Get(uint32_t ep) { return hwreg::RegisterAddr<RXCSR_HOST>(0x106 + ep * 0x10); }
};

// RX Count Register
class RXCOUNT : public hwreg::RegisterBase<RXCOUNT, uint16_t> {
 public:
  DEF_FIELD(13, 0, rxcount);
  static auto Get(uint32_t ep) { return hwreg::RegisterAddr<RXCOUNT>(0x108 + ep * 0x10); }
};

// TX Type Register
class TXTYPE : public hwreg::RegisterBase<TXTYPE, uint8_t> {
 public:
  DEF_FIELD(7, 6, tx_speed);
  DEF_FIELD(5, 4, tx_protocol);
  DEF_FIELD(3, 0, tx_target_ep_number);
  static auto Get(uint32_t ep) { return hwreg::RegisterAddr<TXTYPE>(0x10a + ep * 0x10); }
};

// TX Interval Register
class TXINTERVAL : public hwreg::RegisterBase<TXINTERVAL, uint8_t> {
 public:
  DEF_FIELD(7, 0, tx_polling_interval_nak_limit_m);
  static auto Get(uint32_t ep) { return hwreg::RegisterAddr<TXINTERVAL>(0x10b + ep * 0x10); }
};

// RX Type Register
class RXTYPE : public hwreg::RegisterBase<RXTYPE, uint8_t> {
 public:
  DEF_FIELD(7, 6, rx_speed);
  DEF_FIELD(5, 4, rx_protocol);
  DEF_FIELD(3, 0, rx_target_ep_number);
  static auto Get(uint32_t ep) { return hwreg::RegisterAddr<RXTYPE>(0x10c + ep * 0x10); }
};

// RX Interval Register
class RXINTERVAL : public hwreg::RegisterBase<RXINTERVAL, uint8_t> {
 public:
  DEF_FIELD(7, 0, rx_polling_interval_nak_limit_m);
  static auto Get(uint32_t ep) { return hwreg::RegisterAddr<RXINTERVAL>(0x10d + ep * 0x10); }
};

// Configured FIFO Size Register
class FIFOSIZE : public hwreg::RegisterBase<FIFOSIZE, uint8_t> {
 public:
  DEF_FIELD(7, 4, rxfifosize);
  DEF_FIELD(3, 0, txfifosize);
  static auto Get(uint32_t ep) { return hwreg::RegisterAddr<FIFOSIZE>(0x10f + ep * 0x10); }
};

// DMA Interrupt Status Register
class DMA_INTR : public hwreg::RegisterBase<DMA_INTR, uint32_t, hwreg::EnablePrinter> {
 public:
  DEF_FIELD(31, 24, unmask_set);
  DEF_FIELD(23, 16, unmask_clear);
  DEF_FIELD(15, 8, unmask);
  DEF_FIELD(7, 0, status);
  static auto Get() { return hwreg::RegisterAddr<DMA_INTR>(0x200); }
};

// DMA Channel n Control Register
class DMA_CNTL : public hwreg::RegisterBase<DMA_CNTL, uint16_t, hwreg::EnablePrinter> {
 public:
  DEF_BIT(13, dma_abort);
  DEF_BIT(11, dma_chan);
  DEF_FIELD(10, 9, burst_mode);
  DEF_BIT(8, buserr);
  DEF_FIELD(7, 4, endpoint);
  DEF_BIT(3, inten);
  DEF_BIT(2, dmamode);
  DEF_BIT(1, dir);
  DEF_BIT(0, enable);
  static auto Get(uint32_t n) { return hwreg::RegisterAddr<DMA_CNTL>(0x204 + n * 0x10); }
};

// DMA Channel n Address Register
class DMA_ADDR : public hwreg::RegisterBase<DMA_ADDR, uint32_t> {
 public:
  DEF_FIELD(31, 0, addr);
  static auto Get(uint32_t n) { return hwreg::RegisterAddr<DMA_ADDR>(0x208 + n * 0x10); }
};

// DMA Channel n Address Register
class DMA_COUNT : public hwreg::RegisterBase<DMA_COUNT, uint32_t> {
 public:
  DEF_FIELD(23, 0, count);
  static auto Get(uint32_t n) { return hwreg::RegisterAddr<DMA_COUNT>(0x20C + n * 0x10); }
};

// DMA Limiter Register
class DMA_LIMITER : public hwreg::RegisterBase<DMA_LIMITER, uint32_t> {
 public:
  DEF_FIELD(7, 0, limiter);
  static auto Get() { return hwreg::RegisterAddr<DMA_LIMITER>(0x210); }
};

// DMA Configuration Register
class DMA_CONFIG : public hwreg::RegisterBase<DMA_CONFIG, uint32_t> {
 public:
  DEF_FIELD(11, 10, dma_active_en);
  DEF_FIELD(9, 8, ahb_hprot_2_en);
  DEF_FIELD(6, 4, dmaq_chan_sel);
  DEF_BIT(1, ahbwait_sel);
  DEF_BIT(0, boundary_1k_cross_en);
  static auto Get() { return hwreg::RegisterAddr<DMA_CONFIG>(0x220); }
};

// RX total packets expected from IN-endpoint (host mode)
class RXPKTCOUNT : public hwreg::RegisterBase<RXPKTCOUNT, uint16_t> {
 public:
  DEF_FIELD(15, 0, rxpktcount);
  static auto Get(uint32_t ep) { return hwreg::RegisterAddr<RXPKTCOUNT>(0x300 + ep * 4); }
};

// Endpoint TX-function Address (host mode)
class TXFUNCADDR : public hwreg::RegisterBase<TXFUNCADDR, uint8_t> {
 public:
  DEF_FIELD(6, 0, tx_func_addr);
  static auto Get(uint32_t ep) {
    uint32_t addr = (ep & 1) ? (0x488 + (ep >> 1) * 10) : (0x480 + (ep >> 1) * 0x10);
    return hwreg::RegisterAddr<TXFUNCADDR>(addr);
  }
};

// Endpoint RX-function Address (host mode)
class RXFUNCADDR : public hwreg::RegisterBase<RXFUNCADDR, uint8_t> {
 public:
  DEF_FIELD(6, 0, rx_func_addr);
  static auto Get(uint32_t ep) { return hwreg::RegisterAddr<RXFUNCADDR>(0x484 + ep * 8); }
};

}  // namespace board_mt8167

#endif  // SRC_DEVICES_LIB_MT8167_INCLUDE_SOC_MT8167_MT8167_USB_H_
