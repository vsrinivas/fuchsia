// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hid.h"

#include <assert.h>
#include <fuchsia/hardware/hidbus/c/banjo.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/trace/event.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/assert.h>
#include <zircon/syscalls.h>

#include <algorithm>
#include <map>
#include <memory>

#include <bind/fuchsia/hid/cpp/bind.h>
#include <fbl/auto_lock.h>
#include <hid/boot.h>

#include "fuchsia/hardware/hiddevice/c/banjo.h"
#include "src/lib/listnode/listnode.h"
#include "src/ui/input/drivers/hid/hid_bind.h"

namespace hid_driver {

namespace {

#define MAKE_KEY(PAGE, USAGE)                                  \
  HidPageUsage {                                               \
    .page = static_cast<uint16_t>(hid::usage::Page::k##PAGE),  \
    .usage = static_cast<uint32_t>(hid::usage::PAGE::k##USAGE) \
  }

std::map<HidPageUsage, const std::string> kBindPropKeyMap({
    {MAKE_KEY(Consumer, ConsumerControl), bind_fuchsia_hid::CONSUMER__CONSUMER_CONTROL},
    {MAKE_KEY(Digitizer, TouchPad), bind_fuchsia_hid::DIGITIZER__TOUCH_PAD},
    {MAKE_KEY(Digitizer, TouchScreen), bind_fuchsia_hid::DIGITIZER__TOUCH_SCREEN},
    {MAKE_KEY(Digitizer, TouchScreenConfiguration),
     bind_fuchsia_hid::DIGITIZER__TOUCH_SCREEN_CONFIGURATION},
    {MAKE_KEY(FidoAlliance, Undefined), bind_fuchsia_hid::FIDO_ALLIANCE},  // only match page
    {MAKE_KEY(GenericDesktop, Keyboard), bind_fuchsia_hid::GENERIC_DESKTOP__KEYBOARD},
    {MAKE_KEY(GenericDesktop, Mouse), bind_fuchsia_hid::GENERIC_DESKTOP__MOUSE},
    {MAKE_KEY(Sensor, Undefined), bind_fuchsia_hid::SENSOR},  // only match page
});

zx::result<HidPageUsage> FindProp(HidPageUsage key) {
  if (kBindPropKeyMap.find(key) != kBindPropKeyMap.end()) {
    return zx::ok(key);
  }

  // Only match page for kBindPropKeyMap entries where usage is Undefined
  for (auto const& [page_usage, value] : kBindPropKeyMap) {
    if (page_usage.usage) {
      continue;
    }

    if (page_usage.page == key.page) {  // pages match
      return zx::ok(page_usage);
    }
  }
  return zx::error(ZX_ERR_NOT_FOUND);
}

}  // namespace

void HidDevice::ParseUsagePage(const hid::ReportDescriptor* descriptor) {
  auto collection = hid::GetAppCollection(&descriptor->input_fields[0]);
  if (collection == nullptr) {
    return;
  }

  page_usage_.insert(
      HidPageUsage{.page = collection->usage.page, .usage = collection->usage.usage});
}

size_t HidDevice::GetReportSizeById(input_report_id_t id, ReportType type) {
  for (size_t i = 0; i < parsed_hid_desc_->rep_count; i++) {
    // If we have more than one report, get the report with the right id. If we only have
    // one report, then always match that report.
    if ((parsed_hid_desc_->report[i].report_id == id) || (parsed_hid_desc_->rep_count == 1)) {
      switch (type) {
        case ReportType::kInput:
          return parsed_hid_desc_->report[i].input_byte_sz;
        case ReportType::kOutput:
          return parsed_hid_desc_->report[i].output_byte_sz;
        case ReportType::kFeature:
          return parsed_hid_desc_->report[i].feature_byte_sz;
      }
    }
  }

  return 0;
}

BootProtocol HidDevice::GetBootProtocol() {
  if (info_.device_class == HID_DEVICE_CLASS_KBD ||
      info_.device_class == HID_DEVICE_CLASS_KBD_POINTER) {
    return BootProtocol::kKbd;
  } else if (info_.device_class == HID_DEVICE_CLASS_POINTER) {
    return BootProtocol::kMouse;
  }
  return BootProtocol::kNone;
}

void HidDevice::RemoveHidInstanceFromList(HidInstance* instance) {
  fbl::AutoLock lock(&instance_lock_);

  // TODO(dgilhooley): refcount the base device and call stop if no instances are open
  for (auto& iter : instance_list_) {
    if (iter.zxdev() == instance->zxdev()) {
      instance_list_.erase(iter);
      break;
    }
  }
}

size_t HidDevice::GetMaxInputReportSize() {
  size_t size = 0;
  for (size_t i = 0; i < parsed_hid_desc_->rep_count; i++) {
    if (parsed_hid_desc_->report[i].input_byte_sz > size)
      size = parsed_hid_desc_->report[i].input_byte_sz;
  }
  return size;
}

zx_status_t HidDevice::ProcessReportDescriptor() {
  hid::ParseResult res = hid::ParseReportDescriptor(hid_report_desc_.data(),
                                                    hid_report_desc_.size(), &parsed_hid_desc_);
  if (res != hid::ParseResult::kParseOk) {
    return ZX_ERR_INTERNAL;
  }

  size_t num_reports = 0;
  for (size_t i = 0; i < parsed_hid_desc_->rep_count; i++) {
    const hid::ReportDescriptor* desc = &parsed_hid_desc_->report[i];
    if (desc->input_count != 0) {
      num_reports++;

      ParseUsagePage(desc);
    }
    if (desc->output_count != 0) {
      num_reports++;
    }
    if (desc->feature_count != 0) {
      num_reports++;
    }
  }
  num_reports_ = num_reports;
  return ZX_OK;
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
  size_t max_report_size = GetMaxInputReportSize();
  rbuf_ = static_cast<uint8_t*>(malloc(max_report_size));
  if (rbuf_ == NULL) {
    return ZX_ERR_NO_MEMORY;
  }

  rbuf_size_ = max_report_size;
  return ZX_OK;
}

void HidDevice::DdkRelease() {
  ReleaseReassemblyBuffer();
  if (parsed_hid_desc_) {
    FreeDeviceDescriptor(parsed_hid_desc_);
  }
  delete this;
}

zx_status_t HidDevice::DdkOpen(zx_device_t** dev_out, uint32_t flags) {
  auto inst = std::make_unique<HidInstance>(zxdev());
  zx_status_t status = inst->Bind(this);
  if (status != ZX_OK) {
    return status;
  }

  {
    fbl::AutoLock lock(&instance_lock_);
    instance_list_.push_front(inst.get());
  }

  *dev_out = inst->zxdev();

  // devmgr is now in charge of the memory for inst.
  __UNUSED auto ptr = inst.release();
  return ZX_OK;
}

void HidDevice::DdkUnbind(ddk::UnbindTxn txn) {
  {
    fbl::AutoLock lock(&instance_lock_);
    for (auto& instance : instance_list_) {
      instance.CloseInstance();
    }
  }
  txn.Reply();
}

void HidDevice::IoQueue(void* cookie, const uint8_t* buf, size_t len, zx_time_t time) {
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
      size_t rpt_sz = hid->GetReportSizeById(buf[0], ReportType::kInput);

      // If we don't recognize this report ID, we are in trouble.  Drop
      // the rest of this payload and hope that the next one gets us back
      // on track.
      if (!rpt_sz) {
        zxlogf(DEBUG, "%s: failed to find input report size (report id %u)", hid->name_.data(),
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
      instance.WriteToFifo(rbuf, rlen, time);
    }

    {
      fbl::AutoLock lock(&hid->listener_lock_);
      if (hid->report_listener_.is_valid()) {
        hid->report_listener_.ReceiveReport(rbuf, rlen, time);
      }
    }
  }
}

zx_status_t HidDevice::HidDeviceRegisterListener(const hid_report_listener_protocol_t* listener) {
  fbl::AutoLock lock(&listener_lock_);

  if (report_listener_.is_valid()) {
    return ZX_ERR_ALREADY_BOUND;
  }
  report_listener_ = ddk::HidReportListenerProtocolClient(listener);

  return ZX_OK;
}

void HidDevice::HidDeviceUnregisterListener() {
  fbl::AutoLock lock(&listener_lock_);
  report_listener_.clear();
}

void HidDevice::HidDeviceGetHidDeviceInfo(hid_device_info_t* out_info) {
  out_info->vendor_id = info_.vendor_id;
  out_info->product_id = info_.product_id;
  out_info->version = info_.version;
}

zx_status_t HidDevice::HidDeviceGetDescriptor(uint8_t* out_descriptor_data, size_t descriptor_count,
                                              size_t* out_descriptor_actual) {
  if (descriptor_count < hid_report_desc_.size()) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }
  memcpy(out_descriptor_data, hid_report_desc_.data(), hid_report_desc_.size());
  *out_descriptor_actual = hid_report_desc_.size();
  return ZX_OK;
}

zx_status_t HidDevice::HidDeviceGetReport(hid_report_type_t rpt_type, uint8_t rpt_id,
                                          uint8_t* out_report_data, size_t report_count,
                                          size_t* out_report_actual) {
  size_t needed = GetReportSizeById(rpt_id, static_cast<ReportType>(rpt_type));
  if (needed == 0) {
    return ZX_ERR_NOT_FOUND;
  }
  if (needed > report_count) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }
  if (needed > HID_MAX_REPORT_LEN) {
    zxlogf(ERROR, "hid: GetReport: Report size 0x%lx larger than max size 0x%x", needed,
           HID_MAX_REPORT_LEN);
    return ZX_ERR_INTERNAL;
  }

  uint8_t report[HID_MAX_REPORT_LEN];
  size_t actual = 0;
  zx_status_t status = hidbus_.GetReport(rpt_type, rpt_id, report, needed, &actual);
  if (status != ZX_OK) {
    return status;
  }
  memcpy(out_report_data, report, actual);
  *out_report_actual = actual;

  return ZX_OK;
}

zx_status_t HidDevice::HidDeviceSetReport(hid_report_type_t rpt_type, uint8_t rpt_id,
                                          const uint8_t* report_data, size_t report_count) {
  size_t needed = GetReportSizeById(rpt_id, static_cast<ReportType>(rpt_type));
  if (needed < report_count) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }
  zx_status_t status = hidbus_.SetReport(rpt_type, rpt_id, report_data, report_count);
  return status;
}

hidbus_ifc_protocol_ops_t hid_ifc_ops = {
    .io_queue = HidDevice::IoQueue,
};

zx_status_t HidDevice::SetReportDescriptor() {
  hid_report_desc_.resize(HID_MAX_DESC_LEN);
  size_t actual;
  zx_status_t status = hidbus_.GetDescriptor(HID_DESCRIPTION_TYPE_REPORT, hid_report_desc_.data(),
                                             hid_report_desc_.size(), &actual);
  if (status != ZX_OK) {
    return status;
  }
  hid_report_desc_.resize(actual);

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
    const uint8_t* boot_kbd_desc = get_boot_kbd_report_desc(&actual);
    hid_report_desc_.resize(actual);
    memcpy(hid_report_desc_.data(), boot_kbd_desc, actual);

    // Disable numlock
    uint8_t zero = 0;
    hidbus_.SetReport(HID_REPORT_TYPE_OUTPUT, 0, &zero, sizeof(zero));
    // ignore failure for now
  }

  // If we are a boot protocol pointer, we need to use the right HID descriptor.
  if (info_.device_class == HID_DEVICE_CLASS_POINTER) {
    const uint8_t* boot_mouse_desc = get_boot_mouse_report_desc(&actual);

    hid_report_desc_.resize(actual);
    memcpy(hid_report_desc_.data(), boot_mouse_desc, actual);
  }

  return ZX_OK;
}

const char* HidDevice::GetName() { return name_.data(); }

zx_status_t HidDevice::Bind(ddk::HidbusProtocolClient hidbus_proto) {
  hidbus_ = std::move(hidbus_proto);
  zx_status_t status = ZX_OK;

  if ((status = hidbus_.Query(0, &info_)) < 0) {
    zxlogf(ERROR, "hid: bind: hidbus query failed: %d", status);
    return status;
  }

  snprintf(name_.data(), name_.size(), "hid-device-%03d", info_.dev_num);
  name_[ZX_DEVICE_NAME_MAX] = 0;

  status = SetReportDescriptor();
  if (status != ZX_OK) {
    zxlogf(ERROR, "hid: could not retrieve HID report descriptor: %d", status);
    return status;
  }

  status = ProcessReportDescriptor();
  if (status != ZX_OK) {
    zxlogf(ERROR, "hid: could not parse hid report descriptor: %d", status);
    return status;
  }

  std::vector<zx_device_str_prop_t> props;
  props.reserve(page_usage_.size());
  for (const auto& prop : page_usage_) {
    auto bind = FindProp(prop);
    if (bind.is_error()) {
      zxlogf(DEBUG, "Page %x Usage %x not supported as a bind property yet. Skipping.", prop.page,
             prop.usage);
      continue;
    }
    props.emplace_back(zx_device_str_prop_t{
        .key = kBindPropKeyMap[*bind].c_str(),
        .property_value = str_prop_bool_val(true),
    });
  }

  status = InitReassemblyBuffer();
  if (status != ZX_OK) {
    zxlogf(ERROR, "hid: failed to initialize reassembly buffer: %d", status);
    return status;
  }

  // TODO: delay calling start until we've been opened by someone
  status = hidbus_.Start(this, &hid_ifc_ops);
  if (status != ZX_OK) {
    zxlogf(ERROR, "hid: could not start hid device: %d", status);
    ReleaseReassemblyBuffer();
    return status;
  }

  status = hidbus_.SetIdle(0, 0);
  if (status != ZX_OK) {
    zxlogf(DEBUG, "hid: [W] set_idle failed for %s: %d", name_.data(), status);
    // continue anyway
  }

  status = DdkAdd(ddk::DeviceAddArgs("hid-device").set_str_props(cpp20::span(props)));
  if (status != ZX_OK) {
    zxlogf(ERROR, "hid: device_add failed for HID device: %d", status);
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
    zxlogf(ERROR, "hid: bind: no hidbus protocol");
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
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = hid_bind;
  return ops;
}();

}  // namespace hid_driver

// clang-format off
ZIRCON_DRIVER(hid, hid_driver::hid_driver_ops, "zircon", "0.1");

// clang-format on
