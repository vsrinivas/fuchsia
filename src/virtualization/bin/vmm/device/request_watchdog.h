// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_DEVICE_REQUEST_WATCHDOG_H_
#define SRC_VIRTUALIZATION_BIN_VMM_DEVICE_REQUEST_WATCHDOG_H_

#include <lib/async/cpp/executor.h>
#include <lib/async/cpp/task.h>
#include <lib/async/cpp/time.h>
#include <lib/syslog/cpp/macros.h>

#include <string>

#include <fbl/intrusive_double_list.h>
#include <fbl/string_printf.h>

// RequestWatchdog allows outstanding operations to be tracked, a have
// a warning printed to the syslog if the operation takes longer than
// a predefined timeout duration.
//
// For example, a system processing requests from a client would have a single
// RequestWatchdog instance defined as follows:
//
//   RequestWatchdog<std::string> watchdog(dispatcher, /*deadline=*/zx::sec(10));
//
// Each time a new request to the service is made, a call to `Start` is made.
// The returned token should be stored with the request, as follows:
//
//   class Request {
//     // ...
//     Request(RequestWatchdog<std::string>& watchdog, std::string request_name) {
//       token_ = watchdog.Start(std::move(request_name));
//     }
//
//     // ...
//     RequestWatchdog<std::string>::RequestToken token_;
//   }
//
// When the RequestWatchdog::RequestToken object is destroyed, the watchdog is
// informed, and it will stop monitoring the request. If the token remains
// alive for more than deadline (ten seconds in this example), then a warning
// will be printed to the syslog. The log message will contain the value
// passed to Start. This can be any type that can be printed to an ostream.
//
// This class uses the async::Loop library, and thus is thread-hostile. All
// operations on the class (including construction and destruction) should
// take place on the same thread.
template <typename T>
class RequestWatchdog {
 public:
  class RequestToken;

  // Create a new watchdog using the given dispatcher, per-request deadline,
  // and polling interval.
  //
  // `dispatcher` must outlive the class instance.
  explicit RequestWatchdog(async_dispatcher_t* dispatcher, zx::duration deadline = kDefaultDeadline,
                           zx::duration poll_interval = kDefaultPollInterval)
      : dispatcher_(dispatcher), deadline_(deadline), poll_interval_(poll_interval) {
    polling_task_.set_handler(
        [this](async_dispatcher_t*, async::Task*, zx_status_t status) { Poll(status); });
    polling_task_.PostDelayed(dispatcher_, poll_interval_);
  }

  ~RequestWatchdog() {
    FX_CHECK(active_requests_.is_empty())
        << "RequestWatchdog destroyed while requests are still active.";
  }

  // Create a new token tracking an outstanding request.
  RequestToken Start(T status) {
    RequestToken result(this, async::Now(dispatcher_), std::move(status));
    active_requests_.push_back(&result);
    return result;
  }

  // By default, we warn about requests that are alive for longer than this duration.
  static constexpr zx::duration kDefaultDeadline = zx::sec(30);

  // By default, we poll for long-lived requests this often.
  static constexpr zx::duration kDefaultPollInterval = zx::sec(1);

 private:
  // Maximum number of requests to print per poll.
  static constexpr size_t kMaxRequestsToPrint = 5;

  // Mark that given request as being complete.
  void Complete(RequestToken* request) {
    if (request->InContainer()) {
      active_requests_.erase(*request);
    }
  }

  // Warn about long-running requests.
  void PrintLongRunningRequests() {
    size_t num_old_requests = 0;
    zx::time now = async::Now(dispatcher_);

    // Print all requests older than `deadline_`.
    while (!active_requests_.is_empty()) {
      RequestToken& request = active_requests_.front();

      // Requests are ordered from oldest to newest: if we see a young
      // request, we don't need to keep searching.
      zx::duration age = now - request.start_time_;
      if (age < deadline_) {
        break;
      }

      // Print a warning.
      if (num_old_requests++ < kMaxRequestsToPrint) {
        FX_LOGS(WARNING) << "request_watchdog: Request has been active for "
                         << fbl::StringPrintf("%0.2lfs",
                                              static_cast<double>(age.to_usecs()) / 1'000'000.)
                         << ": " << request.status();
      }

      // Don't warn about this request again.
      active_requests_.erase(request);
    }

    // Warn if there were more requests that we didn't print.
    if (num_old_requests > kMaxRequestsToPrint) {
      FX_LOGS(WARNING) << "request_watchdog: " << (num_old_requests - kMaxRequestsToPrint)
                       << " additional request(s) have been active for more than "
                       << fbl::StringPrintf("%0.1lfs",
                                            static_cast<double>(deadline_.to_usecs()) / 1'000'000.);
    }
  }

  // Poll for long-running requests.
  void Poll(zx_status_t status) {
    if (status != ZX_OK) {
      return;
    }

    PrintLongRunningRequests();

    FX_DCHECK(!polling_task_.is_pending());
    polling_task_.PostDelayed(dispatcher_, poll_interval_);
  }

  async_dispatcher_t* dispatcher_;  // Owned elsewhere.
  async::Task polling_task_;

  zx::duration deadline_;       // Warn when requests exceed this age.
  zx::duration poll_interval_;  // Interval we poll for old requests.

  // List of active requests.
  //
  // The list is stored in the order that the requests were started. That is,
  // the oldest requests are at the start of the list.
  fbl::SizedDoublyLinkedList<RequestToken*> active_requests_;
};

template <typename T>
class RequestWatchdog<T>::RequestToken : public fbl::DoublyLinkedListable<RequestToken*> {
 public:
  RequestToken() = default;
  ~RequestToken() { reset(); }

  // Get the RequestToken's status payload.
  const T& status() { return status_; }

  // Indicate that this request has completed.
  void reset() {
    if (parent_ != nullptr) {
      parent_->Complete(this);
    }
    parent_ = nullptr;
  }

  // Allow move of RequestToken objects.
  RequestToken& operator=(RequestToken&& other) noexcept {
    // Copy parent/start_time/status.
    parent_ = other.parent_;
    start_time_ = other.start_time_;
    status_ = std::move(other.status_);

    // If our source was in the container, insert the new RequestToken into
    // the container and remove the old RequestToken.
    if (other.InContainer()) {
      parent_->active_requests_.insert_after(parent_->active_requests_.make_iterator(other), this);
      parent_->active_requests_.erase(other);
    }

    // Deactivate the original request.
    other.parent_ = nullptr;

    return *this;
  }
  RequestToken(RequestToken&& other) noexcept { *this = std::move(other); }

 private:
  explicit RequestToken(RequestWatchdog* parent, zx::time start_time, T status)
      : parent_(parent), start_time_(start_time), status_(std::move(status)) {}
  friend RequestWatchdog;

  RequestWatchdog* parent_;
  zx::time start_time_;  // Time the request started.
  T status_;
};

#endif  // SRC_VIRTUALIZATION_BIN_VMM_DEVICE_REQUEST_WATCHDOG_H_
