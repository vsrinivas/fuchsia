// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "xhci-trb.h"

void xhci_handle_port_changed_event(xhci_t* xhci, xhci_trb_t* trb);
void xhci_handle_rh_port_connected(xhci_t* xhci, int port);
