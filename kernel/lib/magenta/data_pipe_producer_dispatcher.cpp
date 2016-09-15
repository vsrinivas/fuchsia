// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/data_pipe_producer_dispatcher.h>

#include <err.h>
#include <new.h>

#include <magenta/handle.h>
#include <magenta/data_pipe.h>

// TODO(cpu): the producer cannot be 'read' but we need the read right so it can be waited on.
// Consider a different right for waits.
constexpr mx_rights_t kDefaultDataPipeProducerRights =
        MX_RIGHT_TRANSFER | MX_RIGHT_WRITE | MX_RIGHT_READ | MX_RIGHT_GET_PROPERTY |
        MX_RIGHT_SET_PROPERTY;

// static
mx_status_t DataPipeProducerDispatcher::Create(mxtl::RefPtr<DataPipe> data_pipe,
                                               mxtl::RefPtr<Dispatcher>* dispatcher,
                                               mx_rights_t* rights) {
    AllocChecker ac;
    Dispatcher* consumer = new (&ac) DataPipeProducerDispatcher(mxtl::move(data_pipe));
    if (!ac.check())
        return ERR_NO_MEMORY;

    *rights = kDefaultDataPipeProducerRights;
    *dispatcher = mxtl::AdoptRef(consumer);
    return NO_ERROR;
}

DataPipeProducerDispatcher::DataPipeProducerDispatcher(mxtl::RefPtr<DataPipe> pipe)
    : pipe_(mxtl::move(pipe)) {
}

DataPipeProducerDispatcher::~DataPipeProducerDispatcher() {
    pipe_->OnProducerDestruction();
}

StateTracker* DataPipeProducerDispatcher::get_state_tracker() {
    return pipe_->get_producer_state_tracker();
}

mx_status_t DataPipeProducerDispatcher::Write(user_ptr<const void> buffer,
                                              mx_size_t* requested,
                                              bool all_or_none) {
    return pipe_->ProducerWriteFromUser(buffer, requested, all_or_none);
}

mx_ssize_t DataPipeProducerDispatcher::BeginWrite(mxtl::RefPtr<VmAspace> aspace, void** buffer) {
    return pipe_->ProducerWriteBegin(mxtl::move(aspace), buffer);
}

mx_status_t DataPipeProducerDispatcher::EndWrite(mx_size_t written) {
    return pipe_->ProducerWriteEnd(written);
}

mx_size_t DataPipeProducerDispatcher::GetWriteThreshold() {
    return pipe_->ProducerGetWriteThreshold();
}

mx_status_t DataPipeProducerDispatcher::SetWriteThreshold(mx_size_t threshold) {
    return pipe_->ProducerSetWriteThreshold(threshold);
}
