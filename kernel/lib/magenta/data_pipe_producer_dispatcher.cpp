// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/data_pipe_producer_dispatcher.h>

#include <err.h>

#include <magenta/handle.h>
#include <magenta/data_pipe.h>

constexpr mx_rights_t kDefaultDataPipeProducerRights =
    MX_RIGHT_TRANSFER | MX_RIGHT_WRITE | MX_RIGHT_READ;

// TODO(cpu): the producer cannot be 'read' but we need the read right so it
// can be waited on. Consider a different right for waits.

// static
mx_status_t DataPipeProducerDispatcher::Create(utils::RefPtr<DataPipe> data_pipe,
                                               utils::RefPtr<Dispatcher>* dispatcher,
                                               mx_rights_t* rights) {
    Dispatcher* consumer = new DataPipeProducerDispatcher(utils::move(data_pipe));
    if (!consumer)
        return ERR_NO_MEMORY;

    *rights = kDefaultDataPipeProducerRights;
    *dispatcher = utils::AdoptRef(consumer);
    return NO_ERROR;
}

DataPipeProducerDispatcher::DataPipeProducerDispatcher(utils::RefPtr<DataPipe> pipe)
    : pipe_(utils::move(pipe)) {
}

DataPipeProducerDispatcher::~DataPipeProducerDispatcher() {
    pipe_->OnProducerDestruction();
}

Waiter* DataPipeProducerDispatcher::get_waiter() {
    return pipe_->get_producer_waiter();
}

mx_status_t DataPipeProducerDispatcher::Write(const void* buffer, mx_size_t* requested) {
    return pipe_->ProducerWriteFromUser(buffer, requested);
}

mx_status_t DataPipeProducerDispatcher::BeginWrite(utils::RefPtr<VmAspace> aspace,
                                                   void** buffer, mx_size_t* requested) {
    return pipe_->ProducerWriteBegin(utils::move(aspace), buffer, requested);
}

mx_status_t DataPipeProducerDispatcher::EndWrite(mx_size_t written) {
    return pipe_->ProducerWriteEnd(written);
}
