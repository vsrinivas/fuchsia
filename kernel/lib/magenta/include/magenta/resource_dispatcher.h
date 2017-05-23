// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <kernel/mutex.h>
#include <lib/user_copy/user_array.h>
#include <magenta/dispatcher.h>
#include <magenta/handle_owner.h>
#include <magenta/state_tracker.h>
#include <magenta/syscalls/resource.h>
#include <magenta/thread_annotations.h>
#include <mxtl/canary.h>
#include <mxtl/intrusive_double_list.h>
#include <mxtl/name.h>
#include <sys/types.h>

class ResourceRecord;

class ResourceDispatcher final : public Dispatcher,
    public mxtl::DoublyLinkedListable<mxtl::RefPtr<ResourceDispatcher>> {
public:
    static mx_status_t Create(mxtl::RefPtr<ResourceDispatcher>* dispatcher,
                           mx_rights_t* rights, const char* name, uint16_t subtype);

    virtual ~ResourceDispatcher() final;
    mx_obj_type_t get_type() const final { return MX_OBJ_TYPE_RESOURCE; }
    StateTracker* get_state_tracker()  final { return &state_tracker_; }
    CookieJar* get_cookie_jar() final { return &cookie_jar_; }
    mx_status_t set_port_client(mxtl::unique_ptr<PortClient> client) final;

    // MakeRoot() validates the resource as the parent of a tree.
    // If successful children may be added to it, but it may never
    // be added as a child to another Resource.
    mx_status_t MakeRoot();

    // DestroyChild() removes a resource from its parent and marks the
    // resource for destruction.  Once so-marked it will be released
    // when the last handle to it is closed, additional children may
    // no longer be added to it, and record actions are no longer
    // available.
    //
    // For now, the child must have no children to be destroyed.
    mx_status_t DestroyChild(mxtl::RefPtr<ResourceDispatcher> child);

    // AddChild() requires that the Resource being added be alive, and
    // not already have a parent.
    mx_status_t AddChild(const mxtl::RefPtr<ResourceDispatcher>& child);

    // Records may be added until MakeRoot() or AddChild() are called,
    // at which point the Resource is validated, and if valid, sealed
    // against further modifications.
    mx_status_t AddRecords(user_ptr<const mx_rrec_t> records, size_t count);

    // Obtain a reference to this Resource's parent, if it has one
    mx_status_t GetParent(mxtl::RefPtr<ResourceDispatcher>* dispatcher);

    // Create a Dispatcher for the indicated Record
    mx_status_t RecordCreateDispatcher(uint32_t index, uint32_t options,
                                       mxtl::RefPtr<Dispatcher>* dispatcher,
                                       mx_rights_t* rights);

    // Ask the sub-resource of the specified Record to do an action
    mx_status_t RecordDoAction(uint32_t index, uint32_t action, uint32_t arg0, uint32_t arg1);

    mxtl::RefPtr<ResourceDispatcher> LookupChildById(mx_koid_t koid);

    mx_status_t GetRecords(user_ptr<mx_rrec_t> records, size_t max,
                           size_t* actual, size_t* available);
    mx_status_t GetChildren(user_ptr<mx_rrec_t> records, size_t max,
                            size_t* actual, size_t* available);

    mx_status_t Connect(HandleOwner* channel);
    mx_status_t Accept(HandleOwner* channel);

    uint16_t get_subtype() const { return subtype_; }

    static constexpr uint32_t kMaxRecords = 32;

private:
    enum class State {
        Created,
        Alive,
        Dead,
    };
    ResourceDispatcher(const char* name, uint16_t subtype_);
    ResourceRecord* GetNthRecordLocked(uint32_t index) TA_REQ(lock_);
    mx_status_t AddRecordLocked(mxtl::unique_ptr<ResourceRecord> rec) TA_REQ(lock_);
    mx_status_t AddRecordLocked(mx_rrec_t* record) TA_REQ(lock_);
    mx_status_t SetParent(ResourceDispatcher* parent);
    void GetSelf(mx_rrec_self_t* out);
    mx_status_t DestroySelf(ResourceDispatcher* parent);

    mxtl::Canary<mxtl::magic("RSRD")> canary_;
    Mutex lock_;

    mxtl::DoublyLinkedList<mxtl::RefPtr<ResourceDispatcher>> children_ TA_GUARDED(lock_);
    mxtl::DoublyLinkedList<mxtl::unique_ptr<ResourceRecord>> records_ TA_GUARDED(lock_);
    ResourceDispatcher* parent_ TA_GUARDED(lock_);

    uint32_t num_children_ TA_GUARDED(lock_);
    uint16_t num_records_ TA_GUARDED(lock_);
    const uint16_t subtype_;
    State state_ TA_GUARDED(lock_);

    HandleOwner inbound_ TA_GUARDED(lock_);
    StateTracker state_tracker_;
    CookieJar cookie_jar_;
    mxtl::unique_ptr<PortClient> iopc_ TA_GUARDED(lock_);

    // The user-friendly resource name. For debug purposes only. That
    // is, there is no mechanism to mint a handle to a resource via this name.
    mxtl::Name<MX_MAX_NAME_LEN> name_;
};
