// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mt-usb.h"

#include <assert.h>
#include <lib/device-protocol/platform-device.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zircon/assert.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/device.h>
#include <fbl/algorithm.h>
#include <fbl/auto_lock.h>
#include <hw/reg.h>
#include <soc/mt8167/mt8167-usb-phy.h>
#include <soc/mt8167/mt8167-usb.h>
#include <usb/usb-request.h>

namespace mt_usb {
using namespace board_mt8167;  // Hardware registers.

MtUsb::Endpoint* MtUsb::EndpointFromAddress(uint8_t addr) {
  size_t ep_num = addr & USB_ENDPOINT_NUM_MASK;
  if (ep_num == 0 || ep_num > NUM_EPS) {
    zxlogf(ERROR, "%s: invalid endpoint address %02x", __func__, addr);
    return nullptr;
  }

  if ((addr & USB_ENDPOINT_DIR_MASK) == USB_DIR_IN) {
    return &in_eps_[ep_num - 1];
  } else {
    return &out_eps_[ep_num - 1];
  }
}

zx_status_t MtUsb::Create(void* ctx, zx_device_t* parent) {
  pdev_protocol_t pdev;

  auto status = device_get_protocol(parent, ZX_PROTOCOL_PDEV, &pdev);
  if (status != ZX_OK) {
    return status;
  }

  fbl::AllocChecker ac;
  auto mt_usb = fbl::make_unique_checked<MtUsb>(&ac, parent, &pdev);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  status = mt_usb->Init();
  if (status != ZX_OK) {
    return status;
  }

  // devmgr is now in charge of the device.
  __UNUSED auto* dummy = mt_usb.release();
  return ZX_OK;
}

zx_status_t MtUsb::Init() {
  for (uint8_t i = 0; i < countof(out_eps_); i++) {
    auto& ep = out_eps_[i];
    ep.ep_num = static_cast<uint8_t>(i + 1);
    ep.direction = EP_OUT;
  }
  for (uint8_t i = 0; i < countof(in_eps_); i++) {
    auto& ep = in_eps_[i];
    ep.ep_num = static_cast<uint8_t>(i + 1);
    ep.direction = EP_IN;
  }

  auto status = pdev_.MapMmio(0, &usb_mmio_);
  if (status != ZX_OK) {
    return status;
  }

  status = pdev_.MapMmio(1, &phy_mmio_);
  if (status != ZX_OK) {
    return status;
  }

  status = pdev_.GetInterrupt(0, &irq_);
  if (status != ZX_OK) {
    return status;
  }

  status = DdkAdd("mt-usb");
  if (status != ZX_OK) {
    return status;
  }
  return ZX_OK;
}

// Initializes PHY in peripheral role, based on bootloader's configuration.
// TODO(voydanoff) Add OTG support, consider moving this to a separate driver.
void MtUsb::InitPhy() {
  auto* mmio = phy_mmio();
  auto usbphyacr6 = USBPHYACR6::Get();
  auto u2phyacr3 = U2PHYACR3::Get();
  auto u2phyacr4 = U2PHYACR4::Get();
  auto u2phydtm0 = U2PHYDTM0::Get();
  auto u2phydtm1 = U2PHYDTM1::Get();

  u2phydtm0.ReadFrom(mmio).set_force_uart_en(0).WriteTo(mmio);
  u2phydtm1.ReadFrom(mmio).set_rg_uart_en(0).WriteTo(mmio);
  u2phyacr4.ReadFrom(mmio).set_tx_vcmpdn_en(0).set_tx_bias_en(0).WriteTo(mmio);
  u2phyacr4.ReadFrom(mmio).set_dp_100k_mode(1).WriteTo(mmio);
  usbphyacr6.ReadFrom(mmio).set_bc11_sw_en(0).WriteTo(mmio);
  u2phyacr4.ReadFrom(mmio).set_dp_100k_en(0).set_dm_100k_en(0).WriteTo(mmio);
  u2phyacr4.ReadFrom(mmio).set_tx_vcmpdn_en(1).WriteTo(mmio);
  u2phydtm0.ReadFrom(mmio).set_force_suspendm(0).WriteTo(mmio);

  usleep(800);

  u2phydtm1.ReadFrom(mmio).set_rg_sessend(0).WriteTo(mmio);
  u2phydtm1.ReadFrom(mmio)
      .set_rg_iddig(1)
      .set_rg_avalid(1)
      .set_rg_bvalid(1)
      .set_rg_vbusvalid(1)
      .set_rg_uart_en(1)
      .set_rg_uart_tx_oe(1)
      .set_rg_uart_i(1)
      .set_clk60m_en(1)
      .set_clk48m_en(1)
      .WriteTo(mmio);
  u2phyacr3.ReadFrom(mmio).set_pupd_bist_en(0).WriteTo(mmio);
  u2phydtm0.ReadFrom(mmio).set_force_uart_en(0).WriteTo(mmio);
  u2phydtm1.ReadFrom(mmio).set_rg_uart_en(0).WriteTo(mmio);
  u2phydtm0.ReadFrom(mmio).set_force_suspendm(0).WriteTo(mmio);
  u2phyacr4.ReadFrom(mmio).set_tx_vcmpdn_en(0).set_tx_bias_en(0).WriteTo(mmio);
  u2phydtm0.ReadFrom(mmio)
      .set_rg_dmpulldown(0)
      .set_rg_dppulldown(0)
      .set_rg_xcvrsel(0)
      .set_rg_termsel(0)
      .WriteTo(mmio);
  u2phydtm0.ReadFrom(mmio).set_rg_datain(0).WriteTo(mmio);
  u2phydtm0.ReadFrom(mmio)
      .set_force_termsel(0)
      .set_force_xcvsel(0)
      .set_force_dp_pulldown(0)
      .set_force_dm_pulldown(0)
      .set_force_datain(0)
      .WriteTo(mmio);
  usbphyacr6.ReadFrom(mmio).set_bc11_sw_en(0).WriteTo(mmio);
  usbphyacr6.ReadFrom(mmio).set_otg_abist_sele(1).WriteTo(mmio);

  usleep(800);
}

void MtUsb::HandleSuspend() {
  // TODO - is this the best place to do this?
  dci_intf_->SetConnected(false);
}

void MtUsb::HandleReset() {
  auto* mmio = usb_mmio();

  FADDR::Get().FromValue(0).set_function_address(0).WriteTo(mmio);
  address_ = 0;
  set_address_ = false;
  configuration_ = 0;

  INTRTXE::Get().FromValue(0).WriteTo(mmio);
  INTRRXE::Get().FromValue(0).WriteTo(mmio);

  BUSPERF3::Get().FromValue(0).set_ep_swrst(1).set_disusbreset(1).WriteTo(mmio);

  // TODO flush fifos

  if (POWER_PERI::Get().ReadFrom(mmio).hsmode()) {
    dci_intf_->SetSpeed(USB_SPEED_HIGH);
    ep0_max_packet_ = 64;
  } else {
    dci_intf_->SetSpeed(USB_SPEED_FULL);
    ep0_max_packet_ = 8;
  }

  TXMAP::Get(0).FromValue(0).set_maximum_payload_transaction(ep0_max_packet_).WriteTo(mmio);
  RXMAP::Get(0).FromValue(0).set_maximum_payload_transaction(ep0_max_packet_).WriteTo(mmio);
}

zx_status_t MtUsb::HandleEp0() {
  auto* mmio = usb_mmio();

  // Loop until we explicitly return from this function.
  // This allows us to handle multiple state transitions at once when appropriate.
  while (true) {
    auto csr0 = CSR0_PERI::Get().ReadFrom(mmio);

    if (csr0.setupend()) {
      csr0.set_serviced_setupend(1);
      csr0.WriteTo(mmio);
      csr0.ReadFrom(mmio);
      ep0_state_ = EP0_IDLE;
    }

    switch (ep0_state_) {
      case EP0_IDLE: {
        if (set_address_) {
          // Set our new address to the FADDR register.
          FADDR::Get().FromValue(0).set_function_address(address_).WriteTo(mmio);
          set_address_ = false;
          dci_intf_->SetConnected(true);
        }

        if (!csr0.rxpktrdy()) {
          return ZX_OK;
        }

        usb_setup_t* setup = &cur_setup_;
        size_t actual;
        FifoRead(0, setup, sizeof(*setup), &actual);
        if (actual != sizeof(cur_setup_)) {
          return ZX_ERR_IO_INVALID;
        }
        zxlogf(DEBUG, "SETUP bmRequestType %x bRequest %u wValue %u wIndex %u wLength %u",
               setup->bmRequestType, setup->bRequest, setup->wValue, setup->wIndex, setup->wLength);

        if (setup->wLength > 0 && (setup->bmRequestType & USB_DIR_MASK) == USB_DIR_OUT) {
          ep0_state_ = EP0_READ;
          ep0_data_offset_ = 0;
          ep0_data_length_ = setup->wLength;
          csr0.ReadFrom(mmio).set_serviced_rxpktrdy(1).set_dataend(0).WriteTo(mmio);
          break;
        } else {
          size_t actual = 0;

          // Handle some special setup requests in this driver.
          if (setup->bmRequestType == (USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE) &&
              setup->bRequest == USB_REQ_SET_ADDRESS) {
            // We save our new address and set it to the FADDR register
            // when we get our next interrupt.
            // We must defer it until after this setup request has completed.
            address_ = static_cast<uint8_t>(setup->wValue);
            set_address_ = true;
          } else if (setup->bmRequestType == (USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE) &&
                     setup->bRequest == USB_REQ_SET_CONFIGURATION) {
            configuration_ = 0;
            auto status = dci_intf_->Control(setup, nullptr, 0, nullptr, 0, &actual);
            if (status != ZX_OK) {
              zxlogf(ERROR, "%s: USB_REQ_SET_CONFIGURATION Control returned %d", __func__,
                     status);
              return status;
            }
            configuration_ = static_cast<uint8_t>(setup->wValue);
            if (configuration_) {
              StartEndpoints();
            }
          } else {
            auto status =
                dci_intf_->Control(setup, nullptr, 0, ep0_data_, sizeof(ep0_data_), &actual);
            if (status != ZX_OK) {
              return status;
            }
          }

          if (actual > 0) {
            ep0_state_ = EP0_WRITE;
            ep0_data_offset_ = 0;
            ep0_data_length_ = actual;
          } else {
            ep0_state_ = EP0_IDLE;
          }

          csr0.ReadFrom(mmio);
          csr0.set_serviced_rxpktrdy(1);
          if (actual == 0) {
            csr0.set_dataend(1);
          }
          csr0.WriteTo(mmio);

          if (ep0_state_ == EP0_IDLE) {
            return ZX_OK;
          }
        }
        break;
      }
      case EP0_READ: {
        if (!csr0.rxpktrdy()) {
          return ZX_OK;
        }

        size_t count = ep0_data_length_ - ep0_data_offset_;
        if (count > ep0_max_packet_) {
          count = ep0_max_packet_;
        }

        size_t actual;
        FifoRead(0, ep0_data_ + ep0_data_offset_, count, &actual);
        ep0_data_offset_ += actual;

        bool complete = (ep0_data_offset_ == ep0_data_length_);
        csr0.ReadFrom(mmio).set_serviced_rxpktrdy(1).set_dataend(complete).WriteTo(mmio);

        if (complete) {
          auto status =
              dci_intf_->Control(&cur_setup_, ep0_data_, ep0_data_length_, nullptr, 0, nullptr);
          ep0_state_ = EP0_IDLE;
          if (status != ZX_OK) {
            zxlogf(ERROR, "%s: Control returned %d", __func__, status);
            return status;
          }
        }
        break;
      }
      case EP0_WRITE: {
        if (csr0.txpktrdy()) {
          return ZX_OK;
        }
        size_t count = ep0_data_length_ - ep0_data_offset_;
        if (count > ep0_max_packet_) {
          count = ep0_max_packet_;
        }
        FifoWrite(0, ep0_data_ + ep0_data_offset_, count);
        ep0_data_offset_ += count;
        if (ep0_data_offset_ == ep0_data_length_) {
          csr0.set_dataend(1).set_txpktrdy(1).WriteTo(mmio);
          ep0_state_ = EP0_IDLE;
        } else {
          csr0.set_txpktrdy(1).WriteTo(mmio);
        }
        break;
      }
    }
  }
}

void MtUsb::HandleEndpointTxLocked(Endpoint* ep) {
  auto* mmio = usb_mmio();
  auto ep_num = ep->ep_num;

  // TODO check errors, clear bits in CSR?

  ZX_DEBUG_ASSERT(ep->direction == EP_IN);

  auto txcsr = TXCSR_PERI::Get(ep_num);

  if (txcsr.ReadFrom(mmio).txpktrdy()) {
    return;
  }

  usb_request_t* req = ep->current_req;
  if (req) {
    auto write_length = req->header.length - ep->cur_offset;

    if (write_length > 0) {
      void* vaddr;
      auto status = usb_request_mmap(req, &vaddr);
      if (status != ZX_OK) {
        zxlogf(ERROR, "%s: usb_request_mmap failed %d", __func__, status);
        req->response.status = status;
        req->response.actual = 0;
        ep->complete_reqs.push(Request(ep->current_req, sizeof(usb_request_t)));
        ep->current_req = nullptr;
      } else {
        auto buffer = static_cast<uint8_t*>(vaddr);
        if (write_length > ep->max_packet_size) {
          write_length = ep->max_packet_size;
        }

        FifoWrite(ep_num, buffer + ep->cur_offset, write_length);
        ep->cur_offset += write_length;

        txcsr.ReadFrom(mmio).set_txpktrdy(1).WriteTo(mmio);
      }
    } else {
      req->response.status = ZX_OK;
      req->response.actual = req->header.length;
      ep->complete_reqs.push(Request(ep->current_req, sizeof(usb_request_t)));
      ep->current_req = nullptr;
    }
  }

  if (ep->enabled && ep->current_req == nullptr) {
    EpQueueNextLocked(ep);
  }
}

void MtUsb::HandleEndpointRxLocked(Endpoint* ep) {
  auto* mmio = usb_mmio();
  auto ep_num = ep->ep_num;

  ZX_DEBUG_ASSERT(ep->direction == EP_OUT);

  // TODO check errors, clear bits in CSR?

  auto rxcsr = RXCSR_PERI::Get(ep_num).ReadFrom(mmio);

  if (!rxcsr.rxpktrdy()) {
    return;
  }

  usb_request_t* req = ep->current_req;
  if (req) {
    size_t length = req->header.length;
    void* vaddr;
    auto status = usb_request_mmap(req, &vaddr);
    if (status != ZX_OK) {
      zxlogf(ERROR, "%s: usb_request_mmap failed %d", __func__, status);
      req->response.status = status;
      req->response.actual = 0;
      ep->complete_reqs.push(Request(ep->current_req, sizeof(usb_request_t)));
      ep->current_req = nullptr;
    } else {
      auto buffer = static_cast<uint8_t*>(vaddr);
      length -= ep->cur_offset;
      if (length > ep->max_packet_size) {
        length = ep->max_packet_size;
      }

      size_t actual = 0;
      if (length > 0) {
        FifoRead(ep_num, buffer + ep->cur_offset, length, &actual);
        ep->cur_offset += actual;
        // signal that we read the packet
        rxcsr.ReadFrom(mmio).set_rxpktrdy(0).WriteTo(mmio);
      }

      if (actual < length || ep->cur_offset == req->header.length) {
        req->response.status = ZX_OK;
        req->response.actual = ep->cur_offset;
        ep->complete_reqs.push(Request(ep->current_req, sizeof(usb_request_t)));
        ep->current_req = nullptr;
      }
    }
  }

  if (ep->enabled && ep->current_req == nullptr) {
    EpQueueNextLocked(ep);
  }
}

void MtUsb::EpQueueNextLocked(Endpoint* ep) {
  std::optional<Request> req;

  if (ep->current_req == nullptr && (req = ep->queued_reqs.pop()).has_value()) {
    ep->current_req = req->take();
    ep->cur_offset = 0;

    if (ep->direction == EP_IN) {
      HandleEndpointTxLocked(ep);
    } else {
      HandleEndpointRxLocked(ep);
    }
  }
}

void MtUsb::StartEndpoint(Endpoint* ep) {
  fbl::AutoLock lock(&ep->lock);

  if (ep->enabled) {
    EpQueueNextLocked(ep);
  }
}

void MtUsb::StartEndpoints() {
  for (uint8_t i = 0; i < countof(out_eps_); i++) {
    StartEndpoint(&out_eps_[i]);
  }
  for (uint8_t i = 0; i < countof(in_eps_); i++) {
    StartEndpoint(&in_eps_[i]);
  }
}

void MtUsb::SetStall(Endpoint* ep, bool stall) {
  auto* mmio = usb_mmio();

  if (ep->direction == EP_IN) {
    TXCSR_PERI::Get(ep->ep_num).ReadFrom(mmio).set_sendstall(stall ? 1 : 0).WriteTo(mmio);
  } else {
    RXCSR_PERI::Get(ep->ep_num).ReadFrom(mmio).set_sendstall(stall ? 1 : 0).WriteTo(mmio);
  }
}

void MtUsb::FifoRead(uint8_t ep_index, void* buf, size_t buflen, size_t* actual) {
  auto* mmio = usb_mmio();

  size_t count = RXCOUNT::Get(ep_index).ReadFrom(mmio).rxcount();
  if (count > buflen) {
    zxlogf(ERROR, "%s: buffer too small: buflen %zu rxcount %zu", __func__, buflen, count);
    count = buflen;
  }

  auto remaining = count;
  auto dest = static_cast<uint32_t*>(buf);

  while (remaining >= 4) {
    *dest++ = FIFO::Get(ep_index).ReadFrom(mmio).fifo_data();
    remaining -= 4;
  }
  auto dest_8 = reinterpret_cast<uint8_t*>(dest);
  while (remaining > 0) {
    *dest_8++ = FIFO_8::Get(ep_index).ReadFrom(mmio).fifo_data();
    remaining--;
  }

  *actual = count;
}

void MtUsb::FifoWrite(uint8_t ep_index, const void* buf, size_t length) {
  auto* mmio = usb_mmio();

  auto remaining = length;
  auto src = static_cast<const uint8_t*>(buf);

  auto fifo = FIFO_8::Get(ep_index).FromValue(0);

  while (remaining-- > 0) {
    fifo.set_fifo_data(*src++).WriteTo(mmio);
  }
}

int MtUsb::IrqThread() {
  auto* mmio = usb_mmio();

  // Turn off power first
  POWER_PERI::Get().ReadFrom(mmio).set_softconn(0).WriteTo(mmio);

  InitPhy();

  // Turn power back on
  POWER_PERI::Get().ReadFrom(mmio).set_softconn(1).set_enablesuspendm(1).set_hsenab(1).WriteTo(
      mmio);

  // Clear interrupts first
  INTRTX::Get().FromValue(0xffff).WriteTo(mmio);
  INTRRX::Get().FromValue(0xffff).WriteTo(mmio);
  INTRUSB::Get().FromValue(0xff).WriteTo(mmio);

  // Enable TX and RX interrupts for endpoint zero
  INTRTXE::Get().FromValue(0).set_ep_tx(1 << 0).WriteTo(mmio);

  // Enable USB interrupts
  INTRUSBE::Get()
      .FromValue(0)
      .set_discon_e(1)
      .set_reset_e(1)
      .set_resume_e(1)
      .set_suspend_e(1)
      .WriteTo(mmio);

  // Enable USB level 1 interrupts
  USB_L1INTM::Get().FromValue(0).set_tx(1).set_rx(1).set_usbcom(1).WriteTo(mmio);

  // Configure all endpoints other than endpoint zero to use 1024 byte double-buffered FIFOs.
  constexpr uint32_t fifo_size = 1024 >> 3;  // FIFO size is measured in 8 byte units.
  uint32_t fifo_addr = (64 >> 3);            // First 64 bytes used for endpoint zero.
  for (uint8_t i = 1; i <= NUM_EPS; i++) {
    INDEX::Get().FromValue(0).set_selected_endpoint(i).WriteTo(mmio);

    ZX_DEBUG_ASSERT(fifo_addr < UINT16_MAX);
    TXFIFOADD::Get().FromValue(0).set_txfifoadd(static_cast<uint16_t>(fifo_addr)).WriteTo(mmio);
    fifo_addr += 2 * fifo_size;  // double-buffered

    ZX_DEBUG_ASSERT(fifo_addr < UINT16_MAX);
    RXFIFOADD::Get().FromValue(0).set_rxfifoadd(static_cast<uint16_t>(fifo_addr)).WriteTo(mmio);
    fifo_addr += 2 * fifo_size;  // double-buffered

    TXFIFOSZ::Get().FromValue(0).set_txdpb(1).set_txsz(FIFO_SIZE_1024).WriteTo(mmio);
    RXFIFOSZ::Get().FromValue(0).set_rxdpb(1).set_rxsz(FIFO_SIZE_1024).WriteTo(mmio);
  }

  while (true) {
    auto status = irq_.wait(nullptr);
    if (status == ZX_ERR_CANCELED) {
      return 0;
    } else if (status != ZX_OK) {
      zxlogf(ERROR, "%s: irq_.wait failed: %d", __func__, status);
      return -1;
    }
    zxlogf(DEBUG, " \n%s: got interrupt!", __func__);

    // Write back these registers to acknowledge the interrupts
    auto intrtx = INTRTX::Get().ReadFrom(mmio).WriteTo(mmio);
    auto intrrx = INTRRX::Get().ReadFrom(mmio).WriteTo(mmio);
    auto intrusb = INTRUSB::Get().ReadFrom(mmio).WriteTo(mmio);

    if (intrusb.suspend()) {
      HandleSuspend();
    }
    if (intrusb.reset()) {
      HandleReset();
    }

    auto ep_tx = intrtx.ep_tx();
    auto ep_rx = intrrx.ep_rx();

    if (ep_tx) {
      if (ep_tx & (1 << 0)) {
        auto status = HandleEp0();
        if (status != ZX_OK) {
          // Stall
          CSR0_PERI::Get().ReadFrom(mmio).set_sendstall(1).WriteTo(mmio);
        }
      }

      for (unsigned i = 0; i < countof(in_eps_); i++) {
        if (ep_tx & (1 << (i + 1))) {
          Endpoint* ep = &in_eps_[i];
          // requests to complete outside of the lock
          RequestQueue complete_reqs;

          {
            fbl::AutoLock lock(&ep->lock);
            HandleEndpointTxLocked(ep);
            complete_reqs = std::move(ep->complete_reqs);
          }
          // Requests must be completed outside of the lock.
          for (auto req = complete_reqs.pop(); req; req = complete_reqs.pop()) {
            const auto& response = req->request()->response;
            req->Complete(response.status, response.actual);
          }
        }
      }
    }

    if (ep_rx) {
      for (unsigned i = 0; i <= countof(out_eps_); i++) {
        if (ep_rx & (1 << (i + 1))) {
          Endpoint* ep = &out_eps_[i];
          RequestQueue complete_reqs;

          {
            fbl::AutoLock lock(&ep->lock);
            HandleEndpointRxLocked(ep);
            complete_reqs = std::move(ep->complete_reqs);
          }
          // Requests must be completed outside of the lock.
          for (auto req = complete_reqs.pop(); req; req = complete_reqs.pop()) {
            const auto& response = req->request()->response;
            req->Complete(response.status, response.actual);
          }
        }
      }
    }
  }
}

void MtUsb::DdkUnbindDeprecated() {
  irq_.destroy();
  thrd_join(irq_thread_, nullptr);
}

void MtUsb::DdkRelease() { delete this; }
zx_status_t MtUsb::UsbDciCancelAll(uint8_t ep) {
  Endpoint* endpoint = EndpointFromAddress(ep);
  if (!endpoint) {
    return ZX_ERR_INVALID_ARGS;
  }
  RequestQueue queue;
  {
    fbl::AutoLock l(&endpoint->lock);
    queue = std::move(endpoint->queued_reqs);
    if (endpoint->current_req) {
      Request pending_request(endpoint->current_req, sizeof(usb_request_t));
      queue.push(std::move(pending_request));
      endpoint->current_req = nullptr;
    }
  }
  for (auto req = queue.pop(); req; req = queue.pop()) {
    req->Complete(ZX_ERR_IO_NOT_PRESENT, 0);
  }
  return ZX_OK;
}
void MtUsb::UsbDciRequestQueue(usb_request_t* req, const usb_request_complete_t* cb) {
  auto* ep = EndpointFromAddress(req->header.ep_address);
  if (ep == nullptr) {
    usb_request_complete(req, ZX_ERR_INVALID_ARGS, 0, cb);
    return;
  }

  fbl::AutoLock lock(&ep->lock);

  if (!ep->enabled) {
    lock.release();
    usb_request_complete(req, ZX_ERR_BAD_STATE, 0, cb);
    return;
  }

  ep->queued_reqs.push(Request(req, *cb, sizeof(usb_request_t)));
  EpQueueNextLocked(ep);
}

zx_status_t MtUsb::UsbDciSetInterface(const usb_dci_interface_protocol_t* interface) {
  // TODO - handle interface == nullptr for tear down path?

  if (dci_intf_.has_value()) {
    zxlogf(ERROR, "%s: dci_intf_ already set", __func__);
    return ZX_ERR_BAD_STATE;
  }

  dci_intf_ = ddk::UsbDciInterfaceProtocolClient(interface);

  // Now that the usb-peripheral driver has bound, we can start things up.
  int rc = thrd_create_with_name(
      &irq_thread_, [](void* arg) -> int { return reinterpret_cast<MtUsb*>(arg)->IrqThread(); },
      reinterpret_cast<void*>(this), "mt-usb-irq-thread");
  if (rc != thrd_success) {
    return ZX_ERR_INTERNAL;
  }

  return ZX_OK;
}

zx_status_t MtUsb::UsbDciConfigEp(const usb_endpoint_descriptor_t* ep_desc,
                                  const usb_ss_ep_comp_descriptor_t* ss_comp_desc) {
  auto* mmio = usb_mmio();
  auto ep_address = ep_desc->bEndpointAddress;
  auto* ep = EndpointFromAddress(ep_address);
  if (ep == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }
  auto ep_num = ep->ep_num;

  zxlogf(DEBUG, "%s address %02x ep_num %u direction %u", __func__, ep_address, ep_num,
         ep->direction);

  fbl::AutoLock lock(&ep->lock);

  if (ep->enabled) {
    return ZX_ERR_BAD_STATE;
  }

  ep->address = ep_address;

  if (ep->direction == EP_IN) {
    auto intrtxe = INTRTXE::Get().ReadFrom(mmio);
    uint16_t mask = intrtxe.ep_tx();
    mask |= static_cast<uint16_t>(1 << ep_num);
    intrtxe.set_ep_tx(mask).WriteTo(mmio);
  } else {
    auto intrrxe = INTRRXE::Get().ReadFrom(mmio);
    uint16_t mask = intrrxe.ep_rx();
    mask |= static_cast<uint16_t>(1 << ep_num);
    intrrxe.set_ep_rx(mask).WriteTo(mmio);
  }

  uint16_t max_packet_size = usb_ep_max_packet(ep_desc);
  if (ep->direction == EP_IN) {
    TXCSR_PERI::Get(ep_num)
        .ReadFrom(mmio)
        .set_clrdatatog(1)
        .set_flushfifo(1)
        .WriteTo(mmio)
        // FIFO must be flushed twice when using double buffering
        .WriteTo(mmio);

    TXMAP::Get(ep_num).FromValue(0).set_maximum_payload_transaction(max_packet_size).WriteTo(mmio);
  } else {
    RXCSR_PERI::Get(ep_num)
        .ReadFrom(mmio)
        .set_clrdatatog(1)
        .set_flushfifo(1)
        .WriteTo(mmio)
        // FIFO must be flushed twice when using double buffering
        .WriteTo(mmio);

    RXMAP::Get(ep_num).FromValue(0).set_maximum_payload_transaction(max_packet_size).WriteTo(mmio);
  }

  ep->max_packet_size = max_packet_size;
  ep->enabled = true;

  if (configuration_) {
    EpQueueNextLocked(ep);
  }

  return ZX_OK;
}

zx_status_t MtUsb::UsbDciDisableEp(uint8_t ep_address) {
  auto* mmio = usb_mmio();
  auto* ep = EndpointFromAddress(ep_address);
  if (ep == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }

  auto ep_num = ep->ep_num;

  zxlogf(DEBUG, "%s address %02x ep_num %u direction %u", __func__, ep_address, ep_num,
         ep->direction);

  fbl::AutoLock lock(&ep->lock);

  if (!ep->enabled) {
    return ZX_ERR_BAD_STATE;
  }

  if (ep->direction == EP_IN) {
    auto intrtxe = INTRTXE::Get().ReadFrom(mmio);
    uint16_t mask = intrtxe.ep_tx();
    mask &= static_cast<uint16_t>(~(1 << ep_num));
    intrtxe.set_ep_tx(mask).WriteTo(mmio);
  } else {
    auto intrrxe = INTRRXE::Get().ReadFrom(mmio);
    uint16_t mask = intrrxe.ep_rx();
    mask &= static_cast<uint16_t>(~(1 << ep_num));
    intrrxe.set_ep_rx(mask).WriteTo(mmio);
  }

  ep->enabled = false;

  return ZX_OK;
}

zx_status_t MtUsb::UsbDciEpSetStall(uint8_t ep_address) {
  auto* ep = EndpointFromAddress(ep_address);
  if (ep == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }
  SetStall(ep, true);
  return ZX_OK;
}

zx_status_t MtUsb::UsbDciEpClearStall(uint8_t ep_address) {
  auto* ep = EndpointFromAddress(ep_address);
  if (ep == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }
  SetStall(ep, false);
  return ZX_OK;
}

size_t MtUsb::UsbDciGetRequestSize() { return Request::RequestSize(sizeof(usb_request_t)); }

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = MtUsb::Create;
  return ops;
}();

}  // namespace mt_usb

ZIRCON_DRIVER_BEGIN(mt_usb, mt_usb::driver_ops, "zircon", "0.1", 3)
BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_MEDIATEK),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_MUSB_PERIPHERAL), ZIRCON_DRIVER_END(mt_usb)
