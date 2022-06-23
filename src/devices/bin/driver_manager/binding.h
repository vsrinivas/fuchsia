// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_BINDING_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_BINDING_H_

#include <lib/ddk/binding_priv.h>

#include <fbl/array.h>
#include <fbl/macros.h>

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

}  // namespace internal

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_BINDING_H_
