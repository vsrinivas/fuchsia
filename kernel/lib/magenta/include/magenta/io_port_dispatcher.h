// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <new.h>
#include <kernel/mutex.h>
#include <kernel/event.h>

#include <magenta/dispatcher.h>
#include <magenta/types.h>

#include <mxtl/intrusive_double_list.h>

#include <sys/types.h>


struct IOP_Packet : public mxtl::DoublyLinkedListable<IOP_Packet*> {
    friend struct IOP_PacketListTraits;

    static IOP_Packet* Alloc(mx_size_t size);
    static IOP_Packet* Make(const void* data, mx_size_t size);
    static IOP_Packet* MakeFromUser(const void* data, mx_size_t size);
    static void Delete(IOP_Packet* packet);

    IOP_Packet(mx_size_t data_size)
        : is_signal(false), data_size(data_size) {}

    IOP_Packet(mx_size_t data_size, bool is_signal)
        : is_signal(is_signal), data_size(data_size) {}

    bool CopyToUser(void* data, mx_size_t* size);

    bool is_signal;
    mx_size_t data_size;
};

struct IOP_Signal : public IOP_Packet {
    mx_io_packet payload;
    volatile int count;

    IOP_Signal(uint64_t key, mx_signals_t signal);
};

// IO Port job is to deliver packets to threads waiting in Wait(). There
// are two types of packets:
//
// 1- Manually posted via Queue(), they are allocated in the
//    heap by the caller and freed in the syscall layer at the bottom
//    of mx_port_wait(). These Packets are of type IOP_Packet and only
//    live in the |packets| list.
//
// 2- Posted by bound dispatchers via Signal(), they are allocated once
//    during their first Signal() call and only freed when the IO port
//    reaches 'zero handles' state. These Packets are of type IOP_Signal
//    and bounce between the |packets_| list and the |at_zero_| list
//    depending on the |count| value:
//
//                                      +-----------+
//                                      |           |
//                                      v           |
// dispatcher-->observer->Signal()-->packets_-->Wait()-->port_wait()
//                           |          ^           |
//                           |          |           |
//                           +------>at_zero_ <-----+
//

class IOPortDispatcher final : public Dispatcher {
public:
    static status_t Create(uint32_t options,
                           mxtl::RefPtr<Dispatcher>* dispatcher,
                           mx_rights_t* rights);

    ~IOPortDispatcher() final;
    mx_obj_type_t get_type() const final { return MX_OBJ_TYPE_IOPORT; }
    void on_zero_handles() final;

    mx_status_t Queue(IOP_Packet* packet);
    void* Signal(void* cookie, uint64_t key, mx_signals_t signal);

    mx_status_t Wait(IOP_Packet** packet);

private:
    IOPortDispatcher(uint32_t options);
    void FreePackets_NoLock();

    const uint32_t options_;

    Mutex lock_;
    bool no_clients_;
    mxtl::DoublyLinkedList<IOP_Packet*> packets_;
    mxtl::DoublyLinkedList<IOP_Packet*> at_zero_;
    event_t event_;
};
