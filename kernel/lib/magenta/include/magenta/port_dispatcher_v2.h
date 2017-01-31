// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <kernel/mutex.h>

#include <magenta/dispatcher.h>
#include <magenta/syscalls/port.h>
#include <magenta/types.h>
#include <magenta/wait_event.h>

#include <sys/types.h>

#include <mxtl/intrusive_double_list.h>

struct PortPacket final : public mxtl::DoublyLinkedListable<PortPacket*> {
    bool from_heap;
    mx_port_packet_t packet;

    PortPacket(bool from_heap) : from_heap(from_heap) {}
};

class PortDispatcherV2 final : public Dispatcher {
public:
    static status_t Create(uint32_t options,
                           mxtl::RefPtr<Dispatcher>* dispatcher,
                           mx_rights_t* rights);

    ~PortDispatcherV2() final;
    mx_obj_type_t get_type() const final { return MX_OBJ_TYPE_IOPORT2; }

    void on_zero_handles() final;

    mx_status_t Queue(PortPacket* packet);
    mx_status_t DeQueue(mx_time_t timeout, PortPacket** packet);

private:
    PortDispatcherV2(uint32_t options);

    WaitEvent event_;

    Mutex lock_;
    mxtl::DoublyLinkedList<PortPacket*> packets_ TA_GUARDED(lock_);
};
