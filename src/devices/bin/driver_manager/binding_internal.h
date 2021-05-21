// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_BINDING_INTERNAL_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_BINDING_INTERNAL_H_

#include <lib/ddk/binding.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <stdio.h>

#include <fbl/array.h>
#include <fbl/macros.h>

#include "composite_device.h"
#include "coordinator.h"
#include "device.h"

namespace internal {

struct BindProgramContext {
  const fbl::Array<const zx_device_prop_t>* props;
  uint32_t protocol_id;
  size_t binding_size;
  const zx_bind_inst_t* binding;
  const char* name;
  uint32_t autobind;
};

uint32_t LookupBindProperty(BindProgramContext* ctx, uint32_t id);
bool EvaluateBindProgram(BindProgramContext* ctx);

template <typename T>
bool EvaluateBindProgram(const fbl::RefPtr<T>& device, const char* drv_name,
                         const fbl::Array<const zx_bind_inst_t>& bind_program, bool autobind) {
  BindProgramContext ctx;
  ctx.props = &device->props();
  ctx.protocol_id = device->protocol_id();
  ctx.binding = bind_program.data();
  ctx.binding_size = bind_program.size() * sizeof(bind_program[0]);
  ctx.name = drv_name;
  ctx.autobind = autobind ? 1 : 0;
  return EvaluateBindProgram(&ctx);
}

}  // namespace internal

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_BINDING_INTERNAL_H_
