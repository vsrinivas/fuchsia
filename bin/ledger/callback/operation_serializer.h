// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_CALLBACK_OPERATION_SERIALIZER_H_
#define APPS_LEDGER_SRC_CALLBACK_OPERATION_SERIALIZER_H_

#include <functional>
#include <queue>

#include "lib/fxl/functional/closure.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"

namespace callback {

// OperationSerializer can be used to serialize a set of operations. A typical
// usage example would be:
//     OperationSerializer<Status> serializer;
//
// and then for each operation to be serialized:
//     std::function<void(Status)> on_done = ...;
//     serializer.Serialize(std::move(on_done),
//                          [](std::function<void(Status)> callback) {
//                            // Code for the operation...
//                            callback(Status::/* result */);
//                          });
template <class... C>
class OperationSerializer {
 public:
  OperationSerializer() {}
  ~OperationSerializer() {}

  // Queues operations so that they are serialized: an operation is executed
  // only when all previous operations registered through this method have
  // terminated by calling their callbacks. When |operation| terminates,
  // |callback| is called with the result returned by |operation|.
  void Serialize(std::function<void(C...)> callback,
                 std::function<void(std::function<void(C...)>)> operation) {
    auto closure = [
      this, callback = std::move(callback), operation = std::move(operation)
    ] {
      operation([ this, callback = std::move(callback) ](C... args) {
        callback(args...);
        queued_operations_.pop();
        if (!queued_operations_.empty()) {
          queued_operations_.front()();
        } else if (on_empty_) {
          on_empty_();
        }
      });
    };
    queued_operations_.emplace(std::move(closure));
    if (queued_operations_.size() == 1) {
      queued_operations_.front()();
    }
  }

  // Returns true if there are no more operations in the queue or false
  // otherwise.
  bool empty() { return queued_operations_.empty(); }

  void set_on_empty(fxl::Closure on_empty) { on_empty_ = on_empty; }

 private:
  std::queue<fxl::Closure> queued_operations_;
  fxl::Closure on_empty_;

  FXL_DISALLOW_COPY_AND_ASSIGN(OperationSerializer);
};

}  // namespace callback

#endif  // APPS_LEDGER_SRC_CALLBACK_OPERATION_SERIALIZER_H_
