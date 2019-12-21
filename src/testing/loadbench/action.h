// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_TESTING_LOADBENCH_ACTION_H_
#define SRC_TESTING_LOADBENCH_ACTION_H_

#include <memory>
#include <utility>

// Forward declaration.
class Worker;

// Abstract base for actions that worker threads can perform.
struct Action {
  virtual ~Action() = default;

  // Performs one-time setup of this action on its host worker. The same Worker
  // instance that is passed to this method is passed to each subsequent
  // invocation of Perform().
  virtual void Setup(Worker* worker) {}

  // Performs the action by/on the given worker.
  virtual void Perform(Worker* worker) = 0;

  // Copies the action. Copy is only called prior to the invocation of Setup().
  virtual std::unique_ptr<Action> Copy() const = 0;
};

enum class ActionDefaultCopyable : bool {
  False = false,
  True = true,
};

template <typename T, ActionDefaultCopyable = ActionDefaultCopyable::True>
struct ActionBase : Action {
  template <typename... Args>
  static std::unique_ptr<T> Create(Args&&... args) {
    return std::make_unique<T>(std::forward<Args>(args)...);
  }

  std::unique_ptr<Action> Copy() const override {
    return std::make_unique<T>(static_cast<const T&>(*this));
  }
};

template <typename T>
struct ActionBase<T, ActionDefaultCopyable::False> : Action {
  template <typename... Args>
  static std::unique_ptr<T> Create(Args&&... args) {
    return std::make_unique<T>(std::forward<Args>(args)...);
  }
};

#endif  // SRC_TESTING_LOADBENCH_ACTION_H_
