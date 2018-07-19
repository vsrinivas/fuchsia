// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <zircon/types.h>

#include <fbl/canary.h>
#include <object/dispatcher.h>

#include <lib/user_copy/user_ptr.h>
#include <sys/types.h>
#include <vm/vm_object.h>

class VmObjectDispatcher final : public SoloDispatcher<VmObjectDispatcher>,
                                 public VmObjectChildObserver {
public:
    static zx_status_t Create(fbl::RefPtr<VmObject> vmo,
                              fbl::RefPtr<Dispatcher>* dispatcher,
                              zx_rights_t* rights);
    ~VmObjectDispatcher() final;

    // VmObjectChildObserver implementation.
    void OnZeroChild() final;
    void OnOneChild() final;

    // SoloDispatcher implementation.
    zx_obj_type_t get_type() const final { return ZX_OBJ_TYPE_VMO; }
    bool has_state_tracker() const final { return true; }
    void get_name(char out_name[ZX_MAX_NAME_LEN]) const final;
    zx_status_t set_name(const char* name, size_t len) final;
    CookieJar* get_cookie_jar() final { return &cookie_jar_; }

    // VmObjectDispatcher own methods.
    zx_status_t Read(user_out_ptr<void> user_data, size_t length,
                     uint64_t offset);
    zx_status_t Write(user_in_ptr<const void> user_data, size_t length,
                      uint64_t offset);
    zx_status_t SetSize(uint64_t);
    zx_status_t GetSize(uint64_t* size);
    zx_status_t RangeOp(uint32_t op, uint64_t offset, uint64_t size, user_inout_ptr<void> buffer,
                        size_t buffer_size, zx_rights_t rights);
    zx_status_t Clone(
        uint32_t options, uint64_t offset, uint64_t size, bool copy_name,
        fbl::RefPtr<VmObject>* clone_vmo);

    zx_status_t SetMappingCachePolicy(uint32_t cache_policy);

    const fbl::RefPtr<VmObject>& vmo() const { return vmo_; }

private:
    explicit VmObjectDispatcher(fbl::RefPtr<VmObject> vmo);

    fbl::Canary<fbl::magic("VMOD")> canary_;

    // The 'const' here is load bearing; we give a raw pointer to
    // ourselves to |vmo_| so we have to ensure we don't reset vmo_
    // except during destruction.
    fbl::RefPtr<VmObject> const vmo_;

    // VMOs do not currently maintain any VMO-specific signal state,
    // but do allow user signals to be set. In addition, the CookieJar
    // shares the same lock.
    CookieJar cookie_jar_;
};
