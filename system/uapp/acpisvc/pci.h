// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/syscalls-types.h>

mx_status_t get_pci_init_arg(mx_pci_init_arg_t** arg, uint32_t* size);
