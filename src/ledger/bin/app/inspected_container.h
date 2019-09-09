// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_APP_INSPECTED_CONTAINER_H_
#define SRC_LEDGER_BIN_APP_INSPECTED_CONTAINER_H_

#include <lib/fit/function.h>
#include <lib/inspect_deprecated/inspect.h>

#include <variant>
#include <vector>

namespace ledger {

// A helper class that holds callbacks associated with an ongoing inspection until the data required
// to satisfy the inspection is available.
template <typename T>
class InspectedContainer {
 public:
  explicit InspectedContainer(fit::function<void(fit::closure)> callback) {
    std::vector<fit::function<void(fit::closure)>> callbacks;
    callbacks.emplace_back(std::move(callback));
    variant_.template emplace<std::vector<fit::function<void(fit::closure)>>>(std::move(callbacks));
  }
  ~InspectedContainer() = default;

  void set_on_empty(fit::closure on_empty_callback) {
    if (std::holds_alternative<std::vector<fit::function<void(fit::closure)>>>(variant_)) {
      on_empty_callback_ = std::move(on_empty_callback);
      return;
    }
    std::get<T>(variant_).set_on_empty(std::move(on_empty_callback));
  }

  // Accepts a callback |callback| associated with some inspection. If this object is not yet
  // matured, |callback| will be stored within this object until this object is matured; otherwise
  // |callback| will be called immediately (though not necessarily synchronously).
  void AddCallback(fit::function<void(fit::closure)> callback) {
    if (std::holds_alternative<std::vector<fit::function<void(fit::closure)>>>(variant_)) {
      std::get<std::vector<fit::function<void(fit::closure)>>>(variant_).emplace_back(
          std::move(callback));
      return;
    }
    callback(std::get<T>(variant_).CreateDetacher());
  }

  // Transitions this object from a state of storing callbacks and awaiting data to a state of
  // holding data and passing that data to callbacks. This method is valid to call at most once
  // during the lifetime of ths object.
  template <typename... Args>
  void Mature(Args&&... args) {
    FXL_DCHECK(std::holds_alternative<std::vector<fit::function<void(fit::closure)>>>(variant_));
    std::vector<fit::function<void(fit::closure)>> callbacks;
    callbacks.swap(std::get<std::vector<fit::function<void(fit::closure)>>>(variant_));
    T& emplaced_inspected = variant_.template emplace<T>(std::forward<Args>(args)...);
    emplaced_inspected.set_on_empty(std::move(on_empty_callback_));
    // We create all detachers before passing any of them to the callbacks - the callbacks are at
    // liberty to synchronously call the detachers they are passed, and we don't want to dither
    // between having one, then zero, then one again, and so on outstanding detachers over the
    // course of calling the several callbacks.
    std::vector<fit::closure> detachers;
    detachers.reserve(callbacks.size());
    for (size_t index = 0; index < callbacks.size(); index++) {
      detachers.emplace_back(emplaced_inspected.CreateDetacher());
    }
    size_t index = 0;
    for (fit::function<void(fit::closure)>& callback : callbacks) {
      callback(std::move(detachers[index]));
      index++;
    }
  }

  // Signals to this object that the data for which it is waiting will never arrive, and that this
  // object should call all stored callbacks indicating as much and then signal its emptiness.
  void Abandon() {
    FXL_DCHECK(std::holds_alternative<std::vector<fit::function<void(fit::closure)>>>(variant_));
    for (fit::function<void(fit::closure)>& callback :
         std::get<std::vector<fit::function<void(fit::closure)>>>(variant_)) {
      callback([] {});
    }
    if (on_empty_callback_) {
      on_empty_callback_();
    }
  }

 private:
  fit::closure on_empty_callback_;
  std::variant<std::vector<fit::function<void(fit::closure)>>, T> variant_;
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_APP_INSPECTED_CONTAINER_H_
