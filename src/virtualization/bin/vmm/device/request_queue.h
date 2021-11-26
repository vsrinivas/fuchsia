// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_DEVICE_REQUEST_QUEUE_H_
#define SRC_VIRTUALIZATION_BIN_VMM_DEVICE_REQUEST_QUEUE_H_

#include <lib/async/cpp/task.h>
#include <lib/syslog/cpp/macros.h>

#include <queue>

// Implements a FIFO queue for requests, limiting the number of in-flight requests
// to a given value.
//
// Callbacks can be put on the queue by calling `RequestQueue::Dispatch`. When
// the callback is ready to run, it will be given an instance of
// a `RequestQueue::Request`. When this request object is destroyed (or
// `Finish` is called), a new request will be allowed to start:
//
//   RequestQueue queue(dispatcher, /*max_in_flight=*/10);
//   ...
//
//   queue.Dispatch([](RequestQueue::Request request) {
//     // Start a long-running operation, which runs a callback when complete.
//     ReadFile("my_file.txt", [request = std::move(request)]() {
//       // Indicate that the request has completed.
//       request.Finish();
//     });
//   });
//
class RequestQueue {
 public:
  explicit RequestQueue(async_dispatcher_t* dispatcher, size_t max_in_flight)
      : dispatcher_(dispatcher), available_requests_(max_in_flight) {}

  // Prevent copy and move.
  RequestQueue(const RequestQueue&) = delete;
  RequestQueue& operator=(const RequestQueue&) = delete;

  // Run the given function when enough resources are available.
  //
  // The function will be given a RequestQueue::Request; when the request is complete,
  // the object should be destroyed or RequestQueue::Request::Finish() called.
  template <typename F>
  void Dispatch(F&& function);

  // A Request object indicates when a request has been completed.
  class Request {
   public:
    Request() = default;
    ~Request();

    // Move only.
    Request(Request&& other) noexcept { *this = std::move(other); }
    Request& operator=(Request&& other) noexcept;

    // Mark this request as complete.
    void Finish();

   private:
    explicit Request(RequestQueue* parent) : parent_(parent) {}
    RequestQueue* parent_ = nullptr;
    friend class RequestQueue;
  };

 private:
  // Called when a request has been completed.
  void RequestDone();

  async_dispatcher_t* dispatcher_;
  size_t available_requests_;  // Number of new requests we are able to issue.
  std::queue<fit::function<void(Request)>> requests_;
};

// Implementation details below.

template <typename F>
void RequestQueue::Dispatch(F&& function) {
  // If we have any free requests available, dispatch directly.
  if (available_requests_ > 0) {
    available_requests_--;
    function(Request(this));
    return;
  }

  // Otherwise, enqueue the request.
  requests_.emplace(std::forward<F>(function));
}

#endif  // SRC_VIRTUALIZATION_BIN_VMM_DEVICE_REQUEST_QUEUE_H_
