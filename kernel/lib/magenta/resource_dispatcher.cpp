// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <kernel/auto_lock.h>
#include <magenta/handle.h>
#include <magenta/resource_dispatcher.h>
#include <new.h>
#include <string.h>

class ResourceRecord : public mxtl::DoublyLinkedListable<mxtl::unique_ptr<ResourceRecord>> {
private:
    static status_t Create(mxtl::unique_ptr<ResourceRecord>& out) {
        AllocChecker ac;
        mxtl::unique_ptr<ResourceRecord> record(new (&ac) ResourceRecord());
        if (!ac.check())
            return ERR_NO_MEMORY;
        out = mxtl::move(record);
        return NO_ERROR;
    }
    explicit ResourceRecord() {};

    mx_rrec_t content_;
    mx_handle_t (*create_dispatcher_)(const mx_rrec_t* rec, mxtl::RefPtr<Dispatcher>*, mx_rights_t*);
    mx_status_t (*do_action_)(const mx_rrec_t* rec, uint32_t action, uint32_t arg0, uint32_t arg1);

    friend class ResourceDispatcher;
};


constexpr mx_rights_t kDefaultResourceRights =
    MX_RIGHT_READ | MX_RIGHT_WRITE | MX_RIGHT_DUPLICATE | MX_RIGHT_TRANSFER | MX_RIGHT_ENUMERATE;

status_t ResourceDispatcher::Create(mxtl::RefPtr<ResourceDispatcher>* dispatcher,
                                    mx_rights_t* rights, const char* name, uint16_t subtype) {
    AllocChecker ac;
    ResourceDispatcher* disp = new (&ac) ResourceDispatcher(name, subtype);
    if (!ac.check())
        return ERR_NO_MEMORY;

    *rights = kDefaultResourceRights;
    *dispatcher = mxtl::AdoptRef<ResourceDispatcher>(disp);
    return NO_ERROR;
}

ResourceDispatcher::ResourceDispatcher(const char* name, uint16_t subtype) :
    num_children_(0), num_records_(0), subtype_(subtype), valid_(false) {
    size_t len = strlen(name);
    if (len >= MX_MAX_NAME_LEN)
        len = MX_MAX_NAME_LEN - 1;
    memcpy(name_, name, len);
    memset(name_ + len, 0, MX_MAX_NAME_LEN - len);
}

ResourceDispatcher::~ResourceDispatcher() {
}


status_t ResourceDispatcher::MakeRoot() {
    AutoLock lock(lock_);

    if (valid_)
        return ERR_BAD_STATE;

    return ValidateLocked();
}

status_t ResourceDispatcher::AddChild(const mxtl::RefPtr<ResourceDispatcher>& child) {
    AutoLock lock(lock_);

    if (!valid_)
        return ERR_BAD_STATE;

    status_t status = child->ValidateLocked();
    if (status != NO_ERROR)
        return status;

    children_.push_back(mxtl::move(child));
    ++num_children_;

    //TODO: signal observers

    return NO_ERROR;
}

mxtl::RefPtr<ResourceDispatcher> ResourceDispatcher::LookupChildById(mx_koid_t koid) {
    AutoLock lock(&lock_);

    auto iter = children_.find_if([koid](const ResourceDispatcher& r) { return r.get_koid() == koid; });
    if (!iter.IsValid())
        return nullptr;
    return iter.CopyPointer();
}

static mx_handle_t default_create_dispatcher(const mx_rrec_t*, mxtl::RefPtr<Dispatcher>*, mx_rights_t*) {
    return ERR_NOT_SUPPORTED;
}

static mx_status_t default_do_action(const mx_rrec_t*, uint32_t, uint32_t, uint32_t) {
    return ERR_NOT_SUPPORTED;
}

status_t ResourceDispatcher::AddRecord(mxtl::unique_ptr<ResourceRecord> rec) {
    AutoLock lock(lock_);

    if (valid_)
        return ERR_BAD_STATE;

    //TODO: validate record contents, assign appropriate hooks
    rec->create_dispatcher_ = default_create_dispatcher;
    rec->do_action_ = default_do_action;

    records_.push_back(mxtl::move(rec));
    ++num_records_;

    return NO_ERROR;
}

status_t ResourceDispatcher::AddRecord(mx_rrec_t* tmpl) {
    status_t status;
    mxtl::unique_ptr<ResourceRecord> rec;
    if ((status = ResourceRecord::Create(rec)) != NO_ERROR)
        return status;
    memcpy(&rec->content_, tmpl, sizeof(mx_rrec_t));
    return AddRecord(mxtl::move(rec));
}

status_t ResourceDispatcher::AddRecords(user_ptr<mx_rrec_t> records, size_t count) {
    if (count > 32)
        return ERR_BAD_STATE;

    for (uint32_t n = 1; n < count; n++) {
        status_t status;
        mxtl::unique_ptr<ResourceRecord> rec;
        if ((status = ResourceRecord::Create(rec)) != NO_ERROR)
            return status;
        if (records.copy_array_from_user(&rec->content_, 1, n) != NO_ERROR) {
            return ERR_INVALID_ARGS;
        }
        if ((status = AddRecord(mxtl::move(rec))) != NO_ERROR) {
            return status;
        }
    }

    return NO_ERROR;
}

status_t ResourceDispatcher::ValidateLocked(void) {
    if (valid_)
        return ERR_BAD_STATE;
    if (num_children_ == 0)
        return ERR_BAD_STATE;

    valid_ = true;
    return NO_ERROR;
}

ResourceRecord* ResourceDispatcher::GetNthRecordLocked(uint32_t index) {
    if (!valid_)
        return nullptr;
    for (auto& rec: records_) {
        if (index == 0) {
            return &rec;
        }
        --index;
    }
    return nullptr;
}

mx_status_t ResourceDispatcher::GetRecords(user_ptr<mx_rrec_t> records, size_t max,
                                           size_t* actual, size_t* available) {
    size_t n = 0;
    *actual = 0;
    mx_status_t status = NO_ERROR;

    mx_rrec_t rec = {};
    rec.self.type = MX_RREC_SELF;
    rec.self.subtype = subtype_;
    memcpy(rec.self.name, name_, MX_MAX_NAME_LEN);

    {
        AutoLock lock(lock_);

        // indicate how many we *could* return
        *available = num_records_ + 1;

        // copy our self entry first
        if (n < max) {
            if (records.copy_array_to_user(&rec, 1, n) != NO_ERROR) {
                status = ERR_INVALID_ARGS;
                goto done;
            }
            n++;
        }
        for (auto& record: records_) {
            if (n == max)
                break;
            if (records.copy_array_to_user(&record.content_, 1, n) != NO_ERROR) {
                status = ERR_INVALID_ARGS;
                break;
            }
            n++;
        }
    }

done:
    *actual = n;
    return status;
}

mx_status_t ResourceDispatcher::GetChildren(user_ptr<mx_rrec_t> records, size_t max,
                                            size_t* actual, size_t* available) {
    mx_rrec_t rec = {};
    size_t n = 0;
    mx_status_t status = NO_ERROR;
    rec.self.type = MX_RREC_SELF;
    {
        AutoLock lock(lock_);

        // indicate how many we *could* return;
        *available = num_children_;

        for (auto& child: children_) {
            if (n == max)
                break;
            memcpy(rec.self.name, child.name_, MX_MAX_NAME_LEN);
            rec.self.subtype = child.subtype_;
            rec.self.child_count = child.num_children_;
            rec.self.record_count = child.num_records_;
            if (records.copy_array_to_user(&rec, 1, n) != NO_ERROR) {
                status = ERR_INVALID_ARGS;
                break;
            }
            ++n;
        }
    }
    *actual = n;
    return status;
}

mx_status_t ResourceDispatcher::RecordCreateDispatcher(uint32_t index,
                                                       mxtl::RefPtr<Dispatcher>* dispatcher,
                                                       mx_rights_t* rights) {
    AutoLock lock(lock_);

    ResourceRecord* rec = GetNthRecordLocked(index);
    if (rec == nullptr) {
        return ERR_NOT_SUPPORTED;
    } else {
        return rec->create_dispatcher_(&rec->content_, dispatcher, rights);
    }
}

mx_status_t ResourceDispatcher::RecordDoAction(uint32_t index, uint32_t action,
                                               uint32_t arg0, uint32_t arg1) {
    AutoLock lock(lock_);

    ResourceRecord* rec = GetNthRecordLocked(index);
    if (rec == nullptr) {
        return ERR_NOT_SUPPORTED;
    } else {
        return rec->do_action_(&rec->content_, action, arg0, arg1);
    }
}


