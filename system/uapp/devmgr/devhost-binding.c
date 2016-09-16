// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>

#include <stdio.h>

static uint32_t dev_get_prop(mx_device_t* dev, uint32_t id) {
    const mx_device_prop_t* props = dev->props;
    const mx_device_prop_t* end = dev->props + dev->prop_count;

    while (props < end) {
        if (props->id == id) {
            return props->value;
        }
        props++;
    }

    // fallback for devices without properties
    if (id == BIND_PROTOCOL) {
        return dev->protocol_id;
    }

    // TODO: better process for missing properties
    return 0;
}

bool devhost_is_bindable(mx_driver_t* drv, mx_device_t* dev) {
    const mx_bind_inst_t* ip = drv->binding;
    const mx_bind_inst_t* end = ip + (drv->binding_size / sizeof(mx_bind_inst_t));
    uint32_t flags = 0;

    while (ip < end) {
        uint32_t inst = ip->op;
        bool cond;

        if (BINDINST_CC(inst) != COND_AL) {
            uint32_t value = ip->arg;
            uint32_t pid = BINDINST_PB(inst);
            uint32_t pval;
            if (pid != BIND_FLAGS) {
                pval = dev_get_prop(dev, pid);
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
                printf("devmgr: dev %p illegal bindinst 0x%08x\n", dev, inst);
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
                printf("devmgr: dev %p illegal GOTO\n", dev);
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
                printf("devmgr: dev %p illegal bindinst 0x%08x\n", dev, inst);
                return false;
            }
        }

next_instruction:
        ip++;
    }

    // default if we leave the program is no-match
    return false;
}