// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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

#include "hid.h"

namespace hid_driver {

static constexpr uint32_t kHidFlagsDead = (1 << 0);
static constexpr uint32_t kHidFlagsWriteFailed = (1 << 1);

static constexpr uint64_t hid_report_trace_id(uint32_t instance_id, uint64_t report_id) {
  return (report_id << 32) | instance_id;
}

void HidInstance::SetReadable() {
  SetState(DEV_STATE_READABLE);
  fifo_event_.signal(0, DEV_STATE_READABLE);
}

void HidInstance::ClearReadable() {
  ClearState(DEV_STATE_READABLE);
  fifo_event_.signal(DEV_STATE_READABLE, 0);
}

zx_status_t HidInstance::DdkRead(void* buf, size_t count, zx_off_t off, size_t* actual) {
  TRACE_DURATION("input", "HID Read Instance");
  zx_status_t status = ZX_OK;

  if (flags_ & kHidFlagsDead) {
    return ZX_ERR_PEER_CLOSED;
  }

  size_t left;
  size_t xfer;
  uint8_t rpt_id;

  fbl::AutoLock lock(&fifo_lock_);
  ssize_t r = zx_hid_fifo_peek(&fifo_, &rpt_id);
  if (r < 1) {
    // fifo is empty
    return ZX_ERR_SHOULD_WAIT;
  }

  xfer = base_->GetReportSizeById(rpt_id, ReportType::INPUT);
  if (xfer == 0) {
    zxlogf(ERROR, "error reading hid device: unknown report id (%u)!\n", rpt_id);
    return ZX_ERR_BAD_STATE;
  }

  if (xfer > count) {
    zxlogf(SPEW, "next report: %zd, read count: %zd\n", xfer, count);
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  r = zx_hid_fifo_read(&fifo_, buf, xfer);
  left = zx_hid_fifo_size(&fifo_);
  if (left == 0) {
    ClearReadable();
  }
  if (r > 0) {
    TRACE_FLOW_STEP("input", "hid_report", hid_report_trace_id(trace_id_, reports_sent_));
    ++reports_sent_;
    *actual = r;
    r = ZX_OK;
  } else if (r == 0) {
    status = ZX_ERR_SHOULD_WAIT;
  }
  return status;
}

void HidInstance::GetReports(GetReportsCompleter::Sync completer) {
  TRACE_DURATION("input", "HID GetReports Instance", "bytes_in_fifo", zx_hid_fifo_size(&fifo_));

  if (flags_ & kHidFlagsDead) {
    ::fidl::VectorView<uint8_t> buf_view(nullptr, 0);
    completer.Reply(ZX_ERR_PEER_CLOSED, buf_view);
    return;
  }

  uint8_t buf[::llcpp::fuchsia::hardware::input::MAX_REPORT_DATA];
  size_t buf_index = 0;
  zx_status_t status = ZX_OK;

  fbl::AutoLock lock(&fifo_lock_);
  uint8_t rpt_id;
  size_t local_reports_read = 0;
  while (zx_hid_fifo_peek(&fifo_, &rpt_id) > 0) {
    size_t xfer = base_->GetReportSizeById(rpt_id, ReportType::INPUT);
    if (xfer == 0) {
      zxlogf(ERROR, "error reading hid device: unknown report id (%u)!\n", rpt_id);
      status = ZX_ERR_BAD_STATE;
      break;
    }

    // Check if we have enough room left in the buffer.
    if (xfer + buf_index > sizeof(buf)) {
      // Only an error if we haven't read any reports yet.
      if (buf_index == 0) {
        status = ZX_ERR_INTERNAL;
      }
      break;
    }

    ssize_t rpt_size = zx_hid_fifo_read(&fifo_, buf + buf_index, xfer);
    size_t left = zx_hid_fifo_size(&fifo_);
    if (left == 0) {
      ClearReadable();
    }

    if (rpt_size <= 0) {
      // Something went wrong. The fifo should always contain full reports in it.
      status = ZX_ERR_INTERNAL;
      break;
    }
    ++local_reports_read;
    buf_index += rpt_size;
  }
  lock.release();

  if (status != ZX_OK) {
    ::fidl::VectorView<uint8_t> buf_view(nullptr, 0);
    completer.Reply(status, buf_view);
    return;
  }

  if (buf_index == 0) {
    status = ZX_ERR_SHOULD_WAIT;
  }

  while (local_reports_read--) {
    TRACE_FLOW_STEP("input", "hid_report", hid_report_trace_id(trace_id_, reports_sent_));
    reports_sent_ += 1;
  }
  ::fidl::VectorView<uint8_t> buf_view(buf, buf_index);
  completer.Reply(status, buf_view);
}

void HidInstance::GetReportsEvent(GetReportsEventCompleter::Sync completer) {
  zx::event new_event;
  zx_status_t status = fifo_event_.duplicate(ZX_RIGHTS_BASIC, &new_event);

  completer.Reply(status, std::move(new_event));
}

zx_status_t HidInstance::DdkClose(uint32_t flags) {
  flags_ |= kHidFlagsDead;
  base_->RemoveHidInstanceFromList(this);
  return ZX_OK;
}

void HidInstance::DdkRelease() { delete this; }

zx_status_t HidInstance::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction transaction(txn);
  ::llcpp::fuchsia::hardware::input::Device::Dispatch(this, msg, &transaction);
  return transaction.Status();
}

void HidInstance::GetBootProtocol(GetBootProtocolCompleter::Sync completer) {
  completer.Reply(base_->GetBootProtocol());
}

void HidInstance::GetDeviceIds(GetDeviceIdsCompleter::Sync completer) {
  hid_info_t info = base_->GetHidInfo();
  ::llcpp::fuchsia::hardware::input::DeviceIds ids = {};
  ids.vendor_id = info.vendor_id;
  ids.product_id = info.product_id;
  ids.version = info.version;

  completer.Reply(ids);
}

void HidInstance::GetReportDescSize(GetReportDescSizeCompleter::Sync completer) {
  completer.Reply(static_cast<uint16_t>(base_->GetReportDescLen()));
}

void HidInstance::GetReportDesc(GetReportDescCompleter::Sync completer) {
  size_t desc_size = base_->GetReportDescLen();
  const uint8_t* desc = base_->GetReportDesc();

  // (BUG 35762) Const cast is necessary until simple data types are generated
  // as const in LLCPP. We know the data is not modified.
  completer.Reply(::fidl::VectorView<uint8_t>(const_cast<uint8_t*>(desc), desc_size));
}

void HidInstance::GetNumReports(GetNumReportsCompleter::Sync completer) {
  completer.Reply(static_cast<uint16_t>(base_->GetNumReports()));
}

void HidInstance::GetReportIds(GetReportIdsCompleter::Sync completer) {
  uint8_t report_ids[::llcpp::fuchsia::hardware::input::MAX_REPORT_IDS];
  base_->GetReportIds(report_ids);

  fidl::VectorView id_view(report_ids, base_->GetNumReports());

  completer.Reply(id_view);
}

void HidInstance::GetReportSize(ReportType type, uint8_t id,
                                GetReportSizeCompleter::Sync completer) {
  input_report_size_t size = base_->GetReportSizeById(id, type);
  completer.Reply((size == 0) ? ZX_ERR_NOT_FOUND : ZX_OK, size);
}

void HidInstance::GetMaxInputReportSize(GetMaxInputReportSizeCompleter::Sync completer) {
  completer.Reply(base_->GetMaxInputReportSize());
}

void HidInstance::GetReport(ReportType type, uint8_t id, GetReportCompleter::Sync completer) {
  input_report_size_t needed = base_->GetReportSizeById(id, type);
  if (needed == 0) {
    completer.Reply(ZX_ERR_NOT_FOUND, fidl::VectorView<uint8_t>(nullptr, 0));
    return;
  }

  uint8_t report[needed];
  size_t actual = 0;
  zx_status_t status = base_->GetHidbusProtocol()->GetReport(static_cast<uint8_t>(type), id, report,
                                                             needed, &actual);

  fidl::VectorView<uint8_t> report_view(report, actual);
  completer.Reply(status, report_view);
}

void HidInstance::SetReport(ReportType type, uint8_t id, ::fidl::VectorView<uint8_t> report,
                            SetReportCompleter::Sync completer) {
  input_report_size_t needed = base_->GetReportSizeById(id, type);
  if (needed < report.count()) {
    completer.Reply(ZX_ERR_BUFFER_TOO_SMALL);
    return;
  }

  zx_status_t status = base_->GetHidbusProtocol()->SetReport(static_cast<uint8_t>(type), id,
                                                             report.data(), report.count());
  completer.Reply(status);
  return;
}

void HidInstance::SetTraceId(uint32_t id, SetTraceIdCompleter::Sync completer) { trace_id_ = id; }

void HidInstance::CloseInstance() {
  flags_ |= kHidFlagsDead;
  SetReadable();
}

void HidInstance::WriteToFifo(const uint8_t* report, size_t report_len) {
  fbl::AutoLock lock(&fifo_lock_);

  bool was_empty = zx_hid_fifo_size(&fifo_) == 0;
  ssize_t wrote = zx_hid_fifo_write(&fifo_, report, report_len);

  if (wrote <= 0) {
    if (!(flags_ & kHidFlagsWriteFailed)) {
      zxlogf(ERROR, "%s: could not write to hid fifo (ret=%zd)\n", base_->GetName(), wrote);
      flags_ |= kHidFlagsWriteFailed;
    }
  } else {
    TRACE_FLOW_BEGIN("input", "hid_report", hid_report_trace_id(trace_id_, reports_written_));
    ++reports_written_;
    flags_ &= ~kHidFlagsWriteFailed;
    if (was_empty) {
      SetReadable();
    }
  }
}

zx_status_t HidInstance::Bind(HidDevice* base) {
  base_ = base;
  zx_status_t status = zx::event::create(0, &fifo_event_);
  if (status != ZX_OK) {
    return status;
  }

  return DdkAdd("hid-instance", DEVICE_ADD_INSTANCE);
}

}  // namespace hid_driver
