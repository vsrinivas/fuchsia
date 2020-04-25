// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dwc3.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/composite.h>
#include <fbl/auto_lock.h>
#include <hw/reg.h>
#include <pretty/hexdump.h>
#include <usb/usb-request.h>

#include "dwc3-regs.h"
#include "dwc3-types.h"

// MMIO indices
enum {
  MMIO_USB3OTG,
};

// IRQ indices
enum {
  IRQ_USB3,
};

// Fragment indices
enum {
  FRAGMENT_PDEV,
  FRAGMENT_UMS,
  FRAGMENT_COUNT,
};

void dwc3_print_status(dwc3_t* dwc) {
  auto* mmio = dwc3_mmio(dwc);
  auto dsts = DSTS::Get().ReadFrom(mmio);
  zxlogf(TRACE, "DSTS: ");
  zxlogf(TRACE, "USBLNKST: %d ", dsts.USBLNKST());
  zxlogf(TRACE, "SOFFN: %d ", dsts.SOFFN());
  zxlogf(TRACE, "CONNECTSPD: %d ", dsts.CONNECTSPD());
  if (dsts.DCNRD())
    zxlogf(TRACE, "DCNRD ");
  if (dsts.SRE())
    zxlogf(TRACE, "SRE ");
  if (dsts.RSS())
    zxlogf(TRACE, "RSS ");
  if (dsts.SSS())
    zxlogf(TRACE, "SSS ");
  if (dsts.COREIDLE())
    zxlogf(TRACE, "COREIDLE ");
  if (dsts.DEVCTRLHLT())
    zxlogf(TRACE, "DEVCTRLHLT ");
  if (dsts.RXFIFOEMPTY())
    zxlogf(TRACE, "RXFIFOEMPTY ");
  zxlogf(TRACE, "");

  auto gsts = GSTS::Get().ReadFrom(mmio);
  zxlogf(TRACE, "GSTS: ");
  zxlogf(TRACE, "CBELT: %d ", gsts.CBELT());
  zxlogf(TRACE, "CURMOD: %d ", gsts.CURMOD());
  if (gsts.SSIC_IP())
    zxlogf(TRACE, "SSIC_IP ");
  if (gsts.OTG_IP())
    zxlogf(TRACE, "OTG_IP ");
  if (gsts.BC_IP())
    zxlogf(TRACE, "BC_IP ");
  if (gsts.ADP_IP())
    zxlogf(TRACE, "ADP_IP ");
  if (gsts.Host_IP())
    zxlogf(TRACE, "HOST_IP ");
  if (gsts.Device_IP())
    zxlogf(TRACE, "DEVICE_IP ");
  if (gsts.CSRTimeout())
    zxlogf(TRACE, "CSR_TIMEOUT ");
  if (gsts.BUSERRADDRVLD())
    zxlogf(TRACE, "BUSERRADDRVLD ");
  zxlogf(TRACE, "");
}

static void dwc3_stop(dwc3_t* dwc) {
  auto* mmio = dwc3_mmio(dwc);

  fbl::AutoLock lock(&dwc->lock);

  DCTL::Get().ReadFrom(mmio).set_RUN_STOP(0).set_CSFTRST(1).WriteTo(mmio);
  while (DCTL::Get().ReadFrom(mmio).CSFTRST()) {
    usleep(1000);
  }
}

static void dwc3_start_peripheral_mode(dwc3_t* dwc) {
  auto* mmio = dwc3_mmio(dwc);

  dwc->lock.Acquire();

  // configure and enable PHYs
  GUSB2PHYCFG::Get(0).ReadFrom(mmio).set_USBTRDTIM(9).WriteTo(mmio);
  GUSB3PIPECTL::Get(0)
      .ReadFrom(mmio)
      .set_DELAYP1TRANS(0)
      .set_SUSPENDENABLE(0)
      .set_LFPSFILTER(1)
      .set_SS_TX_DE_EMPHASIS(1)
      .WriteTo(mmio);

  // configure for device mode
  GCTL::Get()
      .FromValue(0)
      .set_PWRDNSCALE(2)
      .set_U2RSTECN(1)
      .set_PRTCAPDIR(GCTL::PRTCAPDIR_DEVICE)
      .set_U2EXIT_LFPS(1)
      .WriteTo(mmio);

  uint32_t nump = 16;
  uint32_t max_speed = DCFG::DEVSPD_SUPER;
  DCFG::Get().ReadFrom(mmio).set_NUMP(nump).set_DEVSPD(max_speed).set_DEVADDR(0).WriteTo(mmio);

  dwc3_events_start(dwc);

  dwc->lock.Release();

  dwc3_ep0_start(dwc);

  dwc->lock.Acquire();

  // start the controller
  DCTL::Get().FromValue(0).set_RUN_STOP(1).WriteTo(mmio);

  dwc->lock.Release();
}

static zx_status_t xhci_get_protocol(void* ctx, uint32_t proto_id, void* protocol) {
  auto* dwc = static_cast<dwc3_t*>(ctx);
  // XHCI uses same MMIO and IRQ as dwc3, so we can just share our pdev protoocl
  // with the XHCI driver
  return device_get_protocol(dwc->pdev_dev, proto_id, protocol);
}

static void xhci_release(void* ctx) {
  auto* dwc = static_cast<dwc3_t*>(ctx);
  fbl::AutoLock lock(&dwc->usb_mode_lock);

  if (dwc->start_device_on_xhci_release) {
    dwc3_start_peripheral_mode(dwc);
    dwc->start_device_on_xhci_release = false;
    dwc->usb_mode = USB_MODE_PERIPHERAL;
  }
}

static zx_protocol_device_t xhci_device_ops = []() {
  zx_protocol_device_t device = {};
  device.version = DEVICE_OPS_VERSION;
  device.get_protocol = xhci_get_protocol;
  device.release = xhci_release;
  return device;
}();

static void dwc3_start_host_mode(dwc3_t* dwc) {
  auto* mmio = dwc3_mmio(dwc);

  dwc->lock.Acquire();

  // configure for host mode
  GCTL::Get()
      .FromValue(0)
      .set_PWRDNSCALE(2)
      .set_U2RSTECN(1)
      .set_PRTCAPDIR(GCTL::PRTCAPDIR_HOST)
      .set_U2EXIT_LFPS(1)
      .WriteTo(mmio);

  dwc->lock.Release();

  // add a device to bind the XHCI driver
  ZX_DEBUG_ASSERT(dwc->xhci_dev == nullptr);

  zx_device_prop_t props[] = {
      {BIND_PLATFORM_DEV_VID, 0, PDEV_VID_GENERIC},
      {BIND_PLATFORM_DEV_PID, 0, PDEV_PID_GENERIC},
      {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_USB_XHCI},
  };

  device_add_args_t args = {};
  args.version = DEVICE_ADD_ARGS_VERSION;
  args.name = "dwc3";
  args.proto_id = ZX_PROTOCOL_PDEV;
  args.ctx = dwc;
  args.ops = &xhci_device_ops;
  args.props = props;
  args.prop_count = countof(props);

  zx_status_t status = device_add(dwc->parent, &args, &dwc->xhci_dev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "dwc3_start_host_mode failed to add device for XHCI: %d", status);
  }
}

void dwc3_usb_reset(dwc3_t* dwc) {
  zxlogf(INFO, "dwc3_usb_reset");

  dwc3_ep0_reset(dwc);

  for (unsigned i = 2; i < countof(dwc->eps); i++) {
    dwc3_ep_end_transfers(dwc, i, ZX_ERR_IO_NOT_PRESENT);
    dwc3_ep_set_stall(dwc, i, false);
  }

  dwc3_set_address(dwc, 0);
  dwc3_ep0_start(dwc);
  usb_dci_interface_set_connected(&dwc->dci_intf, true);
}

void dwc3_disconnected(dwc3_t* dwc) {
  zxlogf(INFO, "dwc3_disconnected");

  dwc3_cmd_ep_end_transfer(dwc, EP0_OUT);
  dwc->ep0_state = EP0_STATE_NONE;

  if (dwc->dci_intf.ops) {
    usb_dci_interface_set_connected(&dwc->dci_intf, false);
  }

  for (unsigned i = 2; i < countof(dwc->eps); i++) {
    dwc3_ep_end_transfers(dwc, i, ZX_ERR_IO_NOT_PRESENT);
    dwc3_ep_set_stall(dwc, i, false);
  }
}

void dwc3_connection_done(dwc3_t* dwc) {
  auto* mmio = dwc3_mmio(dwc);

  dwc->lock.Acquire();

  uint32_t speed = DSTS::Get().ReadFrom(mmio).CONNECTSPD();
  uint16_t ep0_max_packet = 0;

  switch (speed) {
    case DSTS::CONNECTSPD_HIGH:
      dwc->speed = USB_SPEED_HIGH;
      ep0_max_packet = 64;
      break;
    case DSTS::CONNECTSPD_FULL:
      dwc->speed = USB_SPEED_FULL;
      ep0_max_packet = 64;
      break;
    case DSTS::CONNECTSPD_SUPER:
    case DSTS::CONNECTSPD_ENHANCED_SUPER:
      dwc->speed = USB_SPEED_SUPER;
      ep0_max_packet = 512;
      break;
    default:
      zxlogf(ERROR, "dwc3_connection_done: unsupported speed %u", speed);
      dwc->speed = USB_SPEED_UNDEFINED;
      break;
  }

  dwc->lock.Release();

  if (ep0_max_packet) {
    dwc->eps[EP0_OUT].max_packet_size = ep0_max_packet;
    dwc->eps[EP0_IN].max_packet_size = ep0_max_packet;
    dwc3_cmd_ep_set_config(dwc, EP0_OUT, USB_ENDPOINT_CONTROL, ep0_max_packet, 0, true);
    dwc3_cmd_ep_set_config(dwc, EP0_IN, USB_ENDPOINT_CONTROL, ep0_max_packet, 0, true);
  }

  usb_dci_interface_set_speed(&dwc->dci_intf, dwc->speed);
}

void dwc3_set_address(dwc3_t* dwc, unsigned address) {
  auto* mmio = dwc3_mmio(dwc);
  fbl::AutoLock lock(&dwc->lock);

  DCFG::Get().ReadFrom(mmio).set_DEVADDR(address).WriteTo(mmio);
}

void dwc3_reset_configuration(dwc3_t* dwc) {
  auto* mmio = dwc3_mmio(dwc);

  dwc->lock.Acquire();

  // disable all endpoints except EP0_OUT and EP0_IN
  DALEPENA::Get().FromValue(0).EnableEp(EP0_OUT).EnableEp(EP0_IN).WriteTo(mmio);

  dwc->lock.Release();

  for (unsigned i = 2; i < countof(dwc->eps); i++) {
    dwc3_ep_end_transfers(dwc, i, ZX_ERR_IO_NOT_PRESENT);
    dwc3_ep_set_stall(dwc, i, false);
  }
}

static zx_status_t dwc3_cancel_all(void* ctx, uint8_t ep) {
  auto* dwc = static_cast<dwc3_t*>(ctx);
  unsigned ep_num = dwc3_ep_num(ep);
  if (ep_num >= 32) {
    return ZX_ERR_INVALID_ARGS;
  }
  fbl::AutoLock l(&dwc->eps[ep_num].lock);
  if (dwc->eps[ep_num].current_req) {
    dwc3_cmd_ep_end_transfer(dwc, ep);
  }
  if (list_is_empty(&dwc->eps[ep_num].queued_reqs)) {
    return ZX_OK;
  }
  list_node_t list;
  list_move(&dwc->eps[ep_num].queued_reqs, &list);
  dwc_usb_req_internal_t* entry;
  l.release();
  list_for_every_entry (&list, entry, dwc_usb_req_internal_t, node) {
    usb_request_complete(INTERNAL_TO_USB_REQ(entry), ZX_ERR_IO_NOT_PRESENT, 0, &entry->complete_cb);
  };
  return ZX_OK;
}

static void dwc3_request_queue(void* ctx, usb_request_t* req, const usb_request_complete_t* cb) {
  auto* dwc = static_cast<dwc3_t*>(ctx);
  auto* req_int = USB_REQ_TO_INTERNAL(req);
  req_int->complete_cb = *cb;

  zxlogf(SERIAL, "dwc3_request_queue ep: %u", req->header.ep_address);
  unsigned ep_num = dwc3_ep_num(req->header.ep_address);
  if (ep_num < 2 || ep_num >= countof(dwc->eps)) {
    zxlogf(ERROR, "dwc3_request_queue: bad ep address 0x%02X", req->header.ep_address);
    usb_request_complete(req, ZX_ERR_INVALID_ARGS, 0, cb);
    return;
  }

  dwc3_ep_queue(dwc, ep_num, req);
}

static zx_status_t dwc3_set_interface(void* ctx, const usb_dci_interface_protocol_t* dci_intf) {
  auto* dwc = static_cast<dwc3_t*>(ctx);
  memcpy(&dwc->dci_intf, dci_intf, sizeof(dwc->dci_intf));
  return ZX_OK;
}

static zx_status_t dwc3_config_ep(void* ctx, const usb_endpoint_descriptor_t* ep_desc,
                                  const usb_ss_ep_comp_descriptor_t* ss_comp_desc) {
  auto* dwc = static_cast<dwc3_t*>(ctx);
  return dwc3_ep_config(dwc, ep_desc, ss_comp_desc);
}

static zx_status_t dwc3_disable_ep(void* ctx, uint8_t ep_addr) {
  auto* dwc = static_cast<dwc3_t*>(ctx);
  return dwc3_ep_disable(dwc, ep_addr);
}

static zx_status_t dwc3_set_stall(void* ctx, uint8_t ep_address) {
  auto* dwc = static_cast<dwc3_t*>(ctx);
  return dwc3_ep_set_stall(dwc, dwc3_ep_num(ep_address), true);
}

static zx_status_t dwc3_clear_stall(void* ctx, uint8_t ep_address) {
  auto* dwc = static_cast<dwc3_t*>(ctx);
  return dwc3_ep_set_stall(dwc, dwc3_ep_num(ep_address), false);
}

static size_t dwc3_get_request_size(void* ctx) {
  // Allocate dwc_usb_req_internal_t after usb_request_t, to accommodate queueing in
  // the dwc3 layer.
  return sizeof(usb_request_t) + sizeof(dwc_usb_req_internal_t);
}

usb_dci_protocol_ops_t dwc_dci_ops = {.request_queue = dwc3_request_queue,
                                      .set_interface = dwc3_set_interface,
                                      .config_ep = dwc3_config_ep,
                                      .disable_ep = dwc3_disable_ep,
                                      .ep_set_stall = dwc3_set_stall,
                                      .ep_clear_stall = dwc3_clear_stall,
                                      .get_request_size = dwc3_get_request_size,
                                      .cancel_all = dwc3_cancel_all};

static zx_status_t dwc3_set_mode(void* ctx, usb_mode_t mode) {
  auto* dwc = static_cast<dwc3_t*>(ctx);
  zx_status_t status = ZX_OK;

  if (mode == USB_MODE_OTG) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  fbl::AutoLock lock(&dwc->usb_mode_lock);

  if (dwc->usb_mode == mode) {
    return ZX_OK;
  }

  // Shutdown if we are in peripheral mode
  if (dwc->usb_mode == USB_MODE_PERIPHERAL) {
    dwc3_events_stop(dwc);
    dwc->irq_handle.reset();
    dwc3_disconnected(dwc);
    dwc3_stop(dwc);
  } else if (dwc->usb_mode == USB_MODE_HOST) {
    if (dwc->xhci_dev) {
      device_remove_deprecated(dwc->xhci_dev);
      dwc->xhci_dev = nullptr;

      if (mode == USB_MODE_PERIPHERAL) {
        dwc->start_device_on_xhci_release = true;
        return ZX_OK;
      }
    }
  }

  dwc->start_device_on_xhci_release = false;
  if (dwc->ums.ops != nullptr) {
    status = usb_mode_switch_set_mode(&dwc->ums, mode);
    if (status != ZX_OK) {
      goto fail;
    }
  }

  if (mode == USB_MODE_PERIPHERAL) {
    status = pdev_get_interrupt(&dwc->pdev, IRQ_USB3, 0, dwc->irq_handle.reset_and_get_address());
    if (status != ZX_OK) {
      zxlogf(ERROR, "dwc3_set_mode: pdev_get_interrupt failed");
      goto fail;
    }

    dwc3_start_peripheral_mode(dwc);
  } else if (mode == USB_MODE_HOST) {
    dwc3_start_host_mode(dwc);
  }

  dwc->usb_mode = mode;
  return ZX_OK;

fail:
  if (dwc->ums.ops != nullptr) {
    usb_mode_switch_set_mode(&dwc->ums, USB_MODE_NONE);
  }
  dwc->usb_mode = USB_MODE_NONE;

  return status;
}

usb_mode_switch_protocol_ops_t dwc_ums_ops = {
    .set_mode = dwc3_set_mode,
};

static void dwc3_unbind(void* ctx) {
  auto* dwc = static_cast<dwc3_t*>(ctx);
  dwc->irq_handle.destroy();
  thrd_join(dwc->irq_thread, nullptr);
  device_unbind_reply(dwc->zxdev);
}

static zx_status_t dwc3_get_protocol(void* ctx, uint32_t proto_id, void* out) {
  switch (proto_id) {
    case ZX_PROTOCOL_USB_DCI: {
      auto proto = static_cast<usb_dci_protocol_t*>(out);
      proto->ops = &dwc_dci_ops;
      proto->ctx = ctx;
      return ZX_OK;
    }
    case ZX_PROTOCOL_USB_MODE_SWITCH: {
      auto proto = static_cast<usb_mode_switch_protocol_t*>(out);
      proto->ops = &dwc_ums_ops;
      proto->ctx = ctx;
      return ZX_OK;
    }
    default:
      return ZX_ERR_NOT_SUPPORTED;
  }
}

static void dwc3_release(void* ctx) {
  auto* dwc = static_cast<dwc3_t*>(ctx);

  for (unsigned i = 0; i < countof(dwc->eps); i++) {
    dwc3_ep_fifo_release(dwc, i);
  }
  io_buffer_release(&dwc->event_buffer);
  io_buffer_release(&dwc->ep0_buffer);
  delete dwc;
}

static zx_protocol_device_t dwc3_device_ops = []() {
  zx_protocol_device_t device = {};
  device.version = DEVICE_OPS_VERSION;
  device.get_protocol = dwc3_get_protocol;
  device.release = dwc3_release;
  return device;
}();

zx_status_t dwc3_bind(void* ctx, zx_device_t* parent) {
  zxlogf(INFO, "dwc3_bind");

  auto* dwc = new dwc3_t;
  if (!dwc) {
    return ZX_ERR_NO_MEMORY;
  }
  list_initialize(&dwc->pending_completions);

  composite_protocol_t composite;
  zx_status_t status = device_get_protocol(parent, ZX_PROTOCOL_COMPOSITE, &composite);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Could not get ZX_PROTOCOL_COMPOSITE", __func__);
    goto fail;
  }
  zx_device_t* fragments[FRAGMENT_COUNT];
  size_t actual;
  composite_get_fragments(&composite, fragments, FRAGMENT_COUNT, &actual);
  if (actual != FRAGMENT_COUNT) {
    zxlogf(ERROR, "%s: Could not get fragments", __func__);
    goto fail;
  }

  status = device_get_protocol(fragments[FRAGMENT_PDEV], ZX_PROTOCOL_PDEV, &dwc->pdev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Could not get ZX_PROTOCOL_PDEV", __func__);
    goto fail;
  }

  status = device_get_protocol(fragments[FRAGMENT_UMS], ZX_PROTOCOL_USB_MODE_SWITCH, &dwc->ums);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: Could not get ZX_PROTOCOL_USB_MODE_SWITCH", __func__);
    goto fail;
  }

  status = pdev_get_bti(&dwc->pdev, 0, dwc->bti_handle.reset_and_get_address());
  if (status != ZX_OK) {
    goto fail;
  }

  for (uint8_t i = 0; i < countof(dwc->eps); i++) {
    dwc3_endpoint_t* ep = &dwc->eps[i];
    ep->ep_num = i;
    list_initialize(&ep->queued_reqs);
  }
  dwc->parent = parent;
  dwc->pdev_dev = fragments[FRAGMENT_PDEV];
  dwc->usb_mode = USB_MODE_NONE;

  mmio_buffer_t mmio;
  status = pdev_map_mmio_buffer(&dwc->pdev, MMIO_USB3OTG, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio);
  if (status != ZX_OK) {
    zxlogf(ERROR, "dwc3_bind: pdev_map_mmio_buffer failed");
    goto fail;
  }
  dwc->mmio = ddk::MmioBuffer(mmio);

  status = io_buffer_init(&dwc->event_buffer, dwc->bti_handle.get(), EVENT_BUFFER_SIZE,
                          IO_BUFFER_RO | IO_BUFFER_CONTIG);
  if (status != ZX_OK) {
    zxlogf(ERROR, "dwc3_bind: io_buffer_init failed");
    goto fail;
  }
  io_buffer_cache_flush(&dwc->event_buffer, 0, EVENT_BUFFER_SIZE);

  status = io_buffer_init(&dwc->ep0_buffer, dwc->bti_handle.get(), UINT16_MAX,
                          IO_BUFFER_RW | IO_BUFFER_CONTIG);
  if (status != ZX_OK) {
    zxlogf(ERROR, "dwc3_bind: io_buffer_init failed");
    goto fail;
  }

  status = dwc3_ep0_init(dwc);
  if (status != ZX_OK) {
    zxlogf(ERROR, "dwc3_bind: dwc3_ep_init failed");
    goto fail;
  }

  {
    device_add_args_t args = {};
    args.version = DEVICE_ADD_ARGS_VERSION;
    args.name = "dwc3";
    args.ctx = dwc;
    args.ops = &dwc3_device_ops;
    args.proto_id = ZX_PROTOCOL_USB_DCI;
    args.proto_ops = &dwc_dci_ops,

    status = device_add(parent, &args, &dwc->zxdev);
    if (status != ZX_OK) {
      goto fail;
    }
  }

  return ZX_OK;

fail:
  zxlogf(ERROR, "dwc3_bind failed %d", status);
  dwc3_release(dwc);
  return status;
}

static constexpr zx_driver_ops_t dwc3_driver_ops = []() {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = dwc3_bind;
  return ops;
}();

// clang-format off
ZIRCON_DRIVER_BEGIN(dwc3, dwc3_driver_ops, "zircon", "0.1", 4)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_COMPOSITE),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GENERIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_GENERIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_USB_DWC3),
ZIRCON_DRIVER_END(dwc3)
    // clang-format on
