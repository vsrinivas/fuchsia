// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_INPUT_REPORT_READER_READER_H_
#define LIB_INPUT_REPORT_READER_READER_H_

#include <fidl/fuchsia.input.report/cpp/wire.h>
#include <lib/input_report_reader/ring_buffer.h>
#include <lib/trace/event.h>
#include <zircon/compiler.h>

#include <array>
#include <list>
#include <memory>
#include <mutex>
#include <optional>

namespace input_report_reader {

using ReadInputReportsCompleterBase =
    fidl::internal::WireCompleterBase<fuchsia_input_report::InputReportsReader::ReadInputReports>;

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
//      void ToFidlInputReport(fidl::WireTableBuilder<::fuchsia_input_report::wire::InputReport>&
//      input_report,
//                             fidl::AnyArena& allocator);
//   };
//
//   InputReportReaderManager<TouchScreenReport> input_report_readers_;
// };
template <class Report>
class InputReportReaderManager final {
 public:
  // Assert that our template type `Report` has the following function:
  //      void ToFidlInputReport(fidl::WireTableBuilder<::fuchsia_input_report::wire::InputReport>&
  //      input_report,
  //                             fidl::AnyArena& allocator);
  template <typename T>
  struct has_to_fidl_input_report {
   private:
    template <typename C>
    static std::true_type test(
        decltype(static_cast<void (C::*)(
                     fidl::WireTableBuilder<fuchsia_input_report::wire::InputReport>& input_report,
                     fidl::AnyArena& allocator)>(&C::ToFidlInputReport)));
    template <typename C>
    static std::false_type test(...);

   public:
    static constexpr bool value = decltype(test<T>(nullptr))::value;
  };
  static_assert(
      has_to_fidl_input_report<Report>::value,
      "Report must implement void "
      "ToFidlInputReport(fidl::WireTableBuilder<::fuchsia_input_report::wire::InputReport>& "
      "input_report, fidl::AnyArena& allocator);");

  InputReportReaderManager() = default;
  // This class can't be moved because the InputReportReaders are pointing to the main class.
  InputReportReaderManager(const InputReportReaderManager&) = delete;
  InputReportReaderManager(InputReportReaderManager&&) = delete;
  InputReportReaderManager& operator=(const InputReportReaderManager&) = delete;
  InputReportReaderManager& operator=(InputReportReaderManager&&) = delete;

  // Create a new InputReportReader that is managed by this InputReportReaderManager.
  zx_status_t CreateReader(async_dispatcher_t* dispatcher,
                           fidl::ServerEnd<fuchsia_input_report::InputReportsReader> server) {
    std::scoped_lock lock(readers_lock_);
    auto reader =
        InputReportReader<Report>::Create(this, next_reader_id_, dispatcher, std::move(server));
    if (!reader) {
      return ZX_ERR_INTERNAL;
    }
    next_reader_id_++;
    readers_list_.push_back(std::move(reader));
    return ZX_OK;
  }

  // Send a report to all InputReportReaders.
  void SendReportToAllReaders(const Report& report) {
    std::scoped_lock lock(readers_lock_);
    for (auto& reader : readers_list_) {
      reader->ReceiveReport(report);
    }
  }

  // Remove a given reader from the list. This is called by the InputReportReader itself
  // when it wishes to be removed.
  void RemoveReaderFromList(InputReportReader<Report>* reader) {
    std::scoped_lock lock(readers_lock_);
    for (auto iter = readers_list_.begin(); iter != readers_list_.end(); ++iter) {
      if (iter->get() == reader) {
        readers_list_.erase(iter);
        break;
      }
    }
  }

 private:
  std::mutex readers_lock_;
  size_t next_reader_id_ __TA_GUARDED(readers_lock_) = 1;
  std::list<std::unique_ptr<InputReportReader<Report>>> readers_list_ __TA_GUARDED(readers_lock_);
};

// This class represents an InputReportReader that sends InputReports out to a specific client.
// This class is thread safe.
// Typical usage:
//  This class shouldn't be touched directly. An InputReport driver should only manipulate
//  the InputReportReaderManager.
template <class Report>
class InputReportReader final : public fidl::WireServer<fuchsia_input_report::InputReportsReader> {
 public:
  // Create the InputReportReader. `manager` and `dispatcher` must outlive this InputReportReader.
  static std::unique_ptr<InputReportReader<Report>> Create(
      InputReportReaderManager<Report>* manager, size_t reader_id, async_dispatcher_t* dispatcher,
      fidl::ServerEnd<fuchsia_input_report::InputReportsReader> server);

  // This is only public to make std::unique_ptr work.
  explicit InputReportReader(InputReportReaderManager<Report>* manager, size_t reader_id)
      : reader_id_(reader_id), manager_(manager) {}

  void ReceiveReport(const Report& report) __TA_EXCLUDES(&report_lock_);

  void ReadInputReports(ReadInputReportsCompleter::Sync& completer)
      __TA_EXCLUDES(&report_lock_) override;

 private:
  static constexpr size_t kInputReportBufferSize = 4096 * 4;

  void ReplyWithReports(ReadInputReportsCompleterBase& completer) __TA_REQUIRES(&report_lock_);

  std::mutex report_lock_;
  std::optional<ReadInputReportsCompleter::Async> completer_ __TA_GUARDED(&report_lock_);
  fidl::Arena<kInputReportBufferSize> report_allocator_ __TA_GUARDED(report_lock_);
  RingBuffer<Report, fuchsia_input_report::wire::kMaxDeviceReportCount> reports_data_
      __TA_GUARDED(report_lock_);

  const size_t reader_id_;
  InputReportReaderManager<Report>* manager_;
};

// Template Implementation.
template <class Report>
inline std::unique_ptr<InputReportReader<Report>> InputReportReader<Report>::Create(
    InputReportReaderManager<Report>* manager, size_t reader_id, async_dispatcher_t* dispatcher,
    fidl::ServerEnd<fuchsia_input_report::InputReportsReader> server) {
  fidl::OnUnboundFn<InputReportReader> unbound_fn(
      [](InputReportReader* reader, fidl::UnbindInfo info,
         fidl::ServerEnd<fuchsia_input_report::InputReportsReader> channel) {
        reader->manager_->RemoveReaderFromList(reader);
      });

  auto reader = std::make_unique<InputReportReader<Report>>(manager, reader_id);
  fidl::BindServer(dispatcher, std::move(server), reader.get(), std::move(unbound_fn));
  return reader;
}

template <class Report>
inline void InputReportReader<Report>::ReceiveReport(const Report& report) {
  std::scoped_lock lock(report_lock_);
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
inline void InputReportReader<Report>::ReadInputReports(
    ReadInputReportsCompleter::Sync& completer) {
  std::scoped_lock lock(report_lock_);
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
inline void InputReportReader<Report>::ReplyWithReports(ReadInputReportsCompleterBase& completer) {
  std::array<fuchsia_input_report::wire::InputReport,
             fuchsia_input_report::wire::kMaxDeviceReportCount>
      reports;

  TRACE_DURATION("input", "InputReportInstance GetReports", "instance_id", reader_id_);
  size_t num_reports = 0;
  for (; !reports_data_.empty() && num_reports < reports.size(); num_reports++) {
    // Build the report.
    auto input_report = fuchsia_input_report::wire::InputReport::Builder(report_allocator_);

    // Add some common fields. Will be overwritten if set.
    input_report.trace_id(TRACE_NONCE());
    input_report.event_time(zx_clock_get_monotonic());

    reports_data_.front().ToFidlInputReport(input_report, report_allocator_);

    reports[num_reports] = input_report.Build();

    TRACE_FLOW_BEGIN("input", "input_report", reports[num_reports].trace_id());
    reports_data_.pop();
  }

  completer.ReplySuccess(
      fidl::VectorView(fidl::VectorView<fuchsia_input_report::wire::InputReport>::FromExternal(
          reports.data(), num_reports)));

  if (reports_data_.empty()) {
    report_allocator_.Reset();
  }
}

}  // namespace input_report_reader

#endif  // LIB_INPUT_REPORT_READER_READER_H_
