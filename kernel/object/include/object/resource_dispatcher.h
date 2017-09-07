// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <kernel/mutex.h>
#include <magenta/syscalls/resource.h>
#include <magenta/thread_annotations.h>
#include <magenta/types.h>
#include <fbl/canary.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/name.h>
#include <object/dispatcher.h>
#include <object/handle_owner.h>
#include <object/state_tracker.h>
#include <sys/types.h>

class ResourceRecord;

class ResourceDispatcher final : public Dispatcher,
    public fbl::DoublyLinkedListable<fbl::RefPtr<ResourceDispatcher>> {
public:
    static mx_status_t Create(fbl::RefPtr<ResourceDispatcher>* dispatcher,
                           mx_rights_t* rights, uint32_t kind,
                           uint64_t low, uint64_t hight);

    ~ResourceDispatcher() final;
    mx_obj_type_t get_type() const final { return MX_OBJ_TYPE_RESOURCE; }
    StateTracker* get_state_tracker()  final { return &state_tracker_; }
    CookieJar* get_cookie_jar() final { return &cookie_jar_; }

    uint32_t get_kind() const { return kind_; }
    void get_range(uint64_t* low, uint64_t* high) { *low = low_, *high = high_; }

private:
    ResourceDispatcher(uint32_t kind, uint64_t low, uint64_t high);

    fbl::Canary<fbl::magic("RSRD")> canary_;

    const uint32_t kind_;
    const uint64_t low_;
    const uint64_t high_;

    StateTracker state_tracker_;
    CookieJar cookie_jar_;
};
