// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stdint.h>

#include <kernel/mutex.h>

#include <utils/ref_counted.h>
#include <utils/ref_ptr.h>

#include <magenta/waiter.h>

class Handle;
class VmObject;
class VmAspace;

struct PipeConsumer;
struct PipeProducer;

class DataPipe : public utils::RefCounted<DataPipe> {
public:
    static mx_status_t Create(mx_size_t capacity,
                              utils::RefPtr<Dispatcher>* producer,
                              utils::RefPtr<Dispatcher>* consumer,
                              mx_rights_t* producer_rights,
                              mx_rights_t* consumer_rights);

    ~DataPipe();

    Waiter* get_producer_waiter();
    Waiter* get_consumer_waiter();

    mx_status_t ProducerWriteBegin(utils::RefPtr<VmAspace> aspace, void** ptr, mx_size_t* requested);
    mx_status_t ProducerWriteEnd(mx_size_t written);

    mx_status_t ConsumerReadBegin(utils::RefPtr<VmAspace> aspace, void** ptr, mx_size_t* requested);
    mx_status_t ConsumerReadEnd(mx_size_t read);

    void OnProducerDestruction();
    void OnConsumerDestruction();

private:
    struct EndPoint {
        bool alive = true;
        mx_size_t cursor = 0u;
        vaddr_t vad_start = 0u;
        mx_size_t max_size = 0u;
        utils::RefPtr<VmAspace> aspace;
        Waiter waiter;
    };

    DataPipe(mx_size_t capacity);
    bool Init();

    const mx_size_t capacity_;

    mutex_t lock_;
    EndPoint producer_;
    EndPoint consumer_;
    utils::RefPtr<VmObject> vmo_;
    uint64_t available_;
};
