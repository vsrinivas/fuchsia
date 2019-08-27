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

zx_status_t HidInstance::DdkRead(void* buf, size_t count, zx_off_t off, size_t* actual) {
  TRACE_DURATION("input", "HID Read Instance");
  zx_status_t status = ZX_OK;

  if (flags_ & kHidFlagsDead) {
    return ZX_ERR_PEER_CLOSED;
  }

  size_t left;
  mtx_lock(&fifo_.lock);
  size_t xfer;
  uint8_t rpt_id;
  ssize_t r = zx_hid_fifo_peek(&fifo_, &rpt_id);
  if (r < 1) {
    // fifo is empty
    mtx_unlock(&fifo_.lock);
    return ZX_ERR_SHOULD_WAIT;
  }

  xfer = base_->GetReportSizeById(rpt_id, ReportType::INPUT);
  if (xfer == 0) {
    zxlogf(ERROR, "error reading hid device: unknown report id (%u)!\n", rpt_id);
    mtx_unlock(&fifo_.lock);
    return ZX_ERR_BAD_STATE;
  }

  if (xfer > count) {
    zxlogf(SPEW, "next report: %zd, read count: %zd\n", xfer, count);
    mtx_unlock(&fifo_.lock);
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  r = zx_hid_fifo_read(&fifo_, buf, xfer);
  left = zx_hid_fifo_size(&fifo_);
  if (left == 0) {
    device_state_clr(zxdev_, DEV_STATE_READABLE);
  }
  mtx_unlock(&fifo_.lock);
  if (r > 0) {
    TRACE_FLOW_STEP("input", "hid_report", hid_report_trace_id(trace_id_, reports_read_));
    ++reports_read_;
    *actual = r;
    r = ZX_OK;
  } else if (r == 0) {
    status = ZX_ERR_SHOULD_WAIT;
  }
  return status;
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

void HidInstance::GetBootProtocol(GetBootProtocolCompleter::Sync _completer) {
  _completer.Reply(base_->GetBootProtocol());
}

void HidInstance::GetReportDescSize(GetReportDescSizeCompleter::Sync _completer) {
  _completer.Reply(static_cast<uint16_t>(base_->GetReportDescLen()));
}

void HidInstance::GetReportDesc(GetReportDescCompleter::Sync _completer) {
  size_t desc_size = base_->GetReportDescLen();
  const uint8_t* desc = base_->GetReportDesc();

   // (BUG 35762) Const cast is necessary until simple data types are generated
   // as const in LLCPP. We know the data is not modified.
  _completer.Reply(::fidl::VectorView<uint8_t>(desc_size, const_cast<uint8_t*>(desc)));
}

void HidInstance::GetNumReports(GetNumReportsCompleter::Sync _completer) {
  _completer.Reply(static_cast<uint16_t>(base_->GetNumReports()));
}

void HidInstance::GetReportIds(GetReportIdsCompleter::Sync _completer) {
  uint8_t report_ids[::llcpp::fuchsia::hardware::input::MAX_REPORT_IDS];
  base_->GetReportIds(report_ids);

  fidl::VectorView id_view(base_->GetNumReports(), report_ids);

  _completer.Reply(id_view);
}

void HidInstance::GetReportSize(ReportType type, uint8_t id,
                                GetReportSizeCompleter::Sync _completer) {
  input_report_size_t size = base_->GetReportSizeById(id, type);
  _completer.Reply((size == 0) ? ZX_ERR_NOT_FOUND : ZX_OK, size);
}

void HidInstance::GetMaxInputReportSize(GetMaxInputReportSizeCompleter::Sync _completer) {
  _completer.Reply(base_->GetMaxInputReportSize());
}

void HidInstance::GetReport(ReportType type, uint8_t id, GetReportCompleter::Sync _completer) {
  input_report_size_t needed = base_->GetReportSizeById(id, type);
  if (needed == 0) {
    _completer.Reply(ZX_ERR_NOT_FOUND, fidl::VectorView<uint8_t>(0, nullptr));
    return;
  }

  uint8_t report[needed];
  size_t actual = 0;
  zx_status_t status = base_->GetHidbusProtocol()->GetReport(static_cast<uint8_t>(type), id, report,
                                                             needed, &actual);

  fidl::VectorView<uint8_t> report_view(actual, report);
  _completer.Reply(status, report_view);
}

void HidInstance::SetReport(ReportType type, uint8_t id, ::fidl::VectorView<uint8_t> report,
                            SetReportCompleter::Sync _completer) {
  input_report_size_t needed = base_->GetReportSizeById(id, type);
  if (needed < report.count()) {
    _completer.Reply(ZX_ERR_BUFFER_TOO_SMALL);
    return;
  }

  zx_status_t status = base_->GetHidbusProtocol()->SetReport(static_cast<uint8_t>(type), id,
                                                             report.data(), report.count());
  _completer.Reply(status);
  return;
}

void HidInstance::SetTraceId(uint32_t id, SetTraceIdCompleter::Sync _completer) { trace_id_ = id; }

void HidInstance::CloseInstance() {
  flags_ |= kHidFlagsDead;
  SetState(DEV_STATE_READABLE);
}

void HidInstance::WriteToFifo(const uint8_t* report, size_t report_len) {
  mtx_lock(&fifo_.lock);
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
      device_state_set(zxdev(), DEV_STATE_READABLE);
    }
  }
  mtx_unlock(&fifo_.lock);
}

zx_status_t HidInstance::Bind(HidDevice* base) {
  base_ = base;
  return DdkAdd("hid-instance", DEVICE_ADD_INSTANCE);
}


}  // namespace hid_driver
