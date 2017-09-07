// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <err.h>
#include <stdint.h>

#include <magenta/syscalls/object.h>
#include <magenta/types.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <object/handle.h>

template <typename T> struct DispatchTag;

#define DECLARE_DISPTAG(T, E)               \
class T;                                    \
template <> struct DispatchTag<T> {         \
    static constexpr mx_obj_type_t ID = E;  \
};

DECLARE_DISPTAG(ProcessDispatcher, MX_OBJ_TYPE_PROCESS)
DECLARE_DISPTAG(ThreadDispatcher, MX_OBJ_TYPE_THREAD)
DECLARE_DISPTAG(VmObjectDispatcher, MX_OBJ_TYPE_VMO)
DECLARE_DISPTAG(ChannelDispatcher, MX_OBJ_TYPE_CHANNEL)
DECLARE_DISPTAG(EventDispatcher, MX_OBJ_TYPE_EVENT)
DECLARE_DISPTAG(PortDispatcher, MX_OBJ_TYPE_PORT)
DECLARE_DISPTAG(InterruptDispatcher, MX_OBJ_TYPE_INTERRUPT)
DECLARE_DISPTAG(PciDeviceDispatcher, MX_OBJ_TYPE_PCI_DEVICE)
DECLARE_DISPTAG(LogDispatcher, MX_OBJ_TYPE_LOG)
DECLARE_DISPTAG(SocketDispatcher, MX_OBJ_TYPE_SOCKET)
DECLARE_DISPTAG(ResourceDispatcher, MX_OBJ_TYPE_RESOURCE)
DECLARE_DISPTAG(EventPairDispatcher, MX_OBJ_TYPE_EVENT_PAIR)
DECLARE_DISPTAG(JobDispatcher, MX_OBJ_TYPE_JOB)
DECLARE_DISPTAG(VmAddressRegionDispatcher, MX_OBJ_TYPE_VMAR)
DECLARE_DISPTAG(FifoDispatcher, MX_OBJ_TYPE_FIFO)
DECLARE_DISPTAG(GuestDispatcher, MX_OBJ_TYPE_GUEST)
DECLARE_DISPTAG(VcpuDispatcher, MX_OBJ_TYPE_VCPU)
DECLARE_DISPTAG(TimerDispatcher, MX_OBJ_TYPE_TIMER)

#undef DECLARE_DISPTAG

class StateTracker;
class StateObserver;
class CookieJar;

class Dispatcher : public fbl::RefCounted<Dispatcher> {
public:
    Dispatcher();
    virtual ~Dispatcher();

    mx_koid_t get_koid() const { return koid_; }

    // Updating |handle_count_| is done at the Handle management layer.
    uint32_t* get_handle_count_ptr() { return &handle_count_; }

    // Interface for derived classes.

    virtual mx_obj_type_t get_type() const = 0;

    virtual StateTracker* get_state_tracker() { return nullptr; }

    virtual mx_status_t add_observer(StateObserver* observer);

    virtual mx_status_t user_signal(uint32_t clear_mask, uint32_t set_mask, bool peer);

    virtual void on_zero_handles() { }

    virtual mx_koid_t get_related_koid() const { return 0ULL; }

    // get_name() will return a null-terminated name of MX_MAX_NAME_LEN - 1 or fewer
    // characters.  For objects that don't have names it will be "".
    virtual void get_name(char out_name[MX_MAX_NAME_LEN]) const { out_name[0] = 0; }

    // set_name() will truncate to MX_MAX_NAME_LEN - 1 and ensure there is a
    // terminating null
    virtual mx_status_t set_name(const char* name, size_t len) { return MX_ERR_NOT_SUPPORTED; }

    // Dispatchers that support get/set cookie must provide
    // a CookieJar for those cookies to be stored in.
    virtual CookieJar* get_cookie_jar() { return nullptr; }

private:
    const mx_koid_t koid_;
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
