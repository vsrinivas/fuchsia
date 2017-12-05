// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <kernel/mutex.h>
#include <zircon/syscalls/resource.h>
#include <zircon/thread_annotations.h>
#include <zircon/types.h>
#include <fbl/canary.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/name.h>
#include <object/dispatcher.h>
#include <object/handle.h>
#include <sys/types.h>

class ResourceRecord;

class ResourceDispatcher final : public SoloDispatcher,
    public fbl::DoublyLinkedListable<fbl::RefPtr<ResourceDispatcher>> {
public:
    static zx_status_t Create(fbl::RefPtr<ResourceDispatcher>* dispatcher,
                           zx_rights_t* rights, uint32_t kind,
                           uint64_t low, uint64_t hight);

    ~ResourceDispatcher() final;
    zx_obj_type_t get_type() const final { return ZX_OBJ_TYPE_RESOURCE; }
    bool has_state_tracker() const final { return true; }
    CookieJar* get_cookie_jar() final { return &cookie_jar_; }

    uint32_t get_kind() const { return kind_; }
    void get_range(uint64_t* low, uint64_t* high) { *low = low_, *high = high_; }

private:
    ResourceDispatcher(uint32_t kind, uint64_t low, uint64_t high);

    fbl::Canary<fbl::magic("RSRD")> canary_;

    const uint32_t kind_;
    const uint64_t low_;
    const uint64_t high_;

    CookieJar cookie_jar_;
};
