// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// choose ring sizes to allow each ring to fit in a single page
#define COMMAND_RING_SIZE (PAGE_SIZE / sizeof(xhci_trb_t))
#define TRANSFER_RING_SIZE ((PAGE_SIZE * 16) / sizeof(xhci_trb_t))
#define EVENT_RING_SIZE ((PAGE_SIZE * 16) / sizeof(xhci_trb_t))
#define ERST_ARRAY_SIZE (static_cast<uint32_t>((EVENT_RING_SIZE * sizeof(xhci_trb_t)) / PAGE_SIZE))
