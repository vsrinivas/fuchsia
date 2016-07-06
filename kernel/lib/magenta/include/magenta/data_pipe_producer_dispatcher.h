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
#include <magenta/types.h>
#include <magenta/waiter.h>

#include <utils/ref_counted.h>

class DataPipe;
class VmAspace;

class DataPipeProducerDispatcher final : public Dispatcher {
public:
    static mx_status_t Create(utils::RefPtr<DataPipe> data_pipe,
                              utils::RefPtr<Dispatcher>* dispatcher,
                              mx_rights_t* rights);

    ~DataPipeProducerDispatcher() final;
    mx_obj_type_t GetType() const final { return MX_OBJ_TYPE_DATA_PIPE_PRODUCER; }
    DataPipeProducerDispatcher* get_data_pipe_producer_dispatcher() final { return this; }
    Waiter* get_waiter() final;

    mx_status_t BeginWrite(utils::RefPtr<VmAspace> aspace, void** buffer, mx_size_t* requested);
    mx_status_t EndWrite(mx_size_t written);

private:
    DataPipeProducerDispatcher(utils::RefPtr<DataPipe> pipe);

    utils::RefPtr<DataPipe> pipe_;
};
