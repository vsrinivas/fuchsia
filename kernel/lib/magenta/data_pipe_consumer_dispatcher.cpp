// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/data_pipe_consumer_dispatcher.h>

#include <err.h>
#include <new.h>

#include <magenta/handle.h>
#include <magenta/data_pipe.h>

constexpr mx_rights_t kDefaultDataPipeConsumerRights = MX_RIGHT_TRANSFER | MX_RIGHT_READ ;

// static
mx_status_t DataPipeConsumerDispatcher::Create(utils::RefPtr<DataPipe> data_pipe,
                                               utils::RefPtr<Dispatcher>* dispatcher,
                                               mx_rights_t* rights) {
    AllocChecker ac;
    Dispatcher* producer = new (&ac) DataPipeConsumerDispatcher(utils::move(data_pipe));
    if (!ac.check())
        return ERR_NO_MEMORY;

    *rights = kDefaultDataPipeConsumerRights;
    *dispatcher = utils::AdoptRef(producer);
    return NO_ERROR;
}

DataPipeConsumerDispatcher::DataPipeConsumerDispatcher(utils::RefPtr<DataPipe> pipe)
    : pipe_(utils::move(pipe)) {
}

DataPipeConsumerDispatcher::~DataPipeConsumerDispatcher() {
    pipe_->OnConsumerDestruction();
}

StateTracker* DataPipeConsumerDispatcher::get_state_tracker() {
    return pipe_->get_consumer_state_tracker();
}

mx_status_t DataPipeConsumerDispatcher::Read(void* buffer, mx_size_t* requested) {
    return pipe_->ConsumerReadFromUser(buffer, requested);
}

mx_status_t DataPipeConsumerDispatcher::BeginRead(utils::RefPtr<VmAspace> aspace,
                                                  void** buffer, mx_size_t* requested) {
    return pipe_->ConsumerReadBegin(utils::move(aspace), buffer, requested);
}

mx_status_t DataPipeConsumerDispatcher::EndRead(mx_size_t read) {
    return pipe_->ConsumerReadEnd(read);
}
