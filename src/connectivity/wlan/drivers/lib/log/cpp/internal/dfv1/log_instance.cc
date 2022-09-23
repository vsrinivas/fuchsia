// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/drivers/log_instance.h>

namespace wlan::drivers::log {

// static
void Instance::Init(uint32_t filter) { get().filter_ = filter; }

// static
bool Instance::IsFilterOn(uint32_t filter) { return get().filter_ & filter; }

// static
Instance& Instance::get() {
  static Instance w;
  return w;
}

}  // namespace wlan::drivers::log
