// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <fbl/array.h>

#include "binding_internal.h"
#include "coordinator.h"
#include "device.h"
#include "src/devices/lib/log/log.h"

namespace internal {

uint32_t LookupBindProperty(BindProgramContext* ctx, uint32_t id) {
  for (const auto prop : *ctx->props) {
    if (prop.id == id) {
      return prop.value;
    }
  }

  // fallback for devices without properties
  switch (id) {
    case BIND_PROTOCOL:
      return ctx->protocol_id;
    case BIND_AUTOBIND:
      return ctx->autobind;
    default:
      // TODO: better process for missing properties
      return 0;
  }
}

bool EvaluateBindProgram(BindProgramContext* ctx) {
  const zx_bind_inst_t* ip = ctx->binding;
  const zx_bind_inst_t* end = ip + (ctx->binding_size / sizeof(zx_bind_inst_t));
  uint32_t flags = 0;

  while (ip < end) {
    uint32_t inst = ip->op;
    bool cond;

    if (BINDINST_CC(inst) != COND_AL) {
      uint32_t value = ip->arg;
      uint32_t pid = BINDINST_PB(inst);
      uint32_t pval;
      if (pid != BIND_FLAGS) {
        pval = LookupBindProperty(ctx, pid);
      } else {
        pval = flags;
      }

      // evaluate condition
      switch (BINDINST_CC(inst)) {
        case COND_EQ:
          cond = (pval == value);
          break;
        case COND_NE:
          cond = (pval != value);
          break;
        case COND_LT:
        case COND_GT:
        case COND_LE:
        case COND_GE:
          LOGF(ERROR, "Driver '%s' has deprecated inequality bind instruction %#08x", ctx->name,
               inst);
          return false;
        default:
          // illegal instruction: abort
          LOGF(ERROR, "Driver '%s' has illegal bind instruction %#08x", ctx->name, inst);
          return false;
      }
    } else {
      cond = true;
    }

    if (cond) {
      switch (BINDINST_OP(inst)) {
        case OP_ABORT:
          return false;
        case OP_MATCH:
          return true;
        case OP_GOTO: {
          uint32_t label = BINDINST_PA(inst);
          while (++ip < end) {
            if ((BINDINST_OP(ip->op) == OP_LABEL) && (BINDINST_PA(ip->op) == label)) {
              goto next_instruction;
            }
          }
          LOGF(ERROR, "Driver '%s' illegal GOTO", ctx->name);
          return false;
        }
        case OP_LABEL:
          // no op
          break;
        default:
          // illegal instruction: abort
          LOGF(ERROR, "Driver '%s' illegal bind instruction %#08x", ctx->name, inst);
          return false;
      }
    }

  next_instruction:
    ip++;
  }

  // default if we leave the program is no-match
  return false;
}

Match SumMatchCounts(Match m1, Match m2) {
  switch (m1) {
    case Match::None:
      return m2;
    case Match::One:
      return (m2 == Match::None ? Match::One : Match::Many);
    case Match::Many:
      return Match::Many;
  }
  __builtin_trap();
}

// Instantiate MatchParts<Device>
template Match MatchParts(const fbl::RefPtr<Device>& device, const FragmentPartDescriptor* parts,
                          uint32_t parts_count);

}  // namespace internal

bool driver_is_bindable(const Driver* drv, uint32_t protocol_id,
                        const fbl::Array<const zx_device_prop_t>& props, bool autobind) {
  internal::BindProgramContext ctx;
  ctx.props = &props;
  ctx.protocol_id = protocol_id;
  ctx.binding = drv->binding.get();
  ctx.binding_size = drv->binding_size;
  ctx.name = drv->name.c_str();
  ctx.autobind = autobind ? 1 : 0;
  return internal::EvaluateBindProgram(&ctx);
}
