// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stdint.h>

#include <kernel/mutex.h>
#include <mxtl/ref_counted.h>
#include <mxtl/ref_ptr.h>
#include <magenta/state_tracker.h>

class Dispatcher;
class Handle;
class VmObject;
class VmAspace;

struct PipeConsumer;
struct PipeProducer;

constexpr mx_size_t kMaxDataPipeCapacity = 256 * 1024 * 1024;

class DataPipe : public mxtl::RefCounted<DataPipe> {
public:
    static mx_status_t Create(mx_size_t element_size,
                              mx_size_t capacity,
                              mxtl::RefPtr<Dispatcher>* producer,
                              mxtl::RefPtr<Dispatcher>* consumer,
                              mx_rights_t* producer_rights,
                              mx_rights_t* consumer_rights);

    ~DataPipe();

    StateTracker* get_producer_state_tracker() { return &producer_.state_tracker; }
    StateTracker* get_consumer_state_tracker() { return &consumer_.state_tracker; }

    mx_status_t ProducerWriteFromUser(const void* ptr, mx_size_t* requested);
    mx_ssize_t ProducerWriteBegin(mxtl::RefPtr<VmAspace> aspace, void** ptr);
    mx_status_t ProducerWriteEnd(mx_size_t written);

    mx_status_t ConsumerReadFromUser(void* ptr,
                                     mx_size_t* requested,
                                     bool all_or_none,
                                     bool discard,
                                     bool peek);
    mx_ssize_t ConsumerQuery();
    mx_ssize_t ConsumerReadBegin(mxtl::RefPtr<VmAspace> aspace, void** ptr);
    mx_status_t ConsumerReadEnd(mx_size_t read);

    void OnProducerDestruction();
    void OnConsumerDestruction();

private:
    struct EndPoint {
        bool alive = true;
        bool read_only = false;
        mx_size_t cursor = 0u;
        char* vad_start = 0u;
        mx_size_t expected = 0u;
        mxtl::RefPtr<VmAspace> aspace;
        StateTracker state_tracker;
    };

    DataPipe(mx_size_t element_size, mx_size_t capacity);
    bool Init();

    mx_size_t ComputeSize(mx_size_t from, mx_size_t to, mx_size_t requested) const;

    // Must be called under |lock_|:
    mx_status_t MapVMOIfNeededNoLock(EndPoint* ep, mxtl::RefPtr<VmAspace> aspace);
    void UpdateSignalsNoLock();

    const mx_size_t element_size_;
    const mx_size_t capacity_;

    Mutex lock_;
    EndPoint producer_;
    EndPoint consumer_;
    mxtl::RefPtr<VmObject> vmo_;
    mx_size_t free_space_;
};
