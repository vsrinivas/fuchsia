// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_LIB_MOCK_HIDBUS_IFC_INCLUDE_LIB_MOCK_HIDBUS_IFC_MOCK_HIDBUS_IFC_H_
#define ZIRCON_SYSTEM_DEV_LIB_MOCK_HIDBUS_IFC_INCLUDE_LIB_MOCK_HIDBUS_IFC_MOCK_HIDBUS_IFC_H_

#include <lib/sync/completion.h>
#include <lib/zircon-internal/thread_annotations.h>

#include <ddktl/protocol/hidbus.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <fbl/vector.h>
#include <zxtest/zxtest.h>

namespace mock_hidbus_ifc {

// This class provides a mock a hidbus_ifc_protocol_t that can be passed to the HidbusStart method
// of HID drivers. The template parameter is used to interpret and save reports. See the following
// example test:
//
// mock_hidbus_ifc::MockHidbusIfc<some_report_struct_t> mock_ifc;
// SomeDriver dut;
//
// ASSERT_OK(dut.HidbusStart(mock_ifc.proto()));
// ASSERT_OK(mock_ifc.WaitForReports(5));
//
// for (const some_report_struct_t& report : mock_ifc.Reports()) {
//     // Do something
// }

template <typename T>
class MockHidbusIfc : public ddk::HidbusIfcProtocol<MockHidbusIfc<T>> {
 public:
  MockHidbusIfc() : ifc_{&this->hidbus_ifc_protocol_ops_, this} {}

  const hidbus_ifc_protocol_t* proto() const { return &ifc_; }

  // Waits for count reports to be received by IoQueue.
  zx_status_t WaitForReports(size_t count) {
    for (;;) {
      {
        fbl::AutoLock lock(&reports_lock_);
        if (reports_.size() == count) {
          break;
        }
      }

      zx_status_t status = sync_completion_wait(&signal_, ZX_TIME_INFINITE);
      sync_completion_reset(&signal_);

      if (status != ZX_OK) {
        return status;
      }
    }

    return ZX_OK;
  }

  // Empties the buffer holding received reports.
  void Reset() {
    fbl::AutoLock lock(&reports_lock_);
    reports_.reset();
  }

  // Returns a vector containing the received reports.
  const fbl::Vector<T>& reports() { return reports_; }

  size_t PendingReports() {
    fbl::AutoLock lock(&reports_lock_);
    return reports_.size();
  }

  void HidbusIfcIoQueue(const void* buffer, size_t buf_size, zx_time_t time) {
    HidbusIfcIoQueueHelper(buffer, buf_size);
  }

 private:
  void HidbusIfcIoQueueHelper(const void* buffer, size_t buf_size) {
    ASSERT_EQ(sizeof(T), buf_size);

    {
      fbl::AutoLock lock(&reports_lock_);
      reports_.push_back(*reinterpret_cast<const T*>(buffer));
    }

    sync_completion_signal(&signal_);
  }

  const hidbus_ifc_protocol_t ifc_;
  fbl::Vector<T> reports_ TA_GUARDED(reports_lock_);
  sync_completion_t signal_;
  fbl::Mutex reports_lock_;
};

}  // namespace mock_hidbus_ifc

#endif  // ZIRCON_SYSTEM_DEV_LIB_MOCK_HIDBUS_IFC_INCLUDE_LIB_MOCK_HIDBUS_IFC_MOCK_HIDBUS_IFC_H_
