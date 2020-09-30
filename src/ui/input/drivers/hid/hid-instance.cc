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

zx_status_t HidInstance::ReadReportFromFifo(uint8_t* buf, size_t buf_size, zx_time_t* time,
                                            size_t* report_size) {
  uint8_t rpt_id;
  if (zx_hid_fifo_peek(&fifo_, &rpt_id) <= 0) {
    return ZX_ERR_SHOULD_WAIT;
  }

  size_t xfer = base_->GetReportSizeById(rpt_id, ReportType::INPUT);
  if (xfer == 0) {
    zxlogf(ERROR, "error reading hid device: unknown report id (%u)!", rpt_id);
    return ZX_ERR_BAD_STATE;
  }

  // Check if we have enough room left in the buffer.
  if (xfer > buf_size) {
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  ssize_t rpt_size = zx_hid_fifo_read(&fifo_, buf, xfer);
  if (rpt_size <= 0) {
    // Something went wrong. The fifo should always contain full reports in it.
    return ZX_ERR_INTERNAL;
  }

  size_t left = zx_hid_fifo_size(&fifo_);
  if (left == 0) {
    ClearReadable();
  }

  *report_size = rpt_size;

  *time = timestamps_.front();
  timestamps_.pop();

  reports_sent_ += 1;
  TRACE_FLOW_STEP("input", "hid_report", hid_report_trace_id(trace_id_, reports_sent_));

  return ZX_OK;
}

void HidInstance::ReadReport(ReadReportCompleter::Sync& completer) {
  TRACE_DURATION("input", "HID ReadReport Instance", "bytes_in_fifo", zx_hid_fifo_size(&fifo_));

  if (flags_ & kHidFlagsDead) {
    completer.Close(ZX_ERR_BAD_STATE);
    return;
  }

  std::array<uint8_t, ::llcpp::fuchsia::hardware::input::MAX_REPORT_DATA> buf;
  zx_time_t time = 0;
  size_t report_size = 0;
  zx_status_t status;

  {
    fbl::AutoLock lock(&fifo_lock_);
    status = ReadReportFromFifo(buf.data(), buf.size(), &time, &report_size);
  }

  ::fidl::VectorView<uint8_t> buf_view(fidl::unowned_ptr(buf.data()), report_size);
  completer.Reply(status, std::move(buf_view), time);
}

void HidInstance::ReadReports(ReadReportsCompleter::Sync& completer) {
  TRACE_DURATION("input", "HID GetReports Instance", "bytes_in_fifo", zx_hid_fifo_size(&fifo_));

  if (flags_ & kHidFlagsDead) {
    completer.Close(ZX_ERR_BAD_STATE);
    return;
  }

  std::array<uint8_t, ::llcpp::fuchsia::hardware::input::MAX_REPORT_DATA> buf;
  size_t buf_index = 0;
  zx_status_t status = ZX_OK;
  zx_time_t time;

  {
    fbl::AutoLock lock(&fifo_lock_);
    while (status == ZX_OK) {
      size_t report_size;
      status =
          ReadReportFromFifo(buf.data() + buf_index, buf.size() - buf_index, &time, &report_size);
      if (status == ZX_OK) {
        buf_index += report_size;
      }
    }
  }

  if ((buf_index > 0) && ((status == ZX_ERR_BUFFER_TOO_SMALL) || (status == ZX_ERR_SHOULD_WAIT))) {
    status = ZX_OK;
  }

  if (status != ZX_OK) {
    ::fidl::VectorView<uint8_t> buf_view(nullptr, 0);
    completer.Reply(status, std::move(buf_view));
    return;
  }

  ::fidl::VectorView<uint8_t> buf_view(fidl::unowned_ptr(buf.data()), buf_index);
  completer.Reply(status, std::move(buf_view));
}

void HidInstance::GetReportsEvent(GetReportsEventCompleter::Sync& completer) {
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

void HidInstance::GetBootProtocol(GetBootProtocolCompleter::Sync& completer) {
  completer.Reply(base_->GetBootProtocol());
}

void HidInstance::GetDeviceIds(GetDeviceIdsCompleter::Sync& completer) {
  hid_info_t info = base_->GetHidInfo();
  ::llcpp::fuchsia::hardware::input::DeviceIds ids = {};
  ids.vendor_id = info.vendor_id;
  ids.product_id = info.product_id;
  ids.version = info.version;

  completer.Reply(ids);
}

void HidInstance::GetReportDesc(GetReportDescCompleter::Sync& completer) {
  size_t desc_size = base_->GetReportDescLen();
  const uint8_t* desc = base_->GetReportDesc();

  // (BUG 35762) Const cast is necessary until simple data types are generated
  // as const in LLCPP. We know the data is not modified.
  completer.Reply(
      ::fidl::VectorView<uint8_t>(fidl::unowned_ptr(const_cast<uint8_t*>(desc)), desc_size));
}

void HidInstance::GetReport(ReportType type, uint8_t id, GetReportCompleter::Sync& completer) {
  size_t needed = base_->GetReportSizeById(id, type);
  if (needed == 0) {
    completer.Reply(ZX_ERR_NOT_FOUND, fidl::VectorView<uint8_t>(nullptr, 0));
    return;
  }

  uint8_t report[needed];
  size_t actual = 0;
  zx_status_t status = base_->GetHidbusProtocol()->GetReport(static_cast<uint8_t>(type), id, report,
                                                             needed, &actual);

  fidl::VectorView<uint8_t> report_view(fidl::unowned_ptr(report), actual);
  completer.Reply(status, std::move(report_view));
}

void HidInstance::SetReport(ReportType type, uint8_t id, ::fidl::VectorView<uint8_t> report,
                            SetReportCompleter::Sync& completer) {
  size_t needed = base_->GetReportSizeById(id, type);
  if (needed != report.count()) {
    zxlogf(ERROR, "%s: Tried to set Report %d (size 0x%lx) with 0x%lx bytes\n", base_->GetName(),
           id, needed, report.count());
    completer.Reply(ZX_ERR_INVALID_ARGS);
    return;
  }

  zx_status_t status = base_->GetHidbusProtocol()->SetReport(static_cast<uint8_t>(type), id,
                                                             report.data(), report.count());
  completer.Reply(status);
  return;
}

void HidInstance::GetDeviceReportsReader(zx::channel reader,
                                         GetDeviceReportsReaderCompleter::Sync& completer) {
  fbl::AutoLock lock(&readers_lock_);
  zx_status_t status;
  if (!loop_started_) {
    status = loop_.StartThread("hid-reports-reader-loop");
    if (status != ZX_OK) {
      completer.ReplyError(status);
      return;
    }
    loop_started_ = true;
  }
  readers_.push_back(std::make_unique<DeviceReportsReader>(base_));
  fidl::BindSingleInFlightOnly(loop_.dispatcher(), std::move(reader), readers_.back().get());
  completer.ReplySuccess();
}

void HidInstance::SetTraceId(uint32_t id, SetTraceIdCompleter::Sync& completer) { trace_id_ = id; }

void HidInstance::CloseInstance() {
  flags_ |= kHidFlagsDead;
  SetReadable();
}

void HidInstance::WriteToFifo(const uint8_t* report, size_t report_len, zx_time_t time) {
  {
    fbl::AutoLock lock(&readers_lock_);
    auto iter = readers_.begin();
    while (iter != readers_.end()) {
      if ((*iter)->WriteToFifo(report, report_len, time) != ZX_OK) {
        iter = readers_.erase(iter);
      } else {
        iter++;
      }
    }
  }

  fbl::AutoLock lock(&fifo_lock_);

  if (timestamps_.full()) {
    flags_ |= kHidFlagsWriteFailed;
    return;
  }

  bool was_empty = zx_hid_fifo_size(&fifo_) == 0;

  ssize_t wrote = zx_hid_fifo_write(&fifo_, report, report_len);
  if (wrote <= 0) {
    if (!(flags_ & kHidFlagsWriteFailed)) {
      zxlogf(ERROR, "%s: could not write to hid fifo (ret=%zd)", base_->GetName(), wrote);
      flags_ |= kHidFlagsWriteFailed;
    }
    return;
  }

  timestamps_.push(time);

  TRACE_FLOW_BEGIN("input", "hid_report", hid_report_trace_id(trace_id_, reports_written_));
  ++reports_written_;
  flags_ &= ~kHidFlagsWriteFailed;
  if (was_empty) {
    SetReadable();
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
