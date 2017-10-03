// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <err.h>
#include <stdint.h>

#include <zircon/syscalls/object.h>
#include <zircon/types.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <object/handle.h>

template <typename T> struct DispatchTag;

#define DECLARE_DISPTAG(T, E)               \
class T;                                    \
template <> struct DispatchTag<T> {         \
    static constexpr zx_obj_type_t ID = E;  \
};

DECLARE_DISPTAG(ProcessDispatcher, ZX_OBJ_TYPE_PROCESS)
DECLARE_DISPTAG(ThreadDispatcher, ZX_OBJ_TYPE_THREAD)
DECLARE_DISPTAG(VmObjectDispatcher, ZX_OBJ_TYPE_VMO)
DECLARE_DISPTAG(ChannelDispatcher, ZX_OBJ_TYPE_CHANNEL)
DECLARE_DISPTAG(EventDispatcher, ZX_OBJ_TYPE_EVENT)
DECLARE_DISPTAG(PortDispatcher, ZX_OBJ_TYPE_PORT)
DECLARE_DISPTAG(InterruptDispatcher, ZX_OBJ_TYPE_INTERRUPT)
DECLARE_DISPTAG(PciDeviceDispatcher, ZX_OBJ_TYPE_PCI_DEVICE)
DECLARE_DISPTAG(LogDispatcher, ZX_OBJ_TYPE_LOG)
DECLARE_DISPTAG(SocketDispatcher, ZX_OBJ_TYPE_SOCKET)
DECLARE_DISPTAG(ResourceDispatcher, ZX_OBJ_TYPE_RESOURCE)
DECLARE_DISPTAG(EventPairDispatcher, ZX_OBJ_TYPE_EVENT_PAIR)
DECLARE_DISPTAG(JobDispatcher, ZX_OBJ_TYPE_JOB)
DECLARE_DISPTAG(VmAddressRegionDispatcher, ZX_OBJ_TYPE_VMAR)
DECLARE_DISPTAG(FifoDispatcher, ZX_OBJ_TYPE_FIFO)
DECLARE_DISPTAG(GuestDispatcher, ZX_OBJ_TYPE_GUEST)
DECLARE_DISPTAG(VcpuDispatcher, ZX_OBJ_TYPE_VCPU)
DECLARE_DISPTAG(TimerDispatcher, ZX_OBJ_TYPE_TIMER)

#undef DECLARE_DISPTAG

class StateTracker;
class StateObserver;
class CookieJar;

class Dispatcher : public fbl::RefCounted<Dispatcher> {
public:
    Dispatcher();
    virtual ~Dispatcher();

    zx_koid_t get_koid() const { return koid_; }

    // Must be called under the handle table lock.
    void increment_handle_count() {
        ++handle_count_;
    }

    // Must be called under the handle table lock.
    // Returns true exactly when the handle count goes to zero.
    bool decrement_handle_count() {
        --handle_count_;
        return handle_count_ == 0u;
    }

    // Must be called under the handle table lock.
    uint32_t current_handle_count() const {
        return handle_count_;
    }

    // Interface for derived classes.

    virtual zx_obj_type_t get_type() const = 0;

    virtual StateTracker* get_state_tracker() { return nullptr; }

    virtual zx_status_t add_observer(StateObserver* observer);

    virtual zx_status_t user_signal(uint32_t clear_mask, uint32_t set_mask, bool peer);

    virtual void on_zero_handles() { }

    virtual zx_koid_t get_related_koid() const { return 0ULL; }

    // get_name() will return a null-terminated name of ZX_MAX_NAME_LEN - 1 or fewer
    // characters.  For objects that don't have names it will be "".
    virtual void get_name(char out_name[ZX_MAX_NAME_LEN]) const { out_name[0] = 0; }

    // set_name() will truncate to ZX_MAX_NAME_LEN - 1 and ensure there is a
    // terminating null
    virtual zx_status_t set_name(const char* name, size_t len) { return ZX_ERR_NOT_SUPPORTED; }

    // Dispatchers that support get/set cookie must provide
    // a CookieJar for those cookies to be stored in.
    virtual CookieJar* get_cookie_jar() { return nullptr; }

private:
    const zx_koid_t koid_;
    uint32_t handle_count_;
};

// DownCastDispatcher checks if a RefPtr<Dispatcher> points to a
// dispatcher of a given dispatcher subclass T and, if so, moves the
// reference to a RefPtr<T>, otherwise it leaves the
// RefPtr<Dispatcher> alone.  Must be called with a pointer to a valid
// (non-null) dispatcher.

// Note that the Dispatcher -> Dispatcher versions come up in generic
// code, and so aren't totally vacuous.

// Dispatcher -> FooDispatcher
template <typename T>
fbl::RefPtr<T> DownCastDispatcher(fbl::RefPtr<Dispatcher>* disp) {
    return (likely(DispatchTag<T>::ID == (*disp)->get_type())) ?
            fbl::RefPtr<T>::Downcast(fbl::move(*disp)) :
            nullptr;
}

// Dispatcher -> Dispatcher
template <>
inline fbl::RefPtr<Dispatcher> DownCastDispatcher(fbl::RefPtr<Dispatcher>* disp) {
    return fbl::move(*disp);
}

// const Dispatcher -> const FooDispatcher
template <typename T>
fbl::RefPtr<T> DownCastDispatcher(fbl::RefPtr<const Dispatcher>* disp) {
    static_assert(fbl::is_const<T>::value, "");
    return (likely(DispatchTag<typename fbl::remove_const<T>::type>::ID == (*disp)->get_type())) ?
            fbl::RefPtr<T>::Downcast(fbl::move(*disp)) :
            nullptr;
}

// const Dispatcher -> const Dispatcher
template <>
inline fbl::RefPtr<const Dispatcher> DownCastDispatcher(fbl::RefPtr<const Dispatcher>* disp) {
    return fbl::move(*disp);
}
