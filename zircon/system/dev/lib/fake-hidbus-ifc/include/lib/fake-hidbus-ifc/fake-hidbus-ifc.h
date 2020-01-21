// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/sync/completion.h>
#include <zircon/types.h>

#include <vector>

#include <ddktl/protocol/hidbus.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>

#ifndef ZIRCON_SYSTEM_DEV_LIB_FAKE_HIDBUS_IFC_INCLUDE_LIB_FAKE_HIDBUS_IFC_FAKE_HIDBUS_IFC_H_
#define ZIRCON_SYSTEM_DEV_LIB_FAKE_HIDBUS_IFC_INCLUDE_LIB_FAKE_HIDBUS_IFC_FAKE_HIDBUS_IFC_H_

namespace fake_hidbus_ifc {

// This class fakes the Hidbus Interface and allows a tester to read reports from their
// Hidbus device. A tester can perform a blocking wait until the next report is seen, or
// read the last seen report from the FakeHidbusIfc.
//
// Here's an example:
//
// MyHidbusDriver dev;
// fake_hidbus_ifc::FakeHidbusIfc ifc;
// dev.HidbusStart(ifc.GetProto());
//
// std::vector<uint8_t> report;
// ifc.WaitUntilNextReport(&report);
class FakeHidbusIfc : public ddk::HidbusIfcProtocol<FakeHidbusIfc> {
 public:
  FakeHidbusIfc() : proto_({&hidbus_ifc_protocol_ops_, this}) {}

  size_t NumReportsSeen() { return reports_seen_; }

  void HidbusIfcIoQueue(const void* buf_buffer, size_t buf_size, zx_time_t time) {
    reports_seen_++;
    auto buf = reinterpret_cast<const uint8_t*>(buf_buffer);
    {
      fbl::AutoLock lock(&report_lock_);
      last_report_ = std::vector<uint8_t>(buf, buf + buf_size);
    }
    sync_completion_signal(&report_queued_);
  }

  // Waits until a report is seen, then puts a copy of the report in |report|.
  // Will wait indefinitely.
  zx_status_t WaitUntilNextReport(std::vector<uint8_t>* report) {
    zx_status_t status = sync_completion_wait_deadline(&report_queued_, zx::time::infinite().get());
    if (status == ZX_OK) {
      fbl::AutoLock lock(&report_lock_);
      *report = last_report_;
    }
    return status;
  }

  // Gets the last seen report. If no report has been seen, a std::vector of 0 size will be
  // returned.
  std::vector<uint8_t> GetLastReport() {
    fbl::AutoLock lock(&report_lock_);
    return last_report_;
  }

  const hidbus_ifc_protocol_t* GetProto() { return &proto_; }

 private:
  fbl::Mutex report_lock_;
  std::vector<uint8_t> last_report_ __TA_GUARDED(report_lock_);

  sync_completion_t report_queued_;
  size_t reports_seen_ = 0;

  hidbus_ifc_protocol_t proto_ = {};
};

}  // namespace fake_hidbus_ifc

#endif  // ZIRCON_SYSTEM_DEV_LIB_FAKE_HIDBUS_IFC_INCLUDE_LIB_FAKE_HIDBUS_IFC_FAKE_HIDBUS_IFC_H_
