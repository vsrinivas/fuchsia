// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

__BEGIN_CDECLS;

// Structure for passing around PCI address information
typedef struct pci_bdf {
    uint8_t bus_id;
    uint8_t device_id;
    uint8_t function_id;
} pci_bdf_t;

__END_CDECLS;
