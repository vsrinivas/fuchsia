// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <magenta/vm_object_dispatcher.h>

// An RoDso object describes one DSO image built with the rodso.ld layout.
class RoDso {
public:
    mxtl::RefPtr<VmObjectDispatcher> vmo() {
        return vmo_;
    }
    HandleUniquePtr vmo_handle();

protected:

    RoDso(const char* name, const void* image, size_t size,
          uintptr_t code_start);

    mx_status_t MapAnywhere(mxtl::RefPtr<VmAddressRegionDispatcher> vmar,
                            uintptr_t* start_addr);
    mx_status_t MapFixed(mxtl::RefPtr<VmAddressRegionDispatcher> vmar,
                         uintptr_t start_addr);

private:

    mx_status_t Map(mxtl::RefPtr<VmAddressRegionDispatcher> vmar,
                    uintptr_t* start_addr);

    mx_status_t MapSegment(mxtl::RefPtr<VmAddressRegionDispatcher> vmar,
                           bool code,
                           uintptr_t start_offset,
                           uintptr_t end_offset,
                           uintptr_t* mapped_addr);

    const char* name_;
    mxtl::RefPtr<VmObjectDispatcher> vmo_;
    mx_rights_t vmo_rights_;
    uintptr_t code_start_;
    size_t size_;
};
