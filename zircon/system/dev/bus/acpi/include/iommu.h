// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <threads.h>
#include <zircon/types.h>

__BEGIN_CDECLS;
// Initializes the iommu_manager using the ACPI DMAR table. If this fails,
// the IOMMU manager will be left in a well-defined empty state, and
// iommu_manager_iommu_for_bdf() can still succeed (yielding dummy IOMMU
// handles).
zx_status_t iommu_manager_init(void);

// Returns a handle to the IOMMU that is responsible for the given BDF. The
// returned handle is borrowed from the iommu_manager.  The caller
// must not close the handle.
zx_status_t iommu_manager_iommu_for_bdf(uint32_t bdf, zx_handle_t* iommu);

// Returns a handle to the dummy IOMMU. The returned handle is BORROWED from
// the iommu_manager.
zx_status_t iommu_manager_get_dummy_iommu(zx_handle_t* iommu);

__END_CDECLS;
