// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <kernel/mutex.h>
#include <lib/user_copy/user_array.h>
#include <magenta/dispatcher.h>
#include <magenta/syscalls/resource.h>
#include <mxtl/intrusive_double_list.h>
#include <sys/types.h>

class ResourceRecord;

class ResourceDispatcher final : public Dispatcher,
    public mxtl::DoublyLinkedListable<mxtl::RefPtr<ResourceDispatcher>> {
public:
    static status_t Create(mxtl::RefPtr<ResourceDispatcher>* dispatcher,
                           mx_rights_t* rights, const char* name, uint16_t subtype);

    virtual ~ResourceDispatcher() final;
    mx_obj_type_t get_type() const final { return MX_OBJ_TYPE_RESOURCE; }
    status_t set_port_client(mxtl::unique_ptr<PortClient> client) final;

    // MakeRoot() validates the resource as the parent of a tree.
    // If successful children may be added to it, but it may never
    // be added as a child to another Resource.
    status_t MakeRoot();

    // AddChild() requires that the Resource being added have not yet
    // been validated and that the Resource the child is being added
    // to has been validated.
    status_t AddChild(const mxtl::RefPtr<ResourceDispatcher>& child);

    // Records may be added until MakeRoot() or AddChild() are called,
    // at which point the Resource is validated, and if valid, sealed
    // against further modifications.
    status_t AddRecords(user_ptr<mx_rrec_t> records, size_t count);
    status_t AddRecord(mx_rrec_t* record);

    // Create a Dispatcher for the indicated Record
    mx_status_t RecordCreateDispatcher(uint32_t index, mxtl::RefPtr<Dispatcher>* dispatcher,
                                       mx_rights_t* rights);

    // Ask the sub-resource of the specified Record to do an action
    mx_status_t RecordDoAction(uint32_t index, uint32_t action, uint32_t arg0, uint32_t arg1);

    mxtl::RefPtr<ResourceDispatcher> LookupChildById(mx_koid_t koid);

    mx_status_t GetRecords(user_ptr<mx_rrec_t> records, size_t max,
                           size_t* actual, size_t* available);
    mx_status_t GetChildren(user_ptr<mx_rrec_t> records, size_t max,
                            size_t* actual, size_t* available);

    uint16_t get_subtype() const { return subtype_; }

    static constexpr uint32_t kMaxRecords = 32;

private:
    ResourceDispatcher(const char* name, uint16_t subtype_);
    mx_status_t ValidateLocked();
    ResourceRecord* GetNthRecordLocked(uint32_t index);
    status_t AddRecord(mxtl::unique_ptr<ResourceRecord> rec);

    Mutex lock_;

    mxtl::DoublyLinkedList<mxtl::RefPtr<ResourceDispatcher>> children_;
    mxtl::DoublyLinkedList<mxtl::unique_ptr<ResourceRecord>> records_;

    uint32_t num_children_;
    uint16_t num_records_;
    uint16_t subtype_;

    mxtl::unique_ptr<PortClient> iopc_;

    bool valid_;
    char name_[MX_MAX_NAME_LEN];
};

