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

constexpr mx_rights_t kDefaultDataPipeConsumerRights =
        MX_RIGHT_TRANSFER | MX_RIGHT_READ | MX_RIGHT_GET_PROPERTY | MX_RIGHT_SET_PROPERTY;

// static
mx_status_t DataPipeConsumerDispatcher::Create(mxtl::RefPtr<DataPipe> data_pipe,
                                               mxtl::RefPtr<Dispatcher>* dispatcher,
                                               mx_rights_t* rights) {
    AllocChecker ac;
    Dispatcher* producer = new (&ac) DataPipeConsumerDispatcher(mxtl::move(data_pipe));
    if (!ac.check())
        return ERR_NO_MEMORY;

    *rights = kDefaultDataPipeConsumerRights;
    *dispatcher = mxtl::AdoptRef(producer);
    return NO_ERROR;
}

DataPipeConsumerDispatcher::DataPipeConsumerDispatcher(mxtl::RefPtr<DataPipe> pipe)
    : pipe_(mxtl::move(pipe)) {
}

DataPipeConsumerDispatcher::~DataPipeConsumerDispatcher() {
    pipe_->OnConsumerDestruction();
}

StateTracker* DataPipeConsumerDispatcher::get_state_tracker() {
    return pipe_->get_consumer_state_tracker();
}

mx_status_t DataPipeConsumerDispatcher::Read(user_ptr<void> buffer,
                                             mx_size_t* requested,
                                             bool all_or_none,
                                             bool discard,
                                             bool peek) {
    return pipe_->ConsumerReadFromUser(buffer, requested, all_or_none, discard, peek);
}

mx_ssize_t DataPipeConsumerDispatcher::Query() {
    return pipe_->ConsumerQuery();
}

mx_ssize_t DataPipeConsumerDispatcher::BeginRead(mxtl::RefPtr<VmAspace> aspace, void** buffer) {
    return pipe_->ConsumerReadBegin(mxtl::move(aspace), buffer);
}

mx_status_t DataPipeConsumerDispatcher::EndRead(mx_size_t read) {
    return pipe_->ConsumerReadEnd(read);
}

mx_size_t DataPipeConsumerDispatcher::GetReadThreshold() {
    return pipe_->ConsumerGetReadThreshold();
}

mx_status_t DataPipeConsumerDispatcher::SetReadThreshold(mx_size_t threshold) {
    return pipe_->ConsumerSetReadThreshold(threshold);
}
