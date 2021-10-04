// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_COMMON_RESPONSE_H_
#define SRC_SYS_FUZZING_COMMON_RESPONSE_H_

#include <fuchsia/fuzzer/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>
#include <lib/fit/result.h>

#include <tuple>
#include <variant>

#include "src/lib/fxl/macros.h"
#include "src/sys/fuzzing/common/dispatcher.h"
#include "src/sys/fuzzing/common/input.h"
#include "src/sys/fuzzing/common/transceiver.h"

namespace fuzzing {

using ::fuchsia::fuzzer::Result;

// This class wraps the various FIDL callbacks for |fuchsia.fuzzer.Controller| and "hides the
// ugliness", that is, it encapsulates a lot of the details in responding asynchronously via a FIDL
// dispatcher.
class Response final {
 public:
  using InputCallback = fit::function<void(FidlInput)>;
  using ResultAndInputCallback = fit::function<void(Result, FidlInput)>;
  using StatusCallback = fit::function<void(zx_status_t)>;
  using ResultAndStatusCallback = fit::function<void(fit::result<Result, zx_status_t>)>;
  using InputAndStatusCallback = fit::function<void(fit::result<FidlInput, zx_status_t>)>;
  using FullCallback = fit::function<void(fit::result<std::tuple<Result, FidlInput>, zx_status_t>)>;

  Response() = default;
  Response(Response&& other) noexcept { *this = std::move(other); }
  ~Response() = default;

  Response& operator=(Response&& other) noexcept;

  void set_dispatcher(const std::shared_ptr<Dispatcher>& dispatcher) { dispatcher_ = dispatcher; }
  void set_transceiver(const std::shared_ptr<Transceiver>& transceiver) {
    transceiver_ = transceiver;
  }

  template <typename Callback>
  void set_callback(Callback&& callback) {
    callback_ = std::move(callback);
  }

  // Respond with a |status|.If this response uses a |fit::result| the status will be sent as a
  // |fit::error|. It is an error to call either version of |send| after calling this method.
  void Send(zx_status_t status);

  // Respond with the appropriate combination of |status|, |result|, and |input|, depending on
  // the callback provided when the object was constructed. It is an error to call either version of
  // |send| after calling this method.
  void Send(zx_status_t status, Result result, const Input& input);

 private:
  void SendImpl(zx_status_t status, Result result, FidlInput input);

  std::shared_ptr<Dispatcher> dispatcher_;
  std::shared_ptr<Transceiver> transceiver_;
  std::variant<std::monostate, InputCallback, ResultAndInputCallback, StatusCallback,
               ResultAndStatusCallback, InputAndStatusCallback, FullCallback>
      callback_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Response);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_COMMON_RESPONSE_H_
