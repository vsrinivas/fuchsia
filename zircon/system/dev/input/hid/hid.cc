// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hid.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/assert.h>
#include <zircon/listnode.h>

#include <algorithm>
#include <memory>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/hidbus.h>
#include <ddk/trace/event.h>
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <hid/boot.h>

namespace hid_driver {

// Until we do full HID parsing, we put mouse and keyboard devices into boot
// protocol mode. In particular, a mouse will always send 3 byte reports (see
// ddk/protocol/input.h for the format). This macro sets FIDL return values for
// boot mouse devices to reflect the boot protocol, rather than what the device
// itself reports.
#define BOOT_MOUSE_HACK 1
#define BOOT_KBD_HACK 1

static constexpr uint32_t kHidFlagsDead = (1 << 0);
static constexpr uint32_t kHidFlagsWriteFailed = (1 << 1);

static constexpr input_report_size_t BitsToBytes(input_report_size_t bits) {
  return static_cast<input_report_size_t>(((bits + 7) / 8));
}

static constexpr uint64_t hid_report_trace_id(uint32_t instance_id, uint64_t report_id) {
  return (report_id << 32) | instance_id;
}

// Convenience functions for calling hidbus protocol functions

input_report_size_t HidDevice::GetReportSizeById(input_report_id_t id,
                                                 fuchsia_hardware_input_ReportType type) {
  for (size_t i = 0; i < num_reports_; i++) {
    // If we have more than one report, get the report with the right id. If we only have
    // one report, then always match that report.
    if ((sizes_[i].id == id) || (num_reports_ == 1)) {
      switch (type) {
        case fuchsia_hardware_input_ReportType_INPUT:
          return BitsToBytes(sizes_[i].in_size);
        case fuchsia_hardware_input_ReportType_OUTPUT:
          return BitsToBytes(sizes_[i].out_size);
        case fuchsia_hardware_input_ReportType_FEATURE:
          return BitsToBytes(sizes_[i].feat_size);
      }
    }
  }

  return 0;
}

fuchsia_hardware_input_BootProtocol HidDevice::GetBootProtocol() {
  if (info_.device_class == HID_DEVICE_CLASS_KBD ||
      info_.device_class == HID_DEVICE_CLASS_KBD_POINTER) {
    return fuchsia_hardware_input_BootProtocol_KBD;
  } else if (info_.device_class == HID_DEVICE_CLASS_POINTER) {
    return fuchsia_hardware_input_BootProtocol_MOUSE;
  }
  return fuchsia_hardware_input_BootProtocol_NONE;
}

void HidDevice::RemoveHidInstanceFromList(HidInstance* instance) {
  fbl::AutoLock lock(&instance_lock_);

  // TODO(dgilhooley): refcount the base device and call stop if no instances are open
  instance_list_.erase_if(
      [instance_ = instance](const HidInstance iter) { return iter.zxdev == instance_->zxdev; });
}

void HidDevice::GetReportIds(uint8_t* report_ids) {
  for (size_t i = 0; i < num_reports_; i++) {
    report_ids[i] = sizes_[i].id;
  }
}

input_report_size_t HidDevice::GetMaxInputReportSize() {
  input_report_size_t size = 0;
  for (size_t i = 0; i < num_reports_; i++) {
    if (sizes_[i].in_size > size)
      size = sizes_[i].in_size;
  }
  return BitsToBytes(size);
}

zx_status_t hid_read_instance(void* ctx, void* buf, size_t count, zx_off_t off, size_t* actual) {
  TRACE_DURATION("input", "HID Read Instance");
  zx_status_t status = ZX_OK;

  auto hid = reinterpret_cast<HidInstance*>(ctx);

  if (hid->flags & kHidFlagsDead) {
    return ZX_ERR_PEER_CLOSED;
  }

  size_t left;
  mtx_lock(&hid->fifo.lock);
  size_t xfer;
  uint8_t rpt_id;
  ssize_t r = zx_hid_fifo_peek(&hid->fifo, &rpt_id);
  if (r < 1) {
    // fifo is empty
    mtx_unlock(&hid->fifo.lock);
    return ZX_ERR_SHOULD_WAIT;
  }

  xfer = hid->base->GetReportSizeById(rpt_id, fuchsia_hardware_input_ReportType_INPUT);
  if (xfer == 0) {
    zxlogf(ERROR, "error reading hid device: unknown report id (%u)!\n", rpt_id);
    mtx_unlock(&hid->fifo.lock);
    return ZX_ERR_BAD_STATE;
  }

  if (xfer > count) {
    zxlogf(SPEW, "next report: %zd, read count: %zd\n", xfer, count);
    mtx_unlock(&hid->fifo.lock);
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  r = zx_hid_fifo_read(&hid->fifo, buf, xfer);
  left = zx_hid_fifo_size(&hid->fifo);
  if (left == 0) {
    device_state_clr(hid->zxdev, DEV_STATE_READABLE);
  }
  mtx_unlock(&hid->fifo.lock);
  if (r > 0) {
    TRACE_FLOW_STEP("input", "hid_report", hid_report_trace_id(hid->trace_id, hid->reports_read));
    ++hid->reports_read;
    *actual = r;
    r = ZX_OK;
  } else if (r == 0) {
    status = ZX_ERR_SHOULD_WAIT;
  }
  return status;
}

static zx_status_t hid_close_instance(void* ctx, uint32_t flags) {
  auto hid = reinterpret_cast<HidInstance*>(ctx);

  hid->flags |= kHidFlagsDead;
  hid->base->RemoveHidInstanceFromList(hid);
  return ZX_OK;
}

static void hid_release_instance(void* ctx) {
  auto hid = reinterpret_cast<HidInstance*>(ctx);
  delete hid;
}

static zx_status_t fidl_GetBootProtocol(void* ctx, fidl_txn_t* txn) {
  auto hid = reinterpret_cast<HidInstance*>(ctx);
  return fuchsia_hardware_input_DeviceGetBootProtocol_reply(txn, hid->base->GetBootProtocol());
}

static zx_status_t fidl_GetReportDescSize(void* ctx, fidl_txn_t* txn) {
  auto hid = reinterpret_cast<HidInstance*>(ctx);
  return fuchsia_hardware_input_DeviceGetReportDescSize_reply(
      txn, static_cast<uint16_t>(hid->base->GetReportDescLen()));
}

static zx_status_t fidl_GetReportDesc(void* ctx, fidl_txn_t* txn) {
  auto hid = reinterpret_cast<HidInstance*>(ctx);
  return fuchsia_hardware_input_DeviceGetReportDesc_reply(txn, hid->base->GetReportDesc(),
                                                          hid->base->GetReportDescLen());
}

static zx_status_t fidl_GetNumReports(void* ctx, fidl_txn_t* txn) {
  auto hid = reinterpret_cast<HidInstance*>(ctx);
  return fuchsia_hardware_input_DeviceGetNumReports_reply(
      txn, static_cast<uint16_t>(hid->base->GetNumReports()));
}

static zx_status_t fidl_GetReportIds(void* ctx, fidl_txn_t* txn) {
  auto hid = reinterpret_cast<HidInstance*>(ctx);
  uint8_t report[fuchsia_hardware_input_MAX_REPORT_IDS];
  hid->base->GetReportIds(report);
  return fuchsia_hardware_input_DeviceGetReportIds_reply(txn, report, hid->base->GetNumReports());
}

static zx_status_t fidl_GetReportSize(void* ctx, fuchsia_hardware_input_ReportType type, uint8_t id,
                                      fidl_txn_t* txn) {
  auto hid = reinterpret_cast<HidInstance*>(ctx);
  input_report_size_t size = hid->base->GetReportSizeById(id, type);
  return fuchsia_hardware_input_DeviceGetReportSize_reply(txn, size == 0 ? ZX_ERR_NOT_FOUND : ZX_OK,
                                                          size);
}

static zx_status_t fidl_GetMaxInputReportSize(void* ctx, fidl_txn_t* txn) {
  auto hid = reinterpret_cast<HidInstance*>(ctx);
  return fuchsia_hardware_input_DeviceGetMaxInputReportSize_reply(
      txn, hid->base->GetMaxInputReportSize());
}

static zx_status_t fidl_GetReport(void* ctx, fuchsia_hardware_input_ReportType type, uint8_t id,
                                  fidl_txn_t* txn) {
  auto hid = reinterpret_cast<HidInstance*>(ctx);
  input_report_size_t needed = hid->base->GetReportSizeById(id, type);
  if (needed == 0) {
    return fuchsia_hardware_input_DeviceGetReport_reply(txn, ZX_ERR_NOT_FOUND, NULL, 0);
  }

  uint8_t report[needed];
  size_t actual = 0;
  zx_status_t status = hid->base->GetHidbusProtocol()->GetReport(type, id, report, needed, &actual);
  return fuchsia_hardware_input_DeviceGetReport_reply(txn, status, report, actual);
}

static zx_status_t fidl_SetReport(void* ctx, fuchsia_hardware_input_ReportType type, uint8_t id,
                                  const uint8_t* report, size_t report_len, fidl_txn_t* txn) {
  auto hid = reinterpret_cast<HidInstance*>(ctx);
  input_report_size_t needed = hid->base->GetReportSizeById(id, type);
  if (needed < report_len) {
    return fuchsia_hardware_input_DeviceSetReport_reply(txn, ZX_ERR_BUFFER_TOO_SMALL);
  }
  zx_status_t status = hid->base->GetHidbusProtocol()->SetReport(type, id, report, report_len);
  return fuchsia_hardware_input_DeviceSetReport_reply(txn, status);
}

static zx_status_t fidl_SetTraceId(void* ctx, uint32_t id) {
  auto hid = reinterpret_cast<HidInstance*>(ctx);
  hid->trace_id = id;
  return ZX_OK;
}

static fuchsia_hardware_input_Device_ops_t fidl_ops = {
    .GetBootProtocol = fidl_GetBootProtocol,
    .GetReportDescSize = fidl_GetReportDescSize,
    .GetReportDesc = fidl_GetReportDesc,
    .GetNumReports = fidl_GetNumReports,
    .GetReportIds = fidl_GetReportIds,
    .GetReportSize = fidl_GetReportSize,
    .GetMaxInputReportSize = fidl_GetMaxInputReportSize,
    .GetReport = fidl_GetReport,
    .SetReport = fidl_SetReport,
    .SetTraceId = fidl_SetTraceId,
};

static zx_status_t hid_message_instance(void* ctx, fidl_msg_t* msg, fidl_txn_t* txn) {
  auto hid = reinterpret_cast<HidInstance*>(ctx);
  if (hid->flags & kHidFlagsDead) {
    return ZX_ERR_PEER_CLOSED;
  }

  return fuchsia_hardware_input_Device_dispatch(ctx, txn, msg, &fidl_ops);
}

zx_protocol_device_t hid_instance_proto = []() {
  zx_protocol_device_t proto = {};
  proto.version = DEVICE_OPS_VERSION;
  proto.read = hid_read_instance;
  proto.close = hid_close_instance;
  proto.message = hid_message_instance;
  proto.release = hid_release_instance;
  return proto;
}();

void hid_reports_set_boot_mode(hid_reports_t* reports) {
  reports->num_reports = 1;
  reports->sizes[0].id = 0;
  reports->sizes[0].in_size = 24;
  reports->sizes[0].out_size = 0;
  reports->sizes[0].feat_size = 0;
  reports->has_rpt_id = false;
}

zx_status_t HidDevice::ProcessReportDescriptor() {
  hid_reports_t reports;
  reports.num_reports = 0;
  reports.sizes_len = kHidMaxReportIds;
  reports.sizes = sizes_.data();
  reports.has_rpt_id = false;

  zx_status_t status = hid_lib_parse_reports(hid_report_desc_, hid_report_desc_len_, &reports);
  if (status != ZX_OK) {
    return status;
  }

  num_reports_ = reports.num_reports;
  ZX_DEBUG_ASSERT(num_reports_ <= countof(sizes_));
  return status;
}

void HidDevice::ReleaseReassemblyBuffer() {
  if (rbuf_ != NULL) {
    free(rbuf_);
  }

  rbuf_ = NULL;
  rbuf_size_ = 0;
  rbuf_filled_ = 0;
  rbuf_needed_ = 0;
}

zx_status_t HidDevice::InitReassemblyBuffer() {
  ZX_DEBUG_ASSERT(rbuf_ == NULL);
  ZX_DEBUG_ASSERT(rbuf_size_ == 0);
  ZX_DEBUG_ASSERT(rbuf_filled_ == 0);
  ZX_DEBUG_ASSERT(rbuf_needed_ == 0);

  // TODO(johngro) : Take into account the underlying transport's ability to
  // deliver payloads.  For example, if this is a USB HID device operating at
  // full speed, we can expect it to deliver up to 64 bytes at a time.  If the
  // maximum HID input report size is only 60 bytes, we should not need a
  // reassembly buffer.
  input_report_size_t max_report_size = GetMaxInputReportSize();
  rbuf_ = static_cast<uint8_t*>(malloc(max_report_size));
  if (rbuf_ == NULL) {
    return ZX_ERR_NO_MEMORY;
  }

  rbuf_size_ = max_report_size;
  return ZX_OK;
}

void HidDevice::DdkRelease() {
  if (hid_report_desc_) {
    free(hid_report_desc_);
    hid_report_desc_ = NULL;
    hid_report_desc_len_ = 0;
  }
  ReleaseReassemblyBuffer();
  delete this;
}

zx_status_t HidDevice::DdkOpen(zx_device_t** dev_out, uint32_t flags) {
  HidInstance* inst = new HidInstance();
  if (inst == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }

  zx_hid_fifo_init(&inst->fifo);

  device_add_args_t args = {};
  args.version = DEVICE_ADD_ARGS_VERSION;
  args.name = "hid";
  args.ctx = inst;
  args.ops = &hid_instance_proto;
  args.proto_id = ZX_PROTOCOL_HID_DEVICE;
  args.flags = DEVICE_ADD_INSTANCE;

  zx_status_t status = device_add(zxdev_, &args, &inst->zxdev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "hid: error creating instance %d\n", status);
    delete inst;
    return status;
  }
  inst->base = this;

  {
    fbl::AutoLock lock(&instance_lock_);
    instance_list_.push_front(std::move(inst));
  }

  *dev_out = inst->zxdev;
  return ZX_OK;
}

void HidDevice::DdkUnbind() {
  {
    fbl::AutoLock lock(&instance_lock_);
    for (auto& instance : instance_list_) {
      instance.flags |= kHidFlagsDead;
      device_state_set(instance.zxdev, DEV_STATE_READABLE);
    }
  }
  device_remove(zxdev_);
}

void HidDevice::IoQueue(void* cookie, const void* _buf, size_t len) {
  const uint8_t* buf = static_cast<const uint8_t*>(_buf);
  HidDevice* hid = static_cast<HidDevice*>(cookie);

  TRACE_DURATION("input", "HID IO Queue");

  fbl::AutoLock lock(&hid->instance_lock_);

  while (len) {
    // Start by figuring out if this payload either completes a partially
    // assembled input report or represents an entire input buffer report on
    // its own.
    const uint8_t* rbuf;
    size_t rlen;
    size_t consumed;

    if (hid->rbuf_needed_) {
      // Reassembly is in progress, just continue the process.
      consumed = std::min(len, hid->rbuf_needed_);
      ZX_DEBUG_ASSERT(hid->rbuf_size_ >= hid->rbuf_filled_);
      ZX_DEBUG_ASSERT((hid->rbuf_size_ - hid->rbuf_filled_) >= consumed);

      memcpy(hid->rbuf_ + hid->rbuf_filled_, buf, consumed);

      if (consumed == hid->rbuf_needed_) {
        // reassembly finished.  Reset the bookkeeping and deliver the
        // payload.
        rbuf = hid->rbuf_;
        rlen = hid->rbuf_filled_ + consumed;
        hid->rbuf_filled_ = 0;
        hid->rbuf_needed_ = 0;
      } else {
        // We have not finished the process yet.  Update the bookkeeping
        // and get out.
        hid->rbuf_filled_ += consumed;
        hid->rbuf_needed_ -= consumed;
        break;
      }
    } else {
      // No reassembly is in progress.  Start by identifying this report's
      // size.
      size_t rpt_sz = hid->GetReportSizeById(buf[0], fuchsia_hardware_input_ReportType_INPUT);

      // If we don't recognize this report ID, we are in trouble.  Drop
      // the rest of this payload and hope that the next one gets us back
      // on track.
      if (!rpt_sz) {
        zxlogf(ERROR, "%s: failed to find input report size (report id %u)\n", hid->name_.data(),
               buf[0]);
        break;
      }

      // Is the entire report present in this payload?  If so, just go
      // ahead an deliver it directly from the input buffer.
      if (len >= rpt_sz) {
        rbuf = buf;
        consumed = rlen = rpt_sz;
      } else {
        // Looks likes our report is fragmented over multiple buffers.
        // Start the process of reassembly and get out.
        ZX_DEBUG_ASSERT(hid->rbuf_ != NULL);
        ZX_DEBUG_ASSERT(hid->rbuf_size_ >= rpt_sz);
        memcpy(hid->rbuf_, buf, len);
        hid->rbuf_filled_ = len;
        hid->rbuf_needed_ = rpt_sz - len;
        break;
      }
    }

    ZX_DEBUG_ASSERT(rbuf != NULL);
    ZX_DEBUG_ASSERT(consumed <= len);
    buf += consumed;
    len -= consumed;

    for (auto& instance : hid->instance_list_) {
      mtx_lock(&instance.fifo.lock);
      bool was_empty = zx_hid_fifo_size(&instance.fifo) == 0;
      ssize_t wrote = zx_hid_fifo_write(&instance.fifo, rbuf, rlen);

      if (wrote <= 0) {
        if (!(instance.flags & kHidFlagsWriteFailed)) {
          zxlogf(ERROR, "%s: could not write to hid fifo (ret=%zd)\n", hid->name_.data(), wrote);
          instance.flags |= kHidFlagsWriteFailed;
        }
      } else {
        TRACE_FLOW_BEGIN("input", "hid_report",
                         hid_report_trace_id(instance.trace_id, instance.reports_written));
        ++instance.reports_written;
        instance.flags &= ~kHidFlagsWriteFailed;
        if (was_empty) {
          device_state_set(instance.zxdev, DEV_STATE_READABLE);
        }
      }
      mtx_unlock(&instance.fifo.lock);
    }
  }
}

hidbus_ifc_protocol_ops_t hid_ifc_ops = {
    .io_queue = HidDevice::IoQueue,
};

zx_status_t HidDevice::SetReportDescriptor() {
  zx_status_t status = hidbus_.GetDescriptor(HID_DESCRIPTION_TYPE_REPORT,
                                             reinterpret_cast<void**>(&hid_report_desc_),
                                             &hid_report_desc_len_);
  if (status != ZX_OK) {
    return status;
  }

  if (!info_.boot_device) {
    return ZX_OK;
  }

  hid_protocol_t protocol;
  status = hidbus_.GetProtocol(&protocol);
  if (status != ZX_OK) {
    if (status == ZX_ERR_NOT_SUPPORTED) {
      status = ZX_OK;
    }
    return status;
  }

  // Only continue if the device was put into the boot protocol.
  if (protocol != HID_PROTOCOL_BOOT) {
    return ZX_OK;
  }

  // If we are a boot protocol kbd, we need to use the right HID descriptor.
  if (info_.device_class == HID_DEVICE_CLASS_KBD) {
    const uint8_t* boot_kbd_desc = get_boot_kbd_report_desc(&hid_report_desc_len_);

    free(hid_report_desc_);
    hid_report_desc_ = static_cast<uint8_t*>(malloc(hid_report_desc_len_));
    if (!hid_report_desc_) {
      status = ZX_ERR_NO_MEMORY;
      return status;
    }
    memcpy(hid_report_desc_, boot_kbd_desc, hid_report_desc_len_);

    // Disable numlock
    uint8_t zero = 0;
    hidbus_.SetReport(HID_REPORT_TYPE_OUTPUT, 0, &zero, sizeof(zero));
    // ignore failure for now
  }

  // If we are a boot protocol pointer, we need to use the right HID descriptor.
  if (info_.device_class == HID_DEVICE_CLASS_POINTER) {
    const uint8_t* boot_mouse_desc = get_boot_mouse_report_desc(&hid_report_desc_len_);

    free(hid_report_desc_);
    hid_report_desc_ = static_cast<uint8_t*>(malloc(hid_report_desc_len_));
    if (!hid_report_desc_) {
      status = ZX_ERR_NO_MEMORY;
      return status;
    }
    memcpy(hid_report_desc_, boot_mouse_desc, hid_report_desc_len_);
  }

  return ZX_OK;
}

zx_status_t HidDevice::Bind(ddk::HidbusProtocolClient hidbus_proto) {
  hidbus_ = std::move(hidbus_proto);
  zx_status_t status = ZX_OK;

  if ((status = hidbus_.Query(0, &info_)) < 0) {
    zxlogf(ERROR, "hid: bind: hidbus query failed: %d\n", status);
    return status;
  }

  snprintf(name_.data(), name_.size(), "hid-device-%03d", info_.dev_num);
  name_[ZX_DEVICE_NAME_MAX] = 0;

  if (info_.boot_device) {
#if BOOT_KBD_HACK
    if (info_.device_class == HID_DEVICE_CLASS_KBD) {
      status = hidbus_.SetProtocol(HID_PROTOCOL_BOOT);
      if (status != ZX_OK) {
        zxlogf(ERROR, "hid: could not put HID device into boot protocol: %d\n", status);
        return status;
      }
    }
#endif
#if BOOT_MOUSE_HACK
    if (info_.device_class == HID_DEVICE_CLASS_POINTER) {
      status = hidbus_.SetProtocol(HID_PROTOCOL_BOOT);
      if (status != ZX_OK) {
        zxlogf(ERROR, "hid: could not put HID device into boot protocol: %d\n", status);
        return status;
      }
    }
#endif
  }

  status = SetReportDescriptor();
  if (status != ZX_OK) {
    zxlogf(ERROR, "hid: could not retrieve HID report descriptor: %d\n", status);
    return status;
  }

  status = ProcessReportDescriptor();
  if (status != ZX_OK) {
    zxlogf(ERROR, "hid: could not parse hid report descriptor: %d\n", status);
    return status;
  }

  status = InitReassemblyBuffer();
  if (status != ZX_OK) {
    zxlogf(ERROR, "hid: failed to initialize reassembly buffer: %d\n", status);
    return status;
  }

  // TODO: delay calling start until we've been opened by someone
  status = hidbus_.Start(this, &hid_ifc_ops);
  if (status != ZX_OK) {
    zxlogf(ERROR, "hid: could not start hid device: %d\n", status);
    ReleaseReassemblyBuffer();
    return status;
  }

  status = hidbus_.SetIdle(0, 0);
  if (status != ZX_OK) {
    zxlogf(TRACE, "hid: [W] set_idle failed for %s: %d\n", name_.data(), status);
    // continue anyway
  }

  status = DdkAdd(name_.data());
  if (status != ZX_OK) {
    zxlogf(ERROR, "hid: device_add failed for HID device: %d\n", status);
    ReleaseReassemblyBuffer();
    return status;
  }

  return ZX_OK;
}

static zx_status_t hid_bind(void* ctx, zx_device_t* parent) {
  zx_status_t status;
  auto dev = std::make_unique<HidDevice>(parent);

  hidbus_protocol_t hidbus;
  if (device_get_protocol(parent, ZX_PROTOCOL_HIDBUS, &hidbus)) {
    zxlogf(ERROR, "hid: bind: no hidbus protocol\n");
    return ZX_ERR_INTERNAL;
  }

  ddk::HidbusProtocolClient client = ddk::HidbusProtocolClient(&hidbus);
  status = dev->Bind(std::move(client));
  if (status == ZX_OK) {
    // devmgr is now in charge of the memory for dev.
    __UNUSED auto ptr = dev.release();
  }
  return status;
}

static zx_driver_ops_t hid_driver_ops = []() {
  zx_driver_ops_t ops;
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = hid_bind;
  return ops;
}();

}  // namespace hid_driver

// clang-format off
ZIRCON_DRIVER_BEGIN(hid, hid_driver::hid_driver_ops, "zircon", "0.1", 1)
  BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_HIDBUS),
ZIRCON_DRIVER_END(hid)
    // clang-format on
