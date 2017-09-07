// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "vm/vm_object.h"

#include "vm_priv.h"

#include <assert.h>
#include <err.h>
#include <inttypes.h>
#include <kernel/vm.h>
#include <lib/console.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <fbl/ref_ptr.h>
#include <safeint/safe_math.h>
#include <stdlib.h>
#include <string.h>
#include <trace.h>
#include <vm/vm_address_region.h>

using fbl::AutoLock;

#define LOCAL_TRACE MAX(VM_GLOBAL_TRACE, 0)

fbl::Mutex VmObject::all_vmos_lock_ = {};
VmObject::GlobalList VmObject::all_vmos_ = {};

VmObject::VmObject(fbl::RefPtr<VmObject> parent)
    : lock_(parent ? parent->lock_ref() : local_lock_),
      parent_(fbl::move(parent)) {
    LTRACEF("%p\n", this);

    // Add ourself to the global VMO list, newer VMOs at the end.
    {
        AutoLock a(&all_vmos_lock_);
        all_vmos_.push_back(this);
    }
}

VmObject::~VmObject() {
    canary_.Assert();
    LTRACEF("%p\n", this);

    // remove ourself from our parent (if present)
    if (parent_) {
        LTRACEF("removing ourself from our parent %p\n", parent_.get());

        // conditionally grab our shared lock with the parent, but only if it's
        // not held. There are some destruction paths that may try to tear
        // down the object with the parent locks held.
        bool need_lock = !lock_.IsHeld();
        if (need_lock)
            lock_.Acquire();
        parent_->RemoveChildLocked(this);
        if (need_lock)
            lock_.Release();
    }

    DEBUG_ASSERT(mapping_list_.is_empty());
    DEBUG_ASSERT(children_list_.is_empty());

    // Remove ourself from the global VMO list.
    {
        AutoLock a(&all_vmos_lock_);
        DEBUG_ASSERT(global_list_state_.InContainer() == true);
        all_vmos_.erase(*this);
    }
}

void VmObject::get_name(char* out_name, size_t len) const {
    canary_.Assert();
    name_.get(len, out_name);
}

status_t VmObject::set_name(const char* name, size_t len) {
    canary_.Assert();
    return name_.set(name, len);
}

void VmObject::set_user_id(uint64_t user_id) {
    canary_.Assert();
    AutoLock a(&lock_);
    DEBUG_ASSERT(user_id_ == 0);
    user_id_ = user_id;
}

uint64_t VmObject::user_id() const {
    canary_.Assert();
    AutoLock a(&lock_);
    return user_id_;
}

uint64_t VmObject::parent_user_id() const {
    canary_.Assert();
    // Don't hold both our lock and our parent's lock at the same time, because
    // it's probably the same lock.
    fbl::RefPtr<VmObject> parent;
    {
        AutoLock a(&lock_);
        if (parent_ == nullptr) {
            return 0u;
        }
        parent = parent_;
    }
    return parent->user_id();
}

bool VmObject::is_cow_clone() const {
    canary_.Assert();
    AutoLock a(&lock_);
    return parent_ != nullptr;
}

void VmObject::AddMappingLocked(VmMapping* r) {
    canary_.Assert();
    DEBUG_ASSERT(lock_.IsHeld());
    mapping_list_.push_front(r);
    mapping_list_len_++;
}

void VmObject::RemoveMappingLocked(VmMapping* r) {
    canary_.Assert();
    DEBUG_ASSERT(lock_.IsHeld());
    mapping_list_.erase(*r);
    DEBUG_ASSERT(mapping_list_len_ > 0);
    mapping_list_len_--;
}

uint32_t VmObject::num_mappings() const {
    canary_.Assert();
    AutoLock a(&lock_);
    return mapping_list_len_;
}

bool VmObject::IsMappedByUser() const {
    canary_.Assert();
    AutoLock a(&lock_);
    for (const auto& m : mapping_list_) {
        if (m.aspace()->is_user()) {
            return true;
        }
    }
    return false;
}

uint32_t VmObject::share_count() const {
    canary_.Assert();

    AutoLock a(&lock_);
    if (mapping_list_len_ < 2) {
        return 1;
    }

    // Find the number of unique VmAspaces that we're mapped into.
    // Use this buffer to hold VmAspace pointers.
    static constexpr int kAspaceBuckets = 64;
    uintptr_t aspaces[kAspaceBuckets];
    unsigned int num_mappings = 0; // Number of mappings we've visited
    unsigned int num_aspaces = 0;  // Unique aspaces we've seen
    for (const auto& m : mapping_list_) {
        uintptr_t as = reinterpret_cast<uintptr_t>(m.aspace().get());
        // Simple O(n^2) should be fine.
        for (unsigned int i = 0; i < num_aspaces; i++) {
            if (aspaces[i] == as) {
                goto found;
            }
        }
        if (num_aspaces < kAspaceBuckets) {
            aspaces[num_aspaces++] = as;
        } else {
            // Maxed out the buffer. Estimate the remaining number of aspaces.
            num_aspaces +=
                // The number of mappings we haven't visited yet
                (mapping_list_len_ - num_mappings)
                // Scaled down by the ratio of unique aspaces we've seen so far.
                * num_aspaces / num_mappings;
            break;
        }
    found:
        num_mappings++;
    }
    DEBUG_ASSERT_MSG(num_aspaces <= mapping_list_len_,
                     "num_aspaces %u should be <= mapping_list_len_ %" PRIu32,
                     num_aspaces, mapping_list_len_);

    // TODO: Cache this value as long as the set of mappings doesn't change.
    // Or calculate it when adding/removing a new mapping under an aspace
    // not in the list.
    return num_aspaces;
}

void VmObject::AddChildLocked(VmObject* o) {
    canary_.Assert();
    DEBUG_ASSERT(lock_.IsHeld());
    children_list_.push_front(o);
    children_list_len_++;
}

void VmObject::RemoveChildLocked(VmObject* o) {
    canary_.Assert();
    DEBUG_ASSERT(lock_.IsHeld());
    children_list_.erase(*o);
    DEBUG_ASSERT(children_list_len_ > 0);
    children_list_len_--;
}

uint32_t VmObject::num_children() const {
    canary_.Assert();
    AutoLock a(&lock_);
    return children_list_len_;
}

void VmObject::RangeChangeUpdateLocked(uint64_t offset, uint64_t len) {
    canary_.Assert();
    DEBUG_ASSERT(lock_.IsHeld());

    // offsets for vmos needn't be aligned, but vmars use aligned offsets
    const uint64_t aligned_offset = ROUNDDOWN(offset, PAGE_SIZE);
    const uint64_t aligned_len = ROUNDUP(offset + len, PAGE_SIZE) - aligned_offset;

    // other mappings may have covered this offset into the vmo, so unmap those ranges
    for (auto& m : mapping_list_) {
        m.UnmapVmoRangeLocked(aligned_offset, aligned_len);
    }

    // inform all our children this as well, so they can inform their mappings
    for (auto& child : children_list_) {
        child.RangeChangeUpdateFromParentLocked(offset, len);
    }
}

static int cmd_vm_object(int argc, const cmd_args* argv, uint32_t flags) {
    if (argc < 2) {
    notenoughargs:
        printf("not enough arguments\n");
    usage:
        printf("usage:\n");
        printf("%s dump <address>\n", argv[0].str);
        printf("%s dump_pages <address>\n", argv[0].str);
        return MX_ERR_INTERNAL;
    }

    if (!strcmp(argv[1].str, "dump")) {
        if (argc < 2)
            goto notenoughargs;

        VmObject* o = reinterpret_cast<VmObject*>(argv[2].u);

        o->Dump(0, false);
    } else if (!strcmp(argv[1].str, "dump_pages")) {
        if (argc < 2)
            goto notenoughargs;

        VmObject* o = reinterpret_cast<VmObject*>(argv[2].u);

        o->Dump(0, true);
    } else {
        printf("unknown command\n");
        goto usage;
    }

    return MX_OK;
}

STATIC_COMMAND_START
#if LK_DEBUGLEVEL > 0
STATIC_COMMAND("vm_object", "vm object debug commands", &cmd_vm_object)
#endif
STATIC_COMMAND_END(vm_object);
