// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_APP_INSPECTED_CONTAINER_H_
#define SRC_LEDGER_BIN_APP_INSPECTED_CONTAINER_H_

#include <lib/fit/function.h>

#include <variant>
#include <vector>

#include "src/ledger/lib/logging/logging.h"
#include "src/lib/inspect_deprecated/inspect.h"

namespace ledger {

// A helper class that holds callbacks associated with an ongoing inspection until the data required
// to satisfy the inspection is available.
template <typename T>
class InspectedContainer {
 public:
  explicit InspectedContainer(fit::function<void(fit::closure)> callback) {
    CallbackList callbacks;
    callbacks.emplace_back(std::move(callback));
    variant_.template emplace<CallbackList>(std::move(callbacks));
  }
  ~InspectedContainer() = default;

  void SetOnDiscardable(fit::closure on_discardable) {
    LEDGER_DCHECK(!std::holds_alternative<Abandoned>(variant_));
    if (std::holds_alternative<CallbackList>(variant_)) {
      on_discardable_ = std::move(on_discardable);
      return;
    }
    std::get<T>(variant_).SetOnDiscardable(std::move(on_discardable));
  }

  bool IsDiscardable() const {
    if (std::holds_alternative<CallbackList>(variant_)) {
      return std::get<CallbackList>(variant_).empty();
    }
    if (std::holds_alternative<T>(variant_)) {
      return std::get<T>(variant_).IsDiscardable();
    }
    return true;
  }

  // Accepts a callback |callback| associated with some inspection. If this object is not yet
  // matured, |callback| will be stored within this object until this object is matured; otherwise
  // |callback| will be called immediately (though not necessarily synchronously).
  void AddCallback(fit::function<void(fit::closure)> callback) {
    if (std::holds_alternative<Abandoned>(variant_)) {
      callback([] {});
      return;
    }
    if (std::holds_alternative<CallbackList>(variant_)) {
      auto& callbacks = std::get<CallbackList>(variant_);
      LEDGER_DCHECK(!callbacks.empty());
      callbacks.emplace_back(std::move(callback));
      return;
    }
    callback(std::get<T>(variant_).CreateDetacher());
  }

  // Transitions this object from a state of storing callbacks and awaiting data to a state of
  // holding data and passing that data to callbacks. This method is valid to call at most once
  // during the lifetime of ths object.
  template <typename... Args>
  void Mature(Args&&... args) {
    LEDGER_DCHECK(std::holds_alternative<CallbackList>(variant_));
    CallbackList callbacks;
    callbacks.swap(std::get<CallbackList>(variant_));
    LEDGER_DCHECK(!callbacks.empty());

    T& emplaced_inspected = variant_.template emplace<T>(std::forward<Args>(args)...);
    emplaced_inspected.SetOnDiscardable(std::move(on_discardable_));

    // Create a detacher, and keep it alive until all callback have been called
    // with their own detacher. It ensures that if callbacks release the
    // detacher synchronously, this object doesn't become empty until the end of
    // this method.
    auto keep_alive = fit::defer(emplaced_inspected.CreateDetacher());
    for (auto& callback : callbacks) {
      callback(emplaced_inspected.CreateDetacher());
    }
  }

  // Signals to this object that the data for which it is waiting will never arrive, and that this
  // object should call all stored callbacks indicating as much and then signal its emptiness.
  void Abandon() {
    LEDGER_DCHECK(std::holds_alternative<CallbackList>(variant_));
    CallbackList callbacks;
    callbacks.swap(std::get<CallbackList>(variant_));
    variant_.template emplace<Abandoned>();
    for (auto& callback : callbacks) {
      callback([] {});
    }
    LEDGER_DCHECK(IsDiscardable());
    if (on_discardable_) {
      on_discardable_();
    }
  }

 private:
  using CallbackList = std::vector<fit::function<void(fit::closure)>>;
  using Abandoned = std::monostate;

  fit::closure on_discardable_;
  std::variant<CallbackList, T, Abandoned> variant_;
};

}  // namespace ledger

#endif  // SRC_LEDGER_BIN_APP_INSPECTED_CONTAINER_H_
