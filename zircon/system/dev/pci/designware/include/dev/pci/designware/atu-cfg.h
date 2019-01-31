// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/metadata.h>

// CFGd
#define IATU_CFG_APERTURE_METADATA  (0x47464300 | DEVICE_METADATA_PRIVATE)

// MIOd
#define IATU_IO_APERTURE_METADATA   (0x4f494d00 | DEVICE_METADATA_PRIVATE)

// MEMd
#define IATU_MMIO_APERTURE_METADATA (0x4d454d00 | DEVICE_METADATA_PRIVATE)

typedef struct iatu_translation_entry {
	// Address on the CPU's memory bus.
	uint64_t cpu_addr;

	// Address on the PCI bus.
	uint64_t pci_addr;

	// Size of the translation aperture.
	size_t length;
} iatu_translation_entry_t;