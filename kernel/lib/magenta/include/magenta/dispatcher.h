// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <err.h>
#include <stdint.h>

#include <magenta/handle.h>
#include <magenta/port_client.h>
#include <magenta/magenta.h>
#include <magenta/syscalls/object.h>
#include <magenta/types.h>

#include <mxtl/ref_counted.h>
#include <mxtl/ref_ptr.h>
#include <mxtl/unique_ptr.h>

template <typename T> struct DispatchTag;

#define DECLARE_DISPTAG(T, E)               \
class T;                                    \
template <> struct DispatchTag<T> {         \
    static constexpr mx_obj_type_t ID = E;  \
};

DECLARE_DISPTAG(ProcessDispatcher, MX_OBJ_TYPE_PROCESS)
DECLARE_DISPTAG(ThreadDispatcher, MX_OBJ_TYPE_THREAD)
DECLARE_DISPTAG(VmObjectDispatcher, MX_OBJ_TYPE_VMEM)
DECLARE_DISPTAG(ChannelDispatcher, MX_OBJ_TYPE_CHANNEL)
DECLARE_DISPTAG(EventDispatcher, MX_OBJ_TYPE_EVENT)
DECLARE_DISPTAG(PortDispatcher, MX_OBJ_TYPE_IOPORT)
DECLARE_DISPTAG(InterruptDispatcher, MX_OBJ_TYPE_INTERRUPT)
DECLARE_DISPTAG(IoMappingDispatcher, MX_OBJ_TYPE_IOMAP)
DECLARE_DISPTAG(PciDeviceDispatcher, MX_OBJ_TYPE_PCI_DEVICE)
DECLARE_DISPTAG(LogDispatcher, MX_OBJ_TYPE_LOG)
DECLARE_DISPTAG(WaitSetDispatcher, MX_OBJ_TYPE_WAIT_SET)
DECLARE_DISPTAG(SocketDispatcher, MX_OBJ_TYPE_SOCKET)
DECLARE_DISPTAG(ResourceDispatcher, MX_OBJ_TYPE_RESOURCE)
DECLARE_DISPTAG(EventPairDispatcher, MX_OBJ_TYPE_EVENT_PAIR)
DECLARE_DISPTAG(JobDispatcher, MX_OBJ_TYPE_JOB)

#undef DECLARE_DISPTAG

class StateTracker;

class Dispatcher : public mxtl::RefCounted<Dispatcher> {
public:
    Dispatcher();
    virtual ~Dispatcher();

    mx_koid_t get_koid() const { return koid_; }

    void add_handle();

    void remove_handle();

    template <typename T>
    T* get_specific() {
        return (DispatchTag<T>::ID == get_type())? static_cast<T*>(this) : nullptr;
    }

    // Interface for derived classes.

    virtual mx_obj_type_t get_type() const = 0;

    virtual StateTracker* get_state_tracker() { return nullptr; }

    virtual status_t user_signal(uint32_t clear_mask, uint32_t set_mask, bool peer);

    virtual status_t set_port_client(mxtl::unique_ptr<PortClient>) { return ERR_NOT_SUPPORTED; }

    virtual void on_zero_handles() { }

    virtual mx_koid_t get_inner_koid() const { return 0ULL; }

    // get_name() will return a null-terminated name of MX_MAX_NAME_LEN - 1 or fewer
    // characters.  For objects that don't have names it will be "".
    virtual void get_name(char out_name[MX_MAX_NAME_LEN]) const { out_name[0] = 0; }

    // set_name() will truncate to MX_MAX_NAME_LEN - 1 and ensure there is a
    // terminating null
    virtual status_t set_name(const char* name, size_t len) { return ERR_NOT_SUPPORTED; }

protected:
    static mx_koid_t GenerateKernelObjectId();

private:
    const mx_koid_t koid_;
    int handle_count_;
};

template <typename T>
mxtl::RefPtr<T> DownCastDispatcher(mxtl::RefPtr<Dispatcher>&& disp) {
    auto ptr = disp.leak_ref()->get_specific<T>();
    return mxtl::internal::MakeRefPtrNoAdopt(ptr);
}
