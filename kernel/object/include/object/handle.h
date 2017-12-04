// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <fbl/arena.h>
#include <fbl/atomic.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/macros.h>
#include <fbl/mutex.h>
#include <fbl/ref_ptr.h>
#include <stdint.h>
#include <zircon/types.h>

class Dispatcher;
class Handle;

// HandleOwner wraps a Handle in a unique_ptr-like object that has single
// ownership of the Handle and deletes it whenever it falls out of scope.
class HandleOwner {
public:
    HandleOwner() = default;
    HandleOwner(decltype(nullptr)) : h_(nullptr) {}

    explicit HandleOwner(Handle* h) : h_(h) {}

    HandleOwner(const HandleOwner&) = delete;
    HandleOwner& operator=(const HandleOwner&) = delete;

    HandleOwner(HandleOwner&& other) : h_(other.release()) {}

    HandleOwner& operator=(HandleOwner&& other) {
        reset(other.release());
        return *this;
    }

    ~HandleOwner() {
        Destroy();
    }

    Handle* operator->() const {
        return h_;
    }

    Handle* get() const {
        return h_;
    }

    Handle* release() {
        Handle* h = h_;
        h_ = nullptr;
        return h;
    }

    void reset(Handle* h) {
        Destroy();
        h_ = h;
    }

    void swap(HandleOwner& other) {
        Handle* h = h_;
        h_ = other.h_;
        other.h_ = h;
    }

    explicit operator bool() const { return h_ != nullptr; }

private:
    // Defined inline below.
    inline void Destroy();

    Handle* h_ = nullptr;
};

// A Handle is how a specific process refers to a specific Dispatcher.
class Handle final : public fbl::DoublyLinkedListable<Handle*> {
public:
    // Returns the Dispatcher to which this instance points.
    const fbl::RefPtr<Dispatcher>& dispatcher() const { return dispatcher_; }

    // Returns the process that owns this instance. Used to guarantee
    // that one process may not access a handle owned by a different process.
    zx_koid_t process_id() const {
        return process_id_.load(fbl::memory_order_relaxed);
    }

    // Sets the value returned by process_id().
    void set_process_id(zx_koid_t pid) {
        process_id_.store(pid, fbl::memory_order_relaxed);
    }

    // Returns the |rights| parameter that was provided when this instance
    // was created.
    uint32_t rights() const {
        return rights_;
    }

    // Returns true if this handle has all of the desired rights bits set.
    bool HasRights(zx_rights_t desired) const {
        return (rights_ & desired) == desired;
    }

    // Returns a value that can be decoded by Handle::FromU32() to derive a
    // pointer to this instance.  ProcessDispatcher will XOR this with its
    // |handle_rand_| to create the zx_handle_t value that user space sees.
    uint32_t base_value() const {
        return base_value_;
    }

    // To be called once during bringup.
    static void Init();

    // Maps an integer obtained by Handle::base_value() back to a Handle.
    static Handle* FromU32(uint32_t value);

    // Get the number of outstanding handles for a given dispatcher.
    static uint32_t Count(const fbl::RefPtr<const Dispatcher>&);

    // Should only be called by diagnostics.cpp.
    struct diagnostics {
        // Dumps internal details of the handle table using printf().
        static void DumpTableInfo();

        // Returns the number of outstanding handles.
        static size_t OutstandingHandles();
    };

    // Handle should never be created by anything other than Make or Dup.
    static HandleOwner Make(
        fbl::RefPtr<Dispatcher> dispatcher, zx_rights_t rights);
    static HandleOwner Dup(Handle* source, zx_rights_t rights);

private:
    DISALLOW_COPY_ASSIGN_AND_MOVE(Handle);

    // Called only by Make.
    Handle(fbl::RefPtr<Dispatcher> dispatcher,
           zx_rights_t rights, uint32_t base_value);
    // Called only by Dup.
    Handle(Handle* rhs, zx_rights_t rights, uint32_t base_value);

    // Private subroutines of Make and Dup.
    static void* Alloc(const fbl::RefPtr<Dispatcher>&, const char* what,
                       uint32_t* base_value);
    static uint32_t GetNewBaseValue(void* addr);

    // Handle should never be destroyed by anything other than Delete,
    // which uses TearDown to do the actual destruction.
    ~Handle() = default;
    void TearDown() TA_EXCL(mutex_);
    void Delete();

    // Only HandleOwner is allowed to call Delete.
    friend class HandleOwner;

    // process_id_ is atomic because threads from different processes can
    // access it concurrently, while holding different instances of
    // handle_table_lock_.
    fbl::atomic<zx_koid_t> process_id_;
    fbl::RefPtr<Dispatcher> dispatcher_;
    const zx_rights_t rights_;
    const uint32_t base_value_;

    // The handle arena and its mutex; also guards Dispatcher::handle_count_.
    static fbl::Mutex mutex_;
    static fbl::Arena TA_GUARDED(mutex_) arena_;

    // NOTE! This can return an invalid pointer.
    // It must be checked against the arena bounds before being used.
    static Handle* IndexToHandle(uint32_t index) TA_NO_THREAD_SAFETY_ANALYSIS {
        return reinterpret_cast<Handle*>(arena_.start()) + index;
    }

    static uint32_t HandleToIndex(Handle* handle) TA_NO_THREAD_SAFETY_ANALYSIS {
        return static_cast<uint32_t>(
            handle - reinterpret_cast<Handle*>(arena_.start()));
    }
};

// This can't be defined direclty in the HandleOwner class definition
// because Handle is an incomplete type at that point.
inline void HandleOwner::Destroy() {
    if (h_)
        h_->Delete();
}
