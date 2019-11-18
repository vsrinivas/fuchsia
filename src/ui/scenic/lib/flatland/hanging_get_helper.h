// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_UI_SCENIC_LIB_FLATLAND_HANGING_GET_HELPER_H_
#define SRC_LIB_UI_SCENIC_LIB_FLATLAND_HANGING_GET_HELPER_H_

#include <lib/fit/function.h>

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
/// Each callback will only be triggered once. Each Update will only trigger a single callback.
/// Update(Data x) is not idempotent. Calling it with the same value will trigger a new
/// execution of a registered callback, even if a previous callback was triggered with the same
/// value.
template <class Data>
class HangingGetHelper {
 public:
  using Callback = fit::function<void(Data)>;

  HangingGetHelper() = default;

  void Update(Data data) {
    data_ = std::move(data);
    SendIfReady();
  }

  void SetCallback(Callback callback) {
    callback_ = std::move(callback);
    SendIfReady();
  }

 private:
  void SendIfReady() {
    if (data_ && callback_) {
      callback_(std::move(data_.value()));
      data_.reset();
      callback_ = nullptr;
    }
  }

  std::optional<Data> data_;
  Callback callback_;
};

}  // namespace flatland

#endif  // SRC_LIB_UI_SCENIC_LIB_FLATLAND_HANGING_GET_HELPER_H_
