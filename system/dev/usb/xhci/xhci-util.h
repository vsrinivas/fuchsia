// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <sync/completion.h>

#include "xhci.h"
#include "xhci-transfer.h"

typedef struct {
    xhci_command_context_t context;
    completion_t completion;
    // from command completion event TRB
    uint32_t status;
    uint32_t control;
} xhci_sync_command_t;

void xhci_sync_command_init(xhci_sync_command_t* command);

// returns condition code
int xhci_sync_command_wait(xhci_sync_command_t* command);

static inline int xhci_sync_command_slot_id(xhci_sync_command_t* command) {
    return (command->control & XHCI_MASK(TRB_SLOT_ID_START, TRB_SLOT_ID_BITS)) >> TRB_SLOT_ID_START;
}

// executes a command with a 1 second timeout
mx_status_t xhci_send_command(xhci_t* xhci, uint32_t command, uint64_t ptr, uint32_t control_bits);
