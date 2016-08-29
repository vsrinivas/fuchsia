// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stdint.h>

#include <kernel/mutex.h>
#include <kernel/vm/vm_aspace.h>

#include <magenta/dispatcher.h>
#include <magenta/state_tracker.h>
#include <magenta/types.h>

#include <mxtl/ref_counted.h>

class DataPipe;
class VmAspace;

class DataPipeProducerDispatcher final : public Dispatcher {
public:
    static mx_status_t Create(mxtl::RefPtr<DataPipe> data_pipe,
                              mxtl::RefPtr<Dispatcher>* dispatcher,
                              mx_rights_t* rights);

    ~DataPipeProducerDispatcher() final;
    mx_obj_type_t GetType() const final { return MX_OBJ_TYPE_DATA_PIPE_PRODUCER; }
    DataPipeProducerDispatcher* get_data_pipe_producer_dispatcher() final { return this; }
    StateTracker* get_state_tracker() final;

    mx_status_t Write(const void* buffer, mx_size_t* requested);
    mx_status_t BeginWrite(mxtl::RefPtr<VmAspace> aspace, void** buffer, mx_size_t* requested);
    mx_status_t EndWrite(mx_size_t written);

private:
    DataPipeProducerDispatcher(mxtl::RefPtr<DataPipe> pipe);

    mxtl::RefPtr<DataPipe> pipe_;
};
