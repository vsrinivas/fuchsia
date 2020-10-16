// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_INPUT_LIB_INPUT_REPORT_READER_READER_H_
#define SRC_UI_INPUT_LIB_INPUT_REPORT_READER_READER_H_

#include <fuchsia/input/report/llcpp/fidl.h>
#include <lib/zircon-internal/thread_annotations.h>

#include <list>

#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <fbl/ring_buffer.h>

namespace input {

namespace fuchsia_input_report = ::llcpp::fuchsia::input::report;

template <class Report>
class InputReportReader;

// This class creates and manages the InputReportReaders. It is able to send reports
// to all existing InputReportReaders.
// When this class is destructed, all of the InputReportReaders will be free.
// This class is thread-safe.
// Typical Usage:
// An InputReport Driver should have one InputReportReaderManager member object.
// The Driver should also have some form of InputReport object that can be converted to Fidl.
//
// Eg:
//
// class MyTouchScreenDriver {
// ...
// private:
//   struct TouchScreenReport {
//      int64_t x;
//      int64_t y;
//      fuchsia_input_report::InputReport ToFidlInputReport(fidl::Allocator& allocator);
//   };
//
//   InputReportReaderManager<TouchScreenReport> input_report_readers_;
// };
template <class Report>
class InputReportReaderManager {
 public:
  // Assert that our template type `Report` has the following function:
  //   fuchsia_input_report::InputReport ToFidlInputReport(fidl::Allocator& allocator);
  DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(
      has_to_fidl_input_report, ToFidlInputReport,
      fuchsia_input_report::InputReport (C::*)(fidl::Allocator& allocator));
  static_assert(has_to_fidl_input_report<Report>::value,
                "Report must implement fuchsia_input_report::InputReport "
                "ToFidlInputReport(fidl::Allocator&);");

  // This class can't be moved because the InputReportReaders are pointing to the main class.
  DISALLOW_COPY_ASSIGN_AND_MOVE(InputReportReaderManager);

  InputReportReaderManager() = default;

  // Create a new InputReportReader that is managed by this InputReportReaderManager.
  zx_status_t CreateReader(async_dispatcher_t* dispatcher, zx::channel server) {
    fbl::AutoLock lock(&readers_lock_);
    auto reader = InputReportReader<Report>::Create(this, dispatcher, std::move(server));
    if (!reader) {
      return ZX_ERR_INTERNAL;
    }
    readers_list_.push_back(std::move(reader));
    return ZX_OK;
  }

  // Send a report to all InputReportReaders.
  void SendReportToAllReaders(const Report& report) {
    fbl::AutoLock lock(&readers_lock_);
    for (auto& reader : readers_list_) {
      reader->ReceiveReport(report);
    }
  }

  // Remove a given reader from the list. This is called by the InputReportReader itself
  // when it wishes to be removed.
  void RemoveReaderFromList(InputReportReader<Report>* reader) {
    fbl::AutoLock lock(&readers_lock_);
    for (auto iter = readers_list_.begin(); iter != readers_list_.end(); ++iter) {
      if (iter->get() == reader) {
        readers_list_.erase(iter);
        break;
      }
    }
  }

 private:
  fbl::Mutex readers_lock_;
  std::list<std::unique_ptr<InputReportReader<Report>>> readers_list_ TA_GUARDED(readers_lock_);
};

// This class represents an InputReportReader that sends InputReports out to a specific client.
// This class is thread safe.
// Typical usage:
//  This class shouldn't be touched directly. An InputReport driver should only manipulate
//  the InputReportReaderManager.
template <class Report>
class InputReportReader : public fuchsia_input_report::InputReportsReader::Interface {
 public:
  // Create the InputReportReader. `manager` and `dispatcher` must outlive this InputReportReader.
  static std::unique_ptr<InputReportReader<Report>> Create(
      InputReportReaderManager<Report>* manager, async_dispatcher_t* dispatcher,
      zx::channel server);

  // This is only public to make std::unique_ptr work.
  explicit InputReportReader(InputReportReaderManager<Report>* manager) : manager_(manager) {}

  void ReceiveReport(const Report& report) TA_EXCL(&report_lock_);

  void ReadInputReports(ReadInputReportsCompleter::Sync& completer) TA_EXCL(&report_lock_) override;

 private:
  static constexpr size_t kInputReportBufferSize = 4096 * 4;

  void ReplyWithReports(ReadInputReportsCompleterBase& completer) TA_REQ(&report_lock_);

  fbl::Mutex report_lock_;
  std::optional<ReadInputReportsCompleter::Async> completer_ TA_GUARDED(&report_lock_);
  fidl::BufferThenHeapAllocator<kInputReportBufferSize> report_allocator_
      __TA_GUARDED(report_lock_);
  fbl::RingBuffer<Report, fuchsia_input_report::MAX_DEVICE_REPORT_COUNT> reports_data_
      __TA_GUARDED(report_lock_);

  InputReportReaderManager<Report>* manager_;
};

// Template Implementation.
template <class Report>
std::unique_ptr<InputReportReader<Report>> InputReportReader<Report>::Create(
    InputReportReaderManager<Report>* manager, async_dispatcher_t* dispatcher, zx::channel server) {
  fidl::OnUnboundFn<fuchsia_input_report::InputReportsReader::Interface> unbound_fn(
      [](fuchsia_input_report::InputReportsReader::Interface* interface, fidl::UnbindInfo info,
         zx::channel channel) {
        auto* reader = static_cast<InputReportReader<Report>*>(interface);
        reader->manager_->RemoveReaderFromList(reader);
      });

  auto reader = std::make_unique<InputReportReader<Report>>(manager);

  auto binding = fidl::BindServer(
      dispatcher, std::move(server),
      static_cast<fuchsia_input_report::InputReportsReader::Interface*>(reader.get()),
      std::move(unbound_fn));
  if (binding.is_error()) {
    return nullptr;
  }

  return reader;
}

template <class Report>
void InputReportReader<Report>::ReceiveReport(const Report& report) {
  fbl::AutoLock lock(&report_lock_);
  if (reports_data_.full()) {
    reports_data_.pop();
  }

  reports_data_.push(report);

  if (completer_) {
    ReplyWithReports(*completer_);
    completer_.reset();
  }
}

template <class Report>
void InputReportReader<Report>::ReadInputReports(ReadInputReportsCompleter::Sync& completer) {
  fbl::AutoLock lock(&report_lock_);
  if (completer_) {
    completer.ReplyError(ZX_ERR_ALREADY_BOUND);
    return;
  }
  if (reports_data_.empty()) {
    completer_.emplace(completer.ToAsync());
  } else {
    ReplyWithReports(completer);
  }
}

template <class Report>
void InputReportReader<Report>::ReplyWithReports(ReadInputReportsCompleterBase& completer) {
  std::array<fuchsia_input_report::InputReport, fuchsia_input_report::MAX_DEVICE_REPORT_COUNT>
      reports;

  size_t num_reports = 0;
  for (; !reports_data_.empty() && num_reports < reports.size(); num_reports++) {
    reports[num_reports] = reports_data_.front().ToFidlInputReport(report_allocator_);
    reports_data_.pop();
  }

  completer.ReplySuccess(fidl::VectorView(fidl::unowned_ptr(reports.data()), num_reports));

  if (reports_data_.empty()) {
    report_allocator_.inner_allocator().reset();
  }
}

}  // namespace input

#endif  // SRC_UI_INPUT_LIB_INPUT_REPORT_READER_READER_H_
