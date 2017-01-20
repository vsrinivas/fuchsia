// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <magenta/handle_owner.h>
#include <magenta/vm_object_dispatcher.h>

// An RoDso object describes one DSO image built with the rodso.ld layout.
class RoDso {
public:
    mxtl::RefPtr<VmObjectDispatcher> vmo() {
        return vmo_;
    }
    HandleOwner vmo_handle();

    size_t size() const { return size_; }

    mx_status_t Map(mxtl::RefPtr<VmAddressRegionDispatcher> vmar,
                    size_t offset);

protected:

    RoDso(const char* name, const void* image, size_t size,
          uintptr_t code_start);

private:

    mx_status_t MapSegment(mxtl::RefPtr<VmAddressRegionDispatcher> vmar,
                           bool code,
                           size_t vmar_offset,
                           size_t start_offset,
                           size_t end_offset);
    const char* name_;
    mxtl::RefPtr<VmObjectDispatcher> vmo_;
    mx_rights_t vmo_rights_;
    uintptr_t code_start_;
    size_t size_;
};
