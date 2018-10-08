// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include "vm/vm_object.h"

// An RAII wrapper around a |VmObject| that is pinned.
class PinnedVmObject {
public:
    static zx_status_t Create(fbl::RefPtr<VmObject> vmo, size_t offset, size_t size,
                              PinnedVmObject* out_pinned_vmo);

    PinnedVmObject();
    PinnedVmObject(PinnedVmObject&&);
    PinnedVmObject& operator=(PinnedVmObject&&);
    ~PinnedVmObject();

    const fbl::RefPtr<VmObject>& vmo() const { return vmo_; }
    size_t offset() const { return offset_; }
    size_t size() const { return size_; }

private:
    fbl::RefPtr<VmObject> vmo_;
    size_t offset_;
    size_t size_;

    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(PinnedVmObject);
};
