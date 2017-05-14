// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>

#include <stdio.h>

#include "devcoordinator.h"

typedef struct {
    const mx_device_prop_t* props;
    const mx_device_prop_t* end;
    uint32_t protocol_id;
    uint32_t binding_size;
    const mx_bind_inst_t* binding;
    const char* name;
    uint32_t autobind;
} bpctx_t;

static uint32_t dev_get_prop(bpctx_t* ctx, uint32_t id) {
    const mx_device_prop_t* props = ctx->props;
    const mx_device_prop_t* end = ctx->end;

    while (props < end) {
        if (props->id == id) {
            return props->value;
        }
        props++;
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

static bool is_bindable(bpctx_t* ctx) {
    const mx_bind_inst_t* ip = ctx->binding;
    const mx_bind_inst_t* end = ip + (ctx->binding_size / sizeof(mx_bind_inst_t));
    uint32_t flags = 0;

    while (ip < end) {
        uint32_t inst = ip->op;
        bool cond;

        if (BINDINST_CC(inst) != COND_AL) {
            uint32_t value = ip->arg;
            uint32_t pid = BINDINST_PB(inst);
            uint32_t pval;
            if (pid != BIND_FLAGS) {
                pval = dev_get_prop(ctx, pid);
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
                cond = (pval < value);
                break;
            case COND_GT:
                cond = (pval > value);
                break;
            case COND_LE:
                cond = (pval <= value);
                break;
            case COND_GE:
                cond = (pval >= value);
                break;
            case COND_MASK:
                cond = ((pval & value) != 0);
                break;
            case COND_BITS:
                cond = ((pval & value) == value);
                break;
            default:
                // illegal instruction: abort
                printf("devmgr: driver '%s' illegal bindinst 0x%08x\n", ctx->name, inst);
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
                    if ((BINDINST_OP(ip->op) == OP_LABEL) &&
                        (BINDINST_PA(ip->op) == label)) {
                        goto next_instruction;
                    }
                }
                printf("devmgr: driver '%s' illegal GOTO\n", ctx->name);
                return false;
            }
            case OP_SET:
                flags |= BINDINST_PA(inst);
                break;
            case OP_CLEAR:
                flags &= ~(BINDINST_PA(inst));
                break;
            case OP_LABEL:
                // no op
                break;
            default:
                // illegal instruction: abort
                printf("devmgr: driver '%s' illegal bindinst 0x%08x\n", ctx->name, inst);
                return false;
            }
        }

next_instruction:
        ip++;
    }

    // default if we leave the program is no-match
    return false;
}

bool dc_is_bindable(driver_t* drv, uint32_t protocol_id,
                    mx_device_prop_t* props, size_t prop_count,
                    bool autobind) {
    if (drv->binding_size == 0) {
        return false;
    }
    bpctx_t ctx;
    ctx.props = props;
    ctx.end = props + prop_count;
    ctx.protocol_id = protocol_id;
    ctx.binding = drv->binding;
    ctx.binding_size = drv->binding_size;
    ctx.name = drv->name;
    ctx.autobind = autobind ? 1 : 0;
    return is_bindable(&ctx);
}
