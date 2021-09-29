// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/response.h"

#include <lib/async/cpp/task.h>
#include <lib/syslog/cpp/macros.h>

namespace fuzzing {
namespace {

struct CallbackVisitor {
  async_dispatcher_t* dispatcher = nullptr;
  zx_status_t status = ZX_OK;
  Result result = Result::NO_ERRORS;
  FidlInput fidl_input;

  void operator()(std::monostate& callback) { FX_NOTREACHED(); }

  void operator()(Response::InputCallback& callback) {
    if (status == ZX_OK) {
      async::PostTask(dispatcher, [callback = std::move(callback),
                                   fidl_input = std::move(fidl_input)]() mutable {
        callback(std::move(fidl_input));
      });
    } else {
      FX_NOTREACHED();
    }
  }

  void operator()(Response::ResultAndInputCallback& callback) {
    if (status == ZX_OK) {
      async::PostTask(dispatcher, [callback = std::move(callback), result = result,
                                   fidl_input = std::move(fidl_input)]() mutable {
        callback(result, std::move(fidl_input));
      });
    } else {
      FX_NOTREACHED();
    }
  }

  // NOLINTNEXTLINE(readability-make-member-function-const)
  void operator()(Response::StatusCallback& callback) {
    async::PostTask(dispatcher,
                    [callback = std::move(callback), status = status]() { callback(status); });
  }

  // NOLINTNEXTLINE(readability-make-member-function-const)
  void operator()(Response::ResultAndStatusCallback& callback) {
    if (status == ZX_OK) {
      async::PostTask(dispatcher, [callback = std::move(callback), result = result]() {
        callback(fit::ok(result));
      });
    } else {
      async::PostTask(dispatcher, [callback = std::move(callback), status = status]() {
        callback(fit::error(status));
      });
    }
  }

  void operator()(Response::InputAndStatusCallback& callback) {
    if (status == ZX_OK) {
      async::PostTask(dispatcher, [callback = std::move(callback),
                                   fidl_input = std::move(fidl_input)]() mutable {
        callback(fit::ok(std::move(fidl_input)));
      });
    } else {
      async::PostTask(dispatcher, [callback = std::move(callback), status = status]() {
        callback(fit::error(status));
      });
    }
  }

  void operator()(Response::FullCallback& callback) {
    if (status == ZX_OK) {
      async::PostTask(dispatcher, [callback = std::move(callback), result = result,
                                   fidl_input = std::move(fidl_input)]() mutable {
        callback(fit::ok(std::make_tuple(result, std::move(fidl_input))));
      });
    } else {
      async::PostTask(dispatcher, [callback = std::move(callback), status = status]() {
        callback(fit::error(status));
      });
    }
  }
};

}  // namespace

Response& Response::operator=(Response&& other) noexcept {
  dispatcher_ = other.dispatcher_;
  transceiver_ = other.transceiver_;
  callback_ = std::move(other.callback_);
  return *this;
}

void Response::Send(zx_status_t status) { SendImpl(status, Result::NO_ERRORS, FidlInput()); }

void Response::Send(zx_status_t status, Result result, const Input& input) {
  if (status == ZX_OK) {
    transceiver_->Transmit(input, [this, result](FidlInput&& fidl_input) {
      SendImpl(ZX_OK, result, std::move(fidl_input));
    });
  } else {
    SendImpl(status, Result::NO_ERRORS, FidlInput());
  }
}

void Response::SendImpl(zx_status_t status, Result result, FidlInput fidl_input) {
  std::visit(CallbackVisitor{dispatcher_, status, result, std::move(fidl_input)}, callback_);
  callback_ = std::monostate{};
}

}  // namespace fuzzing
