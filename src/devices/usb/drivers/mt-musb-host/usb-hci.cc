// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "usb-hci.h"

#include <lib/device-protocol/pdev.h>
#include <lib/zx/time.h>
#include <zircon/hw/usb.h>
#include <zircon/status.h>

#include <algorithm>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <fbl/auto_call.h>
#include <soc/mt8167/mt8167-usb-phy.h>
#include <soc/mt8167/mt8167-usb.h>
#include <usb/request-cpp.h>
#include <usb/usb-request.h>

#include "trace.h"
#include "usb-device.h"
#include "usb-root-hub.h"
#include "usb-spew.h"

namespace mt_usb_hci {
namespace regs = board_mt8167;

namespace {

// Currently only a single (peer2peer) device is supported.
constexpr uint32_t kDeviceId = 1;

}  // namespace

void UsbHci::UsbHciRequestQueue(usb_request_t* usb_request,
                                const usb_request_complete_t* complete_cb) {
  usb::BorrowedRequest<> req(usb_request, *complete_cb, sizeof(usb_request_t));
  device_[usb_request->header.device_id]->HandleRequest(std::move(req));
}

void UsbHci::UsbHciSetBusInterface(const usb_bus_interface_protocol_t* bus_intf) {
  bus_ = ddk::UsbBusInterfaceProtocolClient(bus_intf);
  auto status = bus_.AddDevice(root_hub()->id(), 0, root_hub()->speed());
  if (status != ZX_OK) {
    zxlogf(ERROR, "adding device to bus error: %s", zx_status_get_string(status));
  }
}

size_t UsbHci::UsbHciGetMaxDeviceCount() {
  return kMaxDevices;  // Chipset constant.
}

zx_status_t UsbHci::UsbHciEnableEndpoint(uint32_t device_id, const usb_endpoint_descriptor_t* desc,
                                         const usb_ss_ep_comp_descriptor_t*, bool enable) {
  if (enable) {
    return device_[device_id]->EnableEndpoint(*desc);
  }
  return device_[device_id]->DisableEndpoint(*desc);
}

uint64_t UsbHci::UsbHciGetCurrentFrame() {
  // Pending ISOCHRONOUS support.
  zxlogf(ERROR, "%s not currently supported", __func__);
  return 0;
}

zx_status_t UsbHci::UsbHciConfigureHub(uint32_t device_id, usb_speed_t speed,
                                       const usb_hub_descriptor_t* desc, bool multi_tt) {
  if (device_id == root_hub()->id()) {
    // This is a no-op for the emulated root hub.  The hub is constructed in a configured state.
    return ZX_OK;
  }
  // Downstream hubs aren't currently supported (pending multipoint support).
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t UsbHci::UsbHciHubDeviceAdded(uint32_t hub_id, uint32_t port, usb_speed_t speed) {
  uint32_t id = kDeviceId;
  device_[id] =
      std::unique_ptr<UsbDevice>(new HardwareDevice(id, hub_id, speed, usb_mmio()->View(0)));
  auto* device = static_cast<HardwareDevice*>(device_[id].get());

  auto status = device->Enumerate();
  if (status != ZX_OK) {
    zxlogf(ERROR, "enumeration error: %s", zx_status_get_string(status));
    device_[id].reset();
    return status;
  }

  // The device survived enumeration and is ready to be managed by the USB stack.
  status = bus_.AddDevice(id, hub_id, speed);
  if (status != ZX_OK) {
    zxlogf(ERROR, "add device failed: %s", zx_status_get_string(status));
    device_[id].reset();
    return status;
  }
  return ZX_OK;
}

zx_status_t UsbHci::UsbHciHubDeviceRemoved(uint32_t hub_id, uint32_t port) {
  auto* device = static_cast<HardwareDevice*>(device_[kDeviceId].get());

  // Here, we know something is being disconnected from the port, though we cannot guarantee it
  // corresponds to a configured device (e.g. the device may not have survived enumeration).  If
  // there is no configured device, this is a no-op.
  if (!device) {
    return ZX_OK;
  }

  device->Disconnect();

  auto status = bus_.RemoveDevice(kDeviceId);
  if (status != ZX_OK) {
    zxlogf(ERROR, "could not remove device: %s", zx_status_get_string(status));
    return status;
  }
  return ZX_OK;
}

zx_status_t UsbHci::UsbHciHubDeviceReset(uint32_t device_id, uint32_t port) {
  return root_hub()->PortReset();
}

zx_status_t UsbHci::UsbHciResetEndpoint(uint32_t device_id, uint8_t ep_address) {
  // All we need to do to reset an endpoint is cancel all outstanding requests.
  return UsbHciCancelAll(device_id, ep_address);
}

zx_status_t UsbHci::UsbHciResetDevice(uint32_t hub_address, uint32_t device_id) {
  return root_hub()->PortReset();
}

size_t UsbHci::UsbHciGetMaxTransferSize(uint32_t device_id, uint8_t ep_address) {
  return device_[device_id]->GetMaxTransferSize(usb_ep_num2(ep_address));
}

zx_status_t UsbHci::UsbHciCancelAll(uint32_t device_id, uint8_t ep_address) {
  auto* device = static_cast<HardwareDevice*>(device_[device_id].get());
  return device->CancelAll(usb_ep_num2(ep_address));
}

size_t UsbHci::UsbHciGetRequestSize() {
  return usb::BorrowedRequest<>::RequestSize(sizeof(usb_request_t));
}

zx_status_t UsbHci::Create(void* ctx, zx_device_t* parent) {
  zx_status_t status;

  ddk::PDev pdev(parent);
  if (!pdev.is_valid()) {
    zxlogf(ERROR, "could not create PDev");
    return ZX_ERR_INTERNAL;
  }

  std::optional<ddk::MmioBuffer> usb_mmio;
  status = pdev.MapMmio(0, &usb_mmio);
  if (status != ZX_OK || !usb_mmio.has_value()) {
    zxlogf(ERROR, "could not map usb_mmio: %s", zx_status_get_string(status));
    return status;
  }

  std::optional<ddk::MmioBuffer> phy_mmio;
  status = pdev.MapMmio(1, &phy_mmio);
  if (status != ZX_OK || !phy_mmio.has_value()) {
    zxlogf(ERROR, "could not map phy_mmio: %s", zx_status_get_string(status));
    return status;
  }

  zx::interrupt irq;
  status = pdev.GetInterrupt(0, &irq);
  if (status != ZX_OK) {
    zxlogf(ERROR, "could not get interrupt: %s", zx_status_get_string(status));
    return status;
  }

  auto usb_hci =
      std::make_unique<UsbHci>(parent, *std::move(usb_mmio), *std::move(phy_mmio), irq.release());
  if (status != ZX_OK) {
    return status;
  }

  status = usb_hci->Init();
  if (status != ZX_OK) {
    return status;
  }

  status = usb_hci->DdkAdd("mt-usb-host");
  if (status != ZX_OK) {
    return status;
  }

  // devmgr is now in charge of the device.
  __UNUSED auto* _ = usb_hci.release();
  return ZX_OK;
}

void UsbHci::DdkUnbindNew(ddk::UnbindTxn txn) {
  if (irq_thread_.joinable()) {
    irq_.destroy();
    irq_thread_.join();
  }
  txn.Reply();
}

void UsbHci::DdkRelease() { delete this; }

zx_status_t UsbHci::Init() {
  auto status = InitPhy();
  if (status != ZX_OK) {
    return status;
  }

  auto go = [](void* arg) { return static_cast<UsbHci*>(arg)->IrqThread(); };
  irq_thread_ = std::thread(go, this);
  if (!irq_thread_.joinable()) {
    return ZX_ERR_INTERNAL;
  }

  auto cleanup = fbl::MakeAutoCall([&]() {
    irq_.destroy();
    irq_thread_.join();
  });

  status = InitRootHub();
  if (status != ZX_OK) {
    return status;
  }

  status = InitEndpointControllers();
  if (status != ZX_OK) {
    return status;
  }

  cleanup.cancel();
  return ZX_OK;
}

void UsbHci::StartSession() {
  regs::DEVCTL::Get().ReadFrom(usb_mmio()).set_hostreq(1).set_session(1).WriteTo(usb_mmio());
}

void UsbHci::HandleIrq() {
  // Immediately clear IRQs.  MUSBMHDRC documents that reading these registers automatically
  // clears the IRQ events, but that doesn't appear to actually be the case in the MT8167s SoM.
  auto irqs = regs::INTRUSB::Get().ReadFrom(usb_mmio()).WriteTo(usb_mmio());
  auto tx_irqs = regs::INTRTX::Get().ReadFrom(usb_mmio()).WriteTo(usb_mmio());
  auto rx_irqs = regs::INTRRX::Get().ReadFrom(usb_mmio()).WriteTo(usb_mmio());

  // See: MUSBMHDRC 13.2 for the order in which IRQ events need to be serviced.
  if (irqs.conn())
    HandleConnect();
  if (irqs.discon())
    HandleDisconnect();
  for (uint8_t i = 0; i <= std::max(rx_ep_count_, tx_ep_count_); i++) {
    auto mask = static_cast<uint16_t>(1 << i);
    if ((tx_irqs.ep_tx() & mask) || (rx_irqs.ep_rx() & mask)) {
      // Here, note that each endpoint can either be an IN or OUT-type endpoint, but not both.
      HandleEndpoint(i);
    }
  }
}

void UsbHci::HandleConnect() {
  zxlogf(INFO, "mt-usb-host sees port connection.");
  root_hub()->PortConnect();
}

void UsbHci::HandleDisconnect() {
  zxlogf(INFO, "mt-usb-host sees port disconnection.");
  root_hub()->PortDisconnect();
}

void UsbHci::HandleEndpoint(uint8_t ep) {
  auto* device = static_cast<HardwareDevice*>(device_[kDeviceId].get());
  device->ep_queue(ep)->Advance(true);
}

int UsbHci::IrqThread() {
  zx_status_t status;

  // Unmask TX/RX and USB-common interrupt to microprocessor.
  regs::USB_L1INTM::Get()
      .ReadFrom(usb_mmio())
      .set_usbcom(1)
      .set_tx(1)
      .set_rx(1)
      .WriteTo(usb_mmio());

  // Unmask endpoint-0 TX interrupt, we need it for enumeration.  All other endpoint interrupts
  // will be dynamically unmasked as additional endpoints are enabled.
  regs::INTRTXE::Get().ReadFrom(usb_mmio()).set_ep_tx(1).WriteTo(usb_mmio());

  // Unmask USB controller interrupts, see: MUSBMHDRC section 3.2.7.
  regs::INTRUSBE::Get().ReadFrom(usb_mmio()).set_discon_e(1).set_conn_e(1).WriteTo(usb_mmio());

  // Based on the PHY's config, the device will begin life in the A-role (i.e. host) and always
  // negotiate as the host with any connected device.
  StartSession();

  for (;;) {
    status = irq_.wait(nullptr);
    if (status == ZX_ERR_CANCELED) {
      zxlogf(DEBUG, "irq thread break");
      break;
    } else if (status != ZX_OK) {
      zxlogf(ERROR, "irq wait error: %s", zx_status_get_string(status));
      continue;
    }
    HandleIrq();
  }
  return 0;
}

zx_status_t UsbHci::InitPhy() {
  // Statically configure USB Macrocell PHY for USB-A cabling and USB-Host role.
  regs::U2PHYDTM0_1P::Get().ReadFrom(phy_mmio()).set_force_uart_en(0).WriteTo(phy_mmio());
  regs::U2PHYDTM1_1P::Get().ReadFrom(phy_mmio()).set_rg_uart_en(0).WriteTo(phy_mmio());
  regs::USBPHYACR6_1P::Get().ReadFrom(phy_mmio()).set_rg_usb20_bc11_sw_en(0).WriteTo(phy_mmio());
  regs::U2PHYACR4_1P::Get()
      .ReadFrom(phy_mmio())
      .set_usb20_dp_100k_en(0)
      .set_rg_usb20_dm_100k_en(0)
      .WriteTo(phy_mmio());
  regs::U2PHYDTM0_1P::Get().ReadFrom(phy_mmio()).set_force_suspendm(0).WriteTo(phy_mmio());

  zx::nanosleep(zx::deadline_after(zx::usec(800)));

  regs::U2PHYDTM1_1P::Get()
      .ReadFrom(phy_mmio())
      .set_force_vbusvalid(1)
      .set_force_sessend(1)
      .set_force_bvalid(1)
      .set_force_avalid(1)
      .set_force_iddig(1)
      .set_rg_vbusvalid(0)
      .set_rg_sessend(0)
      .set_rg_bvalid(0)
      .set_rg_avalid(0)
      .set_rg_iddig(0)
      .WriteTo(phy_mmio());

  zx::nanosleep(zx::deadline_after(zx::usec(5)));

  regs::U2PHYDTM1_1P::Get()
      .ReadFrom(phy_mmio())
      .set_rg_vbusvalid(1)
      .set_rg_sessend(0)
      .set_rg_bvalid(1)
      .set_rg_avalid(1)
      .WriteTo(phy_mmio());

  zx::nanosleep(zx::deadline_after(zx::usec(800)));

  return ZX_OK;
}

zx_status_t UsbHci::InitRootHub() {
  device_[kRootHubId] = std::unique_ptr<UsbDevice>(new UsbRootHub(kRootHubId, usb_mmio()->View(0)));
  return ZX_OK;
}

zx_status_t UsbHci::InitEndpointControllers() {
  auto epinfo = regs::EPINFO::Get().ReadFrom(usb_mmio());
  rx_ep_count_ = epinfo.rxendpoints();
  tx_ep_count_ = epinfo.txendpoints();

  // Each FIFO is initialized to the largest it could possibly be (singly-buffered).  As endpoints
  // are subsequently intialized, each FIFO will be appropriately resized based on the needs of
  // the endpoint the FIFO supports.  Here, note that FIFO assumes 64-bit wordsize.
  constexpr uint32_t fifo_size = kFifoMaxSize >> 3;
  uint32_t fifo_addr = (64 >> 3);  // The first 64 bytes are used by endpoint-0.
  for (uint8_t i = 1; i <= rx_ep_count_; i++) {
    regs::INDEX::Get().FromValue(0).set_selected_endpoint(i).WriteTo(usb_mmio());
    regs::RXFIFOADD::Get()
        .FromValue(0)
        .set_rxfifoadd(static_cast<uint16_t>(fifo_addr))
        .WriteTo(usb_mmio());
    fifo_addr += fifo_size;

    // See: MUSBMHDRC section 3.10.1.
    regs::RXFIFOSZ::Get().FromValue(0).set_rxsz(0x9).WriteTo(usb_mmio());
  }
  for (uint8_t i = 1; i <= tx_ep_count_; i++) {
    regs::INDEX::Get().FromValue(0).set_selected_endpoint(i).WriteTo(usb_mmio());
    regs::TXFIFOADD::Get()
        .FromValue(0)
        .set_txfifoadd(static_cast<uint16_t>(fifo_addr))
        .WriteTo(usb_mmio());
    fifo_addr += fifo_size;
    regs::TXFIFOSZ::Get().FromValue(0).set_txsz(0x9).WriteTo(usb_mmio());
  }
  return ZX_OK;
}

static constexpr zx_driver_ops_t driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = UsbHci::Create;
  return ops;
}();

}  // namespace mt_usb_hci

// clang-format off
ZIRCON_DRIVER_BEGIN(mt_usb_hci, mt_usb_hci::driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_MEDIATEK),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_MUSB_HOST),
ZIRCON_DRIVER_END(mt_usb_hci)
    // clang-format on
