// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/resource_dispatcher.h>

#include <kernel/auto_lock.h>
#include <kernel/vm/vm_object.h>
#include <kernel/vm/vm_object_physical.h>
#include <magenta/handle.h>
#include <magenta/handle_owner.h>
#include <magenta/channel_dispatcher.h>
#include <magenta/interrupt_event_dispatcher.h>
#include <magenta/rights.h>
#include <magenta/vm_object_dispatcher.h>
#include <mxalloc/new.h>
#include <string.h>

class ResourceRecord : public mxtl::DoublyLinkedListable<mxtl::unique_ptr<ResourceRecord>> {
private:
    static status_t Create(mxtl::unique_ptr<ResourceRecord>& out) {
        AllocChecker ac;
        mxtl::unique_ptr<ResourceRecord> record(new (&ac) ResourceRecord());
        if (!ac.check())
            return MX_ERR_NO_MEMORY;
        out = mxtl::move(record);
        return MX_OK;
    }
    explicit ResourceRecord() {};

    mx_rrec_t content_;
    mx_status_t (*create_dispatcher_)(const mx_rrec_t* rec, uint32_t options,
                                      mxtl::RefPtr<Dispatcher>*, mx_rights_t*);
    mx_status_t (*do_action_)(const mx_rrec_t* rec, uint32_t action, uint32_t arg0, uint32_t arg1);

    friend class ResourceDispatcher;
};

mx_status_t ResourceDispatcher::Create(mxtl::RefPtr<ResourceDispatcher>* dispatcher,
                                    mx_rights_t* rights, const char* name, uint16_t subtype) {
    AllocChecker ac;
    ResourceDispatcher* disp = new (&ac) ResourceDispatcher(name, subtype);
    if (!ac.check())
        return MX_ERR_NO_MEMORY;

    *rights = MX_DEFAULT_RESOURCE_RIGHTS;
    *dispatcher = mxtl::AdoptRef<ResourceDispatcher>(disp);
    return MX_OK;
}

ResourceDispatcher::ResourceDispatcher(const char* name, uint16_t subtype) :
    parent_(nullptr), num_children_(0), num_records_(0),
    subtype_(subtype), state_(State::Created),
    state_tracker_(MX_RESOURCE_WRITABLE),
    name_(name, strlen(name)) {
}

//TODO: deferred deletion of children
//
// Right now the resource tree lives for the lifespan of the system
// and deletion of resources may only occur from leaf to root, so
// the default recursive teardown is not a problem.  It would be
// nice to be able to prune subtrees and as they could be of
// an arbitrary size, we'll need to be careful.
ResourceDispatcher::~ResourceDispatcher() {
}

mx_status_t ResourceDispatcher::MakeRoot() {
    canary_.Assert();

    AutoLock lock(&lock_);

    if (state_ != State::Created)
        return MX_ERR_BAD_STATE;

    state_ = State::Alive;
    return MX_OK;
}

mx_status_t ResourceDispatcher::SetParent(ResourceDispatcher* parent) {
    canary_.Assert();

    AutoLock lock(&lock_);

    if (state_ != State::Alive)
        return MX_ERR_BAD_STATE;

    if (parent_ != nullptr)
        return MX_ERR_ALREADY_BOUND;

    parent_ = parent;
    return MX_OK;
}

mx_status_t ResourceDispatcher::GetParent(mxtl::RefPtr<ResourceDispatcher>* dispatcher) {
    canary_.Assert();

    AutoLock lock(&lock_);

    if (parent_ == nullptr)
        return MX_ERR_BAD_STATE;

    *dispatcher = mxtl::RefPtr<ResourceDispatcher>(parent_);
    return MX_OK;
}

mx_status_t ResourceDispatcher::AddChild(const mxtl::RefPtr<ResourceDispatcher>& child) {
    canary_.Assert();

    AutoLock lock(&lock_);

    if (state_ != State::Alive)
        return MX_ERR_BAD_STATE;

    // Currently this should be impossible, as it is only ever
    // called from sys_resource_create on a freshly created child.
    // But let's ensure that we can't ever create loops.
    if (child.get() == this)
        return MX_ERR_BAD_STATE;

    mx_status_t status = child->SetParent(this);
    if (status != MX_OK)
        return status;

    children_.push_back(mxtl::move(child));
    ++num_children_;

    state_tracker_.StrobeState(MX_RESOURCE_CHILD_ADDED);

    return MX_OK;
}

mx_status_t ResourceDispatcher::DestroySelf(ResourceDispatcher* parent) {
    AutoLock lock(&lock_);

    if (state_ != State::Alive)
        return MX_ERR_BAD_STATE;

    if (num_children_ > 0)
        return MX_ERR_BAD_STATE;

    if (parent != parent_)
        return MX_ERR_INVALID_ARGS;

    state_ = State::Dead;
    parent_ = nullptr;

    state_tracker_.UpdateState(0, MX_RESOURCE_DESTROYED);

    return MX_OK;
}

mx_status_t ResourceDispatcher::DestroyChild(mxtl::RefPtr<ResourceDispatcher> child) {
    canary_.Assert();

    AutoLock lock(&lock_);

    mx_status_t status = child->DestroySelf(this);

    if (status == MX_OK) {
        children_.erase(*child);
        --num_children_;
    }

    return status;
}

mxtl::RefPtr<ResourceDispatcher> ResourceDispatcher::LookupChildById(mx_koid_t koid) {
    canary_.Assert();

    AutoLock lock(&lock_);

    auto iter = children_.find_if([koid](const ResourceDispatcher& r) { return r.get_koid() == koid; });
    if (!iter.IsValid())
        return nullptr;
    return iter.CopyPointer();
}

static mx_status_t mmio_create_dispatcher(const mx_rrec_t* rec, uint32_t options,
                                          mxtl::RefPtr<Dispatcher>* dispatcher,
                                          mx_rights_t* rights) {
    mxtl::RefPtr<VmObject> vmo = VmObjectPhysical::Create(
        static_cast<mx_paddr_t>(rec->mmio.phys_base), rec->mmio.phys_size);
    if (!vmo)
        return MX_ERR_NO_MEMORY;

    return VmObjectDispatcher::Create(mxtl::move(vmo), dispatcher, rights);
}

static mx_status_t irq_create_dispatcher(const mx_rrec_t* rec, uint32_t options,
                                         mxtl::RefPtr<Dispatcher>* dispatcher,
                                         mx_rights_t* rights) {
    return InterruptEventDispatcher::Create(rec->irq.irq_base, options, dispatcher, rights);
}

#if ARCH_X86
#include <arch/x86/descriptor.h>
#include <arch/x86/ioport.h>

static mx_status_t ioport_do_action(const mx_rrec_t* rec, uint32_t action,
                                    uint32_t arg0, uint32_t arg1) {
    switch (action) {
    case MX_RACT_ENABLE:
        return x86_set_io_bitmap(rec->ioport.port_base, rec->ioport.port_count, 1);
    default:
        return MX_ERR_NOT_SUPPORTED;
    }
}
#endif

static mx_status_t default_create_dispatcher(const mx_rrec_t*, uint32_t,
                                             mxtl::RefPtr<Dispatcher>*, mx_rights_t*) {
    return MX_ERR_NOT_SUPPORTED;
}

static mx_status_t default_do_action(const mx_rrec_t*, uint32_t, uint32_t, uint32_t) {
    return MX_ERR_NOT_SUPPORTED;
}

mx_status_t ResourceDispatcher::AddRecordLocked(mxtl::unique_ptr<ResourceRecord> rrec) {
    if (state_ != State::Created)
        return MX_ERR_BAD_STATE;

    if (num_records_ >= kMaxRecords)
        return MX_ERR_BAD_STATE;

    mx_rrec_t* rec = &rrec->content_;

    //TODO: validate mmio/irq values, ensure they're non-overlapping
    //TODO: special rights needed to create MMIO/IRQ records?
    switch (rec->type) {
    case MX_RREC_IRQ:
        rrec->create_dispatcher_ = irq_create_dispatcher;
        rrec->do_action_ = default_do_action;
        break;
    case MX_RREC_MMIO:
        rrec->create_dispatcher_ = mmio_create_dispatcher;
        rrec->do_action_ = default_do_action;
        break;
#if ARCH_X86
    case MX_RREC_IOPORT:
        rrec->create_dispatcher_ = default_create_dispatcher;
        rrec->do_action_ = ioport_do_action;
        break;
#endif
    case MX_RREC_DATA:
        rrec->create_dispatcher_ = default_create_dispatcher;
        rrec->do_action_ = default_do_action;
        break;
    default:
        return MX_ERR_NOT_SUPPORTED;
    }

    records_.push_back(mxtl::move(rrec));
    ++num_records_;

    return MX_OK;
}

mx_status_t ResourceDispatcher::AddRecordLocked(mx_rrec_t* tmpl) {
    status_t status;
    mxtl::unique_ptr<ResourceRecord> rec;
    if ((status = ResourceRecord::Create(rec)) != MX_OK)
        return status;
    memcpy(&rec->content_, tmpl, sizeof(mx_rrec_t));
    return AddRecordLocked(mxtl::move(rec));
}

mx_status_t ResourceDispatcher::AddRecords(user_ptr<const mx_rrec_t> records, size_t count) {
    canary_.Assert();

    AutoLock lock(&lock_);

    if (state_ != State::Created)
        return MX_ERR_BAD_STATE;

    for (uint32_t n = 1; n < count; n++) {
        status_t status;
        mxtl::unique_ptr<ResourceRecord> rec;
        if ((status = ResourceRecord::Create(rec)) != MX_OK)
            return status;
        if (records.copy_array_from_user(&rec->content_, 1, n) != MX_OK) {
            return MX_ERR_INVALID_ARGS;
        }
        if ((status = AddRecordLocked(mxtl::move(rec))) != MX_OK) {
            return status;
        }
    }

    state_ = State::Alive;

    return MX_OK;
}

void ResourceDispatcher::GetSelf(mx_rrec_self_t* self) {
    canary_.Assert();

    self->type = MX_RREC_SELF;
    self->subtype = subtype_;
    self->options = 0;
    self->koid = get_koid();
    self->reserved[0] = 0;
    self->reserved[1] = 0;
    name_.get(sizeof(self->name), self->name);

    AutoLock lock(&lock_);
    self->child_count = num_children_;
    self->record_count = num_records_ + 1;
}

ResourceRecord* ResourceDispatcher::GetNthRecordLocked(uint32_t index) {
    canary_.Assert();

    if (state_ != State::Alive)
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
    canary_.Assert();

    size_t n = 0;
    *actual = 0;
    mx_status_t status = MX_OK;

    mx_rrec_t rec = {};
    rec.self.type = MX_RREC_SELF;
    rec.self.subtype = subtype_;
    rec.self.koid = get_koid();
    name_.get(sizeof(rec.self.name), rec.self.name);

    {
        AutoLock lock(&lock_);
        rec.self.child_count = num_children_;
        rec.self.record_count = num_records_ + 1;

        // indicate how many we *could* return
        *available = num_records_ + 1;

        // copy our self entry first
        if (n < max) {
            if (records.copy_array_to_user(&rec, 1, n) != MX_OK) {
                status = MX_ERR_INVALID_ARGS;
                goto done;
            }
            n++;
        }
        for (auto& record: records_) {
            if (n == max)
                break;
            if (records.copy_array_to_user(&record.content_, 1, n) != MX_OK) {
                status = MX_ERR_INVALID_ARGS;
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
    canary_.Assert();

    mx_rrec_t rec = {};
    size_t n = 0;
    mx_status_t status = MX_OK;
    {
        AutoLock lock(&lock_);

        // indicate how many we *could* return;
        *available = num_children_;

        for (auto& child: children_) {
            if (n == max)
                break;
            // This acquires the child lock. Doing so is safe because
            // the parent-child relationship is strictly hierarchical.
            child.GetSelf(&rec.self);

            if (records.copy_array_to_user(&rec, 1, n) != MX_OK) {
                status = MX_ERR_INVALID_ARGS;
                break;
            }
            ++n;
        }
    }
    *actual = n;
    return status;
}

mx_status_t ResourceDispatcher::RecordCreateDispatcher(uint32_t index, uint32_t options,
                                                       mxtl::RefPtr<Dispatcher>* dispatcher,
                                                       mx_rights_t* rights) {
    canary_.Assert();

    AutoLock lock(&lock_);

    if (index == 0) {
        return MX_ERR_NOT_SUPPORTED;
    }
    ResourceRecord* rec = GetNthRecordLocked(index - 1);
    if (rec == nullptr) {
        return MX_ERR_NOT_SUPPORTED;
    } else {
        return rec->create_dispatcher_(&rec->content_, options, dispatcher, rights);
    }
}

mx_status_t ResourceDispatcher::RecordDoAction(uint32_t index, uint32_t action,
                                               uint32_t arg0, uint32_t arg1) {
    canary_.Assert();

    AutoLock lock(&lock_);

    ResourceRecord* rec = GetNthRecordLocked(index);
    if (rec == nullptr) {
        return MX_ERR_NOT_SUPPORTED;
    } else {
        return rec->do_action_(&rec->content_, action, arg0, arg1);
    }
}

mx_status_t ResourceDispatcher::Connect(HandleOwner* handle) {
    canary_.Assert();

    AutoLock lock(&lock_);

    if (inbound_)
        return MX_ERR_SHOULD_WAIT;

    inbound_ = mxtl::move(*handle);

    state_tracker_.UpdateState(MX_RESOURCE_WRITABLE, MX_RESOURCE_READABLE);

    return MX_OK;
}

mx_status_t ResourceDispatcher::Accept(HandleOwner* handle) {
    canary_.Assert();

    AutoLock lock(&lock_);

    if (!inbound_)
        return MX_ERR_SHOULD_WAIT;

    *handle = mxtl::move(inbound_);

    state_tracker_.UpdateState(MX_RESOURCE_READABLE, MX_RESOURCE_WRITABLE);

    return MX_OK;
}
