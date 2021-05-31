// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Expect page size to be a compile time constant so we pick a constant here and will assert at run
// time in xhci_init that it matches the actual system page size.
#define XHCI_PAGE_SIZE 4096

// choose ring sizes to allow each ring to fit in a single page
#define COMMAND_RING_SIZE (XHCI_PAGE_SIZE / sizeof(xhci_trb_t))
#define TRANSFER_RING_SIZE ((XHCI_PAGE_SIZE * 16) / sizeof(xhci_trb_t))
#define EVENT_RING_SIZE ((XHCI_PAGE_SIZE * 16) / sizeof(xhci_trb_t))
#define ERST_ARRAY_SIZE \
  (static_cast<uint32_t>((EVENT_RING_SIZE * sizeof(xhci_trb_t)) / XHCI_PAGE_SIZE))
