// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mock-device.h"

#include <lib/fidl/cpp/builder.h>
#include <lib/fidl/cpp/message_buffer.h>
#include <zircon/assert.h>

namespace libdriver_integration_test {

MockDevice::MockDevice(fidl::InterfaceRequest<Interface> request, async_dispatcher_t* dispatcher,
                       std::string path)
    : binding_(this, std::move(request), dispatcher), path_(std::move(path)) {
}

void MockDevice::Bind(HookInvocation record, BindCallback callback)  {
    hooks_->Bind(record, std::move(callback));
}

void MockDevice::Release(HookInvocation record) {
    hooks_->Release(record);

}

void MockDevice::GetProtocol(HookInvocation record, uint32_t protocol_id,
                             GetProtocolCallback callback)  {
    hooks_->GetProtocol(record, protocol_id, std::move(callback));
}

void MockDevice::Open(HookInvocation record, uint32_t flags, OpenCallback callback)  {
    hooks_->Open(record, flags, std::move(callback));
}

void MockDevice::OpenAt(HookInvocation record, std::string path, uint32_t flags,
                        OpenAtCallback callback)  {
    hooks_->OpenAt(record, path, flags, std::move(callback));
}

void MockDevice::Close(HookInvocation record, uint32_t flags, CloseCallback callback)  {
    hooks_->Close(record, flags, std::move(callback));
}

void MockDevice::Unbind(HookInvocation record, UnbindCallback callback)  {
    hooks_->Unbind(record, std::move(callback));
}

void MockDevice::Read(HookInvocation record, uint64_t count, uint64_t off,
                      ReadCallback callback)  {
    hooks_->Read(record, count, off, std::move(callback));
}

void MockDevice::Write(HookInvocation record, std::vector<uint8_t> buffer, uint64_t off,
                       WriteCallback callback)  {
    hooks_->Write(record, std::move(buffer), off, std::move(callback));
}

void MockDevice::GetSize(HookInvocation record, GetSizeCallback callback)  {
    hooks_->GetSize(record, std::move(callback));
}

void MockDevice::Suspend(HookInvocation record, uint32_t flags, SuspendCallback callback)  {
    hooks_->Suspend(record, flags, std::move(callback));
}

void MockDevice::Resume(HookInvocation record, uint32_t flags, ResumeCallback callback)  {
    hooks_->Resume(record, flags, std::move(callback));
}

void MockDevice::Ioctl(HookInvocation record, uint32_t op, std::vector<uint8_t> in,
                       uint64_t out_count, IoctlCallback callback)  {
    hooks_->Ioctl(record, op, std::move(in), out_count, std::move(callback));
}

void MockDevice::Message(HookInvocation record, MessageCallback callback)  {
    hooks_->Message(record, std::move(callback));
}

void MockDevice::Rxrpc(HookInvocation record, RxrpcCallback callback)  {
    hooks_->Rxrpc(record, std::move(callback));
}

void MockDevice::AddDeviceDone(uint64_t action_id) {
    // Check the list of pending actions and signal the corresponding completer
    auto itr = pending_actions_.find(action_id);
    ZX_ASSERT(itr != pending_actions_.end());
    itr->second.complete_ok();
    pending_actions_.erase(itr);
}

void MockDevice::RemoveDeviceDone(uint64_t action_id) {
    AddDeviceDone(action_id);
}

std::vector<ActionList::Action> MockDevice::FinalizeActionList(ActionList action_list) {
    std::vector<ActionList::Action> actions;
    std::map<uint64_t, fit::completer<void, std::string>> local_ids;
    action_list.Take(&actions, &local_ids);
    for (auto& action : actions) {
        uint64_t local_action_id = 0;
        if (action.is_add_device()) {
            local_action_id = action.add_device().action_id;
        } else if (action.is_remove_device()) {
            local_action_id = action.remove_device().action_id;
        } else {
            continue;
        }
        auto itr = local_ids.find(local_action_id);
        ZX_ASSERT(itr != local_ids.end());
        uint64_t remote_action_id = next_action_id_++;
        pending_actions_[remote_action_id] = std::move(itr->second);
        local_ids.erase(itr);
    }
    return actions;
}

} // namespace libdriver_integration_test
