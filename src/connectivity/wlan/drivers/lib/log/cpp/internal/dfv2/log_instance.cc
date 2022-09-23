// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <zircon/assert.h>

#include <wlan/drivers/log_instance.h>

namespace wlan::drivers::log {

// static
void Instance::Init(uint32_t filter, driver::Logger&& logger) {
  Instance& inst = get();

  ZX_ASSERT(!inst.initialized_);
  inst.initialized_ = true;
  inst.filter_ = filter;
  inst.logger_ = std::move(logger);
}

// static
bool Instance::IsFilterOn(uint32_t filter) {
  Instance& inst = get();
  ZX_ASSERT(inst.initialized_);
  return (inst.filter_ & filter) != 0;
}

// static
driver::Logger& Instance::GetLogger() {
  Instance& inst = get();
  ZX_ASSERT(inst.initialized_);
  return inst.logger_;
}

// static
Instance& Instance::get() {
  static Instance inst{};
  return inst;
}

}  // namespace wlan::drivers::log
