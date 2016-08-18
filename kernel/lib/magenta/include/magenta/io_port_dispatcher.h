// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <kernel/mutex.h>
#include <kernel/event.h>

#include <magenta/dispatcher.h>
#include <magenta/io_port_observer.h>
#include <magenta/types.h>

#include <utils/fifo_buffer.h>
#include <sys/types.h>

struct IOP_Packet {
    friend struct IOP_PacketListTraits;

    static IOP_Packet* Alloc(mx_size_t size);
    static IOP_Packet* Make(const void* data, mx_size_t size);
    static IOP_Packet* MakeFromUser(const void* data, mx_size_t size);
    static void Delete(IOP_Packet* packet);

    IOP_Packet(mx_size_t data_size) : data_size(data_size) {}
    bool CopyToUser(void* data, mx_size_t* size);

    utils::DoublyLinkedListNodeState<IOP_Packet*> iop_lns_;
    mx_size_t data_size;
};

struct IOP_PacketListTraits {
    inline static utils::DoublyLinkedListNodeState<IOP_Packet*>& node_state(
            IOP_Packet& obj) {
        return obj.iop_lns_;
    }
};

class IOPortDispatcher final : public Dispatcher {
public:
    static status_t Create(uint32_t options,
                           utils::RefPtr<Dispatcher>* dispatcher,
                           mx_rights_t* rights);

    ~IOPortDispatcher() final;
    mx_obj_type_t GetType() const final { return MX_OBJ_TYPE_IOPORT; }
    IOPortDispatcher* get_io_port_dispatcher() final { return this; }
    void on_zero_handles() final;

    mx_status_t Queue(IOP_Packet* packet);
    mx_status_t Wait(IOP_Packet** packet);

    // Called under the handle table lock.
    mx_status_t Bind(Handle* handle, mx_signals_t signals, uint64_t key);
    mx_status_t Unbind(Handle* handle, uint64_t key);

    void CancelObserver(IOPortObserver* observer);

private:
    IOPortDispatcher(uint32_t options);
    void FreePackets_NoLock();

    utils::unique_ptr<IOPortObserver> MaybeRemoveObserver(IOP_Packet* packet);

    const uint32_t options_;
    Mutex lock_;

    bool no_clients_;
    utils::DoublyLinkedList<IOPortObserver*, IOPortObserverListTraits> observers_;
    utils::DoublyLinkedList<IOP_Packet*, IOP_PacketListTraits> packets_;

    event_t event_;
};
