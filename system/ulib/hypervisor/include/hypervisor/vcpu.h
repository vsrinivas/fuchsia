// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/types.h>

typedef struct zx_vcpu_create_args zx_vcpu_create_args_t;

class Guest;

class Vcpu {
public:
    zx_status_t Init(const Guest& guest, zx_vcpu_create_args_t* args);
    zx_status_t Loop();

    zx_status_t Interrupt(uint32_t vector);
    zx_status_t ReadState(uint32_t kind, void* buffer, uint32_t len) const;
    zx_status_t WriteState(uint32_t kind, const void* buffer, uint32_t len);

private:
    zx_handle_t vcpu_ = ZX_HANDLE_INVALID;
};
