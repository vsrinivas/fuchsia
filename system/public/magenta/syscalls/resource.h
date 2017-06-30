// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

__BEGIN_CDECLS

// The root resource
#define MX_RSRC_KIND_ROOT    (0x0000u)

// Hardware resources
#define MX_RSRC_KIND_MMIO    (0x1000u)
#define MX_RSRC_KIND_IOPORT  (0x1001u)
#define MX_RSRC_KIND_IRQ     (0x1002u)

__END_CDECLS
