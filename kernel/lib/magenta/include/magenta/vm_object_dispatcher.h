// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <magenta/dispatcher.h>
#include <magenta/state_tracker.h>
#include <mxtl/canary.h>

#include <lib/user_copy/user_ptr.h>

#include <sys/types.h>

class VmObject;
class VmAspace;

class VmObjectDispatcher : public Dispatcher {
public:
    static status_t Create(mxtl::RefPtr<VmObject> vmo, mxtl::RefPtr<Dispatcher>* dispatcher,
                           mx_rights_t* rights);

    ~VmObjectDispatcher() final;
    mx_obj_type_t get_type() const final { return MX_OBJ_TYPE_VMEM; }
    StateTracker* get_state_tracker() final { return &state_tracker_; }
    void get_name(char out_name[MX_MAX_NAME_LEN]) const final;
    status_t set_name(const char* name, size_t len) final;
    CookieJar* get_cookie_jar() final { return &cookie_jar_; }

    mx_status_t Read(user_ptr<void> user_data, size_t length,
                     uint64_t offset, size_t* actual);
    mx_status_t Write(user_ptr<const void> user_data, size_t length,
                      uint64_t offset, size_t* actual);
    mx_status_t SetSize(uint64_t);
    mx_status_t GetSize(uint64_t* size);
    mx_status_t RangeOp(uint32_t op, uint64_t offset, uint64_t size, user_ptr<void> buffer, size_t buffer_size);
    mx_status_t Clone(uint32_t options, uint64_t offset, uint64_t size, mxtl::RefPtr<VmObject>* clone_vmo);
    mx_status_t SetMappingCachePolicy(uint32_t cache_policy);

    mxtl::RefPtr<VmObject> vmo() const { return vmo_; }

private:
    explicit VmObjectDispatcher(mxtl::RefPtr<VmObject> vmo);

    mxtl::Canary<mxtl::magic("VMOD")> canary_;
    mxtl::RefPtr<VmObject> vmo_;

    // VMOs do not currently maintain any VMO-specific signal state,
    // but do allow user signals to be set. In addition, the CookieJar
    // shares the same lock.
    StateTracker state_tracker_;
    CookieJar cookie_jar_;
};
