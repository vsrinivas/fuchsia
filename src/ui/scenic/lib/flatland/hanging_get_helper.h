// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_FLATLAND_HANGING_GET_HELPER_H_
#define SRC_UI_SCENIC_LIB_FLATLAND_HANGING_GET_HELPER_H_

#include <fuchsia/ui/composition/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>
#include <lib/syslog/cpp/macros.h>

#include <mutex>

#include "src/ui/scenic/lib/utils/dispatcher_holder.h"

namespace flatland {
/// A helper class for managing [hanging get
/// semantics](https://fuchsia.dev/fuchsia-src/development/api/fidl.md#delay-responses-using-hanging-gets).
/// It responds with the most recently updated value.
///
/// For each hanging get method in a FIDL interface, like GetData() -> ( Data response ), create one
/// of these classes. Any time the response should change, call Update(Data x). Any time the client
/// calls GetFoo(), set the callback on this helper. Once the callback has been set and the data has
/// been updated, the callback will be triggered with the new data.
///
/// Each callback will only be triggered once. Each Update will only trigger, at most, a single
/// callback. Update(Data x) is idempotent. Calling it with the same value will not trigger a new
/// execution of a registered callback, nor will it remove the registered callback.
///
/// The templated Data parameter must be a FIDL type, one that supports both fidl::Clone and
/// fidl::Equals.
template <class Data>
class HangingGetHelper {
 public:
  using Callback = fit::function<void(Data)>;

  // Hanging get responses are sent on the provided |dispatcher|.
  // TODO(fxbug.dev/76183): |dispatcher| is used because the FIDL HLCPP bindings require method
  // responses to be sent from the same thread that handled the method.  This may change in the
  // future; if so the |dispatcher| may no longer be necessary.
  explicit HangingGetHelper(std::shared_ptr<utils::DispatcherHolder> dispatcher_holder)
      : dispatcher_holder_(std::move(dispatcher_holder)) {
    FX_DCHECK(dispatcher_holder_);
  }

  void Update(Data data) {
    std::lock_guard<std::mutex> guard(mutex_);

    if (last_data_ && fidl::Equals(last_data_.value(), data)) {
      return;
    }

    data_ = std::move(data);
    SendIfReady();
  }

  void SetCallback(Callback callback) {
    std::lock_guard<std::mutex> guard(mutex_);

    callback_ = std::move(callback);
    SendIfReady();
  }

  bool HasPendingCallback() {
    std::lock_guard<std::mutex> guard(mutex_);
    return static_cast<bool>(callback_);
  }

 private:
  void SendIfReady() {
    if (data_ && callback_) {
      last_data_ = Data();
      fidl::Clone(data_.value(), &last_data_.value());

      // Invoke callback on the appropriate Flatland instance thread.
      auto status = async::PostTask(
          dispatcher_holder_->dispatcher(),
          [data{std::move(data_.value())}, callback{std::move(callback_)}]() mutable {
            callback(std::move(data));
          });
      FX_DCHECK(status == ZX_OK);

      data_.reset();
      callback_ = nullptr;
    }
  }

  std::shared_ptr<utils::DispatcherHolder> dispatcher_holder_;
  std::mutex mutex_;
  std::optional<Data> data_;
  std::optional<Data> last_data_;
  Callback callback_;
};

}  // namespace flatland

#endif  // SRC_UI_SCENIC_LIB_FLATLAND_HANGING_GET_HELPER_H_
