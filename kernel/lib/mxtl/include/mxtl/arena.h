// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stddef.h>

#include <kernel/vm/vm_address_region.h>

#include <mxcpp/new.h>
#include <mxtl/intrusive_single_list.h>
#include <mxtl/ref_ptr.h>
#include <mxtl/type_support.h>

namespace mxtl {

// Arena is a fast memory allocator for objects of a single size.
// Both Alloc() and Free() are always O(1) and memory always comes
// from a single contigous chunck of page-aligned memory.
//
// The control structures and data are not interleaved so it is
// more resilient to memory bugs than traditional pool allocators.
//
// The overhead per object is two pointers (16 bytes in 64-bits)

class Arena {
public:
    Arena() = default;
    ~Arena();

    status_t Init(const char* name, size_t ob_size, size_t max_count);
    void* Alloc();
    void Free(void* addr);
    bool in_range(void* addr) const {
        return data_.InRange(static_cast<char*>(addr));
    }

    void* start() const { return data_.start(); }
    void* end() const { return data_.end(); }

    // Dumps information about the Arena using printf().
    // TIP: Use "k mx htinfo" to dump the handle table at runtime.
    void Dump() const;

private:
    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;

    struct Node : public SinglyLinkedListable<Node*> {
        explicit Node(void* s)
            : slot(s) {}
        void* slot;
    };

    SinglyLinkedList<Node*> free_;

    // A memory pool that manually commits/decommits memory as
    // necessary, avoiding demand-paging.
    class Pool {
    public:
        // Initializes the pool. |mapping| must be fully backed by its
        // VMO, but should not have any committed pages. |slot_size|
        // is the size of object returned/accepted by Pop/Push.
        void Init(mxtl::RefPtr<VmMapping> mapping, size_t slot_size);

        // Returns a pointer to a |slot_size| piece of memory, or nullptr
        // if there is no memory available.
        void* Pop();

        // Makes |p| available to be returned by future calls to Pop.
        // |p| must be the pointer most recently returned by Pop, or
        // the method will ASSERT.
        void Push(void* p);

        // Returns true if |addr| could have been returned by Pop and has
        // not been reclaimed by Push.
        bool InRange(void* addr) const {
            return (addr >= start_ && addr < top_);
        }

        // The lowest address of the memory managed by this Pool.
        // Pop will only return values > |start| (besides nullptr).
        char* start() const { return start_; }

        // The highest address of the memory managed by this Pool.
        // Pop will only return values <= |end|-|slot_size| (besides nullptr).
        char* end() const { return end_; }

        // Dumps information about the Pool using printf().
        void Dump() const;

    private:
        mxtl::RefPtr<VmMapping> mapping_;
        size_t slot_size_;
        char* start_;
        char* top_;           // |start|..|top| contains all allocated slots.
        char* committed_;     // |start|..|mapped| is committed.
        char* committed_max_; // Largest committed_ value seen.
        char* end_;           // |mapped|..|end| is not committed.
    };

    Pool control_; // Free list nodes
    Pool data_;    // Objects

    // Parent VMAR of our memory.
    mxtl::RefPtr<VmAddressRegion> vmar_;

    friend class ArenaTestFriend;
};

} // namespace mxtl
