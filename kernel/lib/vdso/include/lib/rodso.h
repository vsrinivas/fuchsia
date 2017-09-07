// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <object/handle_owner.h>
#include <object/vm_object_dispatcher.h>

// An RoDso object describes one DSO image built with the rodso.ld layout.
class RoDso {
public:
    fbl::RefPtr<VmObjectDispatcher> vmo() const {
        return vmo_;
    }
    HandleOwner vmo_handle() const;

    size_t size() const { return size_; }

    bool valid_code_mapping(uint64_t vmo_offset, size_t size) const {
        return vmo_offset == code_start_ && size == size_ - code_start_;
    }

    mx_status_t Map(fbl::RefPtr<VmAddressRegionDispatcher> vmar,
                    size_t offset) const;

protected:

    RoDso(const char* name, const void* image, size_t size,
          uintptr_t code_start);

    mx_rights_t vmo_rights() const { return vmo_rights_; }

private:

    mx_status_t MapSegment(fbl::RefPtr<VmAddressRegionDispatcher> vmar,
                           bool code,
                           size_t vmar_offset,
                           size_t start_offset,
                           size_t end_offset) const;

    const char* name_;
    fbl::RefPtr<VmObjectDispatcher> vmo_;
    mx_rights_t vmo_rights_;
    uintptr_t code_start_;
    size_t size_;
};
