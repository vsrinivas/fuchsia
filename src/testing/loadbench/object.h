// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_TESTING_LOADBENCH_OBJECT_H_
#define SRC_TESTING_LOADBENCH_OBJECT_H_

#include <lib/zx/channel.h>
#include <lib/zx/event.h>
#include <lib/zx/port.h>
#include <lib/zx/profile.h>
#include <lib/zx/time.h>
#include <lib/zx/timer.h>

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "src/lib/fxl/logging.h"

// Abstract base for objects.
class Object {
 public:
  virtual ~Object() = default;
  virtual zx_obj_type_t type() const = 0;

  static void CloseHandles() { handles_.clear(); }

 protected:
  // Utility to create a zx::handle-based object. The actual handle is maintained in a static handle
  // table for the lifetime of the workload. The return value is an unowned handle so that
  // subclasses of Object that have handle members may be copiable.
  template <typename T, typename... Args>
  static zx::unowned<T> CreateHandle(Args&&... args) {
    T result;
    const auto status = T::create(std::forward<Args>(args)..., &result);
    FXL_CHECK(status == ZX_OK);

    const auto handle = result.release();
    handles_.emplace_back(handle);

    return zx::unowned<T>{handle};
  }

  // Similar to above for creating peered objects.
  template <typename T, typename... Args>
  static std::pair<zx::unowned<T>, zx::unowned<T>> CreateHandlePair(Args&&... args) {
    std::pair<T, T> result;
    const auto status = T::create(std::forward<Args>(args)..., &result.first, &result.second);
    FXL_CHECK(status == ZX_OK);

    const auto handle_first = result.first.release();
    handles_.emplace_back(handle_first);

    const auto handle_second = result.second.release();
    handles_.emplace_back(handle_second);

    return {zx::unowned<T>{handle_first}, zx::unowned<T>{handle_second}};
  }

 private:
  inline static std::vector<zx::handle> handles_{};
};

// CRTP type that provides common functionality for handle-based objects. ZxType must be an owned
// libzx object type (i.e. not an instantiation of zx::unowned<>).
template <typename T, typename ZxType>
class ObjectBase : public Object {
 public:
  using Base = ObjectBase;
  inline static constexpr auto Type = ZxType::TYPE;

  ~ObjectBase() override {}

  zx_obj_type_t type() const override { return Type; }

  zx::unowned<ZxType>& operator->() { return object_; }

  zx::unowned<ZxType>& object() { return object_; }
  zx::unowned<ZxType>& object() const { return object_; }

  template <typename... Args>
  static std::unique_ptr<T> Create(Args&&... args) {
    return std::make_unique<T>(std::forward<Args>(args)...);
  }

 protected:
  template <typename... Args>
  ObjectBase(Args&&... args) : object_{CreateHandle<ZxType>(std::forward<Args>(args)...)} {}

 private:
  zx::unowned<ZxType> object_;
};

template <typename T, typename ZxType>
class PairObjectBase : public Object {
 public:
  using Base = PairObjectBase;
  inline static constexpr auto Type = ZxType::TYPE;

  ~PairObjectBase() override {}

  zx_obj_type_t type() const override { return Type; }

  auto* operator-> () { return &objects_; }

  auto bind() { return std::tie(objects_.first, objects_.second); }

  template <typename... Args>
  static std::unique_ptr<T> Create(Args&&... args) {
    return std::make_unique<T>(std::forward<Args>(args)...);
  }

 protected:
  template <typename... Args>
  PairObjectBase(Args&&... args)
      : objects_{CreateHandlePair<ZxType>(std::forward<Args>(args)...)} {}

 private:
  std::pair<zx::unowned<ZxType>, zx::unowned<ZxType>> objects_;
};

// Define objects.
struct EventObject : ObjectBase<EventObject, zx::event> {
  EventObject(uint32_t options = 0) : Base{options} {}
};

struct TimerObject : ObjectBase<TimerObject, zx::timer> {
  TimerObject() : Base{ZX_TIMER_SLACK_CENTER, ZX_CLOCK_MONOTONIC} {}
};

struct PortObject : ObjectBase<PortObject, zx::port> {
  PortObject(uint32_t options = 0) : Base{options} {}

  static constexpr zx_signals_t kTerminateSignal = ZX_USER_SIGNAL_0;

  static zx::unowned_event GetTerminateEvent() {
    static zx::unowned_event terminate_event{CreateHandle<zx::event>(0)};
    return zx::unowned_event{terminate_event->get()};
  }
};

struct ChannelObject : PairObjectBase<ChannelObject, zx::channel> {
  ChannelObject(uint32_t options = 0) : Base{options} {}
};

#endif  // SRC_TESTING_LOADBENCH_OBJECT_H_
