// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <err.h>
#include <stdint.h>

#include <kernel/event.h>

#include <magenta/handle.h>
#include <magenta/magenta.h>
#include <magenta/types.h>

#include <mxtl/ref_counted.h>
#include <mxtl/ref_ptr.h>

class StateTracker;

// The Kernel Objects. Keep this list sorted.
class DataPipeConsumerDispatcher;
class DataPipeProducerDispatcher;
class InterruptDispatcher;
class IoMappingDispatcher;
class IOPortDispatcher;
class LogDispatcher;
class MessagePipeDispatcher;
class PciDeviceDispatcher;
class PciInterruptDispatcher;
class ProcessDispatcher;
class SocketDispatcher;
class ThreadDispatcher;
class VmObjectDispatcher;
class WaitSetDispatcher;
class ResourceDispatcher;

class Dispatcher : public mxtl::RefCounted<Dispatcher> {
public:
    Dispatcher();
    virtual ~Dispatcher() {}

    mx_koid_t get_koid() const { return koid_; }

    void add_handle();

    void remove_handle();

    virtual mx_obj_type_t GetType() const = 0;

    virtual void on_zero_handles() { }

    virtual mx_koid_t get_inner_koid() const {
        return 0ULL;
    }

    // Note that |set_mask| and |clear_mask| are *not* previously validated. Also note that they may
    // "overlap", and that |clear_mask| should be cleared and then |set_mask| set.
    virtual status_t UserSignal(uint32_t clear_mask, uint32_t set_mask) {
        return ERR_NOT_SUPPORTED;
    }

    virtual StateTracker* get_state_tracker() {
        return nullptr;
    }

    virtual InterruptDispatcher* get_interrupt_dispatcher() {
        return nullptr;
    }

    virtual MessagePipeDispatcher* get_message_pipe_dispatcher() {
        return nullptr;
    }

    virtual ProcessDispatcher* get_process_dispatcher() {
        return nullptr;
    }

    virtual ThreadDispatcher* get_thread_dispatcher() {
        return nullptr;
    }

    virtual VmObjectDispatcher* get_vm_object_dispatcher() {
        return nullptr;
    }

    virtual PciDeviceDispatcher* get_pci_device_dispatcher() {
        return nullptr;
    }

    virtual PciInterruptDispatcher* get_pci_interrupt_dispatcher() {
        return nullptr;
    }

    virtual IoMappingDispatcher* get_io_mapping_dispatcher() {
        return nullptr;
    }

    virtual LogDispatcher* get_log_dispatcher() {
        return nullptr;
    }

    virtual IOPortDispatcher* get_io_port_dispatcher() {
        return nullptr;
    }

    virtual DataPipeProducerDispatcher* get_data_pipe_producer_dispatcher() {
        return nullptr;
    }

    virtual DataPipeConsumerDispatcher* get_data_pipe_consumer_dispatcher() {
        return nullptr;
    }

    virtual WaitSetDispatcher* get_wait_set_dispatcher() {
        return nullptr;
    }

    virtual SocketDispatcher* get_socket_dispatcher() {
        return nullptr;
    }

    virtual ResourceDispatcher* get_resource_dispatcher() {
        return nullptr;
    }

protected:
    static mx_koid_t GenerateKernelObjectId();

private:
    const mx_koid_t koid_;
    int handle_count_;
};
