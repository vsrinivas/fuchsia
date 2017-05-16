// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <lib/rodso.h>
#include <kernel/vm/vm_object.h>

class VmMapping;

class VDso : public RoDso {
public:
    // This is called only once, at boot time.
    static const VDso* Create();

    static bool vmo_is_vdso(const mxtl::RefPtr<VmObject>& vmo) {
        return instance_ && vmo == instance_->vmo()->vmo();
    }

    static bool valid_code_mapping(uint64_t vmo_offset, size_t size) {
        return instance_->RoDso::valid_code_mapping(vmo_offset, size);
    }

    // Given VmAspace::vdso_code_mapping_, return the vDSO base address or 0.
    static uintptr_t base_address(const mxtl::RefPtr<VmMapping>& code_mapping);

private:
    VDso();

    static const VDso* instance_;
};
