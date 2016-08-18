// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/msg_pipe_dispatcher.h>

#include <string.h>

#include <assert.h>
#include <err.h>
#include <new.h>
#include <trace.h>

#include <kernel/auto_lock.h>

#include <magenta/handle.h>
#include <magenta/msg_pipe.h>

#define LOCAL_TRACE 0

constexpr mx_rights_t kDefaultPipeRights = MX_RIGHT_TRANSFER | MX_RIGHT_READ | MX_RIGHT_WRITE;

// static
status_t MessagePipeDispatcher::Create(uint32_t flags,
                                       utils::RefPtr<Dispatcher>* dispatcher0,
                                       utils::RefPtr<Dispatcher>* dispatcher1,
                                       mx_rights_t* rights) {
    LTRACE_ENTRY;

    AllocChecker ac;
    utils::RefPtr<MessagePipe> pipe = utils::AdoptRef(new (&ac) MessagePipe(GenerateKernelObjectId()));
    if (!ac.check()) return ERR_NO_MEMORY;

    Dispatcher* msgp0 = new (&ac) MessagePipeDispatcher((flags & ~MX_FLAG_REPLY_PIPE), 0u, pipe);
    if (!ac.check()) return ERR_NO_MEMORY;

    Dispatcher* msgp1 = new (&ac) MessagePipeDispatcher(flags, 1u, pipe);
    if (!ac.check()) return ERR_NO_MEMORY;

    *rights = kDefaultPipeRights;
    *dispatcher0 = utils::AdoptRef(msgp0);
    *dispatcher1 = utils::AdoptRef(msgp1);
    return NO_ERROR;
}

MessagePipeDispatcher::MessagePipeDispatcher(uint32_t flags,
                                             size_t side, utils::RefPtr<MessagePipe> pipe)
    : side_(side), flags_(flags), pipe_(utils::move(pipe)) {
}

MessagePipeDispatcher::~MessagePipeDispatcher() {
    pipe_->OnDispatcherDestruction(side_);
}

StateTracker* MessagePipeDispatcher::get_state_tracker() {
    return pipe_->GetStateTracker(side_);
}

status_t MessagePipeDispatcher::BeginRead(uint32_t* message_size, uint32_t* handle_count) {
    LTRACE_ENTRY;
    // Note that a second thread can arrive here before the first thread
    // calls AcceptRead(). Both threads now race to retrieve this message.
    status_t result;
    {
        AutoLock lock(&lock_);
        result = pending_ ? NO_ERROR : pipe_->Read(side_, &pending_);
        if (result == NO_ERROR) {
            *message_size = static_cast<uint32_t>(pending_->data.size());
            *handle_count = static_cast<uint32_t>(pending_->handles.size());
        }
    }
    return result;
}

status_t MessagePipeDispatcher::AcceptRead(utils::Array<uint8_t>* data,
                                           utils::Array<Handle*>* handles) {
    LTRACE_ENTRY;

    utils::unique_ptr<MessagePacket> msg;
    {
        AutoLock lock(&lock_);
        msg = utils::move(pending_);
        // if there is no message it means another user thread beat us here.
        if (!msg) return ERR_BAD_STATE;

        *data = utils::move(msg->data);
        *handles = utils::move(msg->handles);
    }
    return NO_ERROR;
}

status_t MessagePipeDispatcher::Write(utils::Array<uint8_t> data, utils::Array<Handle*> handles) {
    LTRACE_ENTRY;
    AllocChecker ac;
    utils::unique_ptr<MessagePacket> msg(
        new (&ac) MessagePacket(utils::move(data), utils::move(handles)));
    if (!ac.check()) return ERR_NO_MEMORY;

    return pipe_->Write(side_, utils::move(msg));
}
