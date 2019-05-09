// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/array.h>
#include <fbl/string_piece.h>
#include <lib/zx/channel.h>

#include <fuchsia/device/mock/c/fidl.h>

namespace mock_device {

zx_status_t BindHook(const zx::channel& c, const fuchsia_device_mock_HookInvocation& record,
                     fbl::Array<const fuchsia_device_mock_Action>* actions_out);
zx_status_t ReleaseHook(const zx::channel& c, const fuchsia_device_mock_HookInvocation& record);
zx_status_t GetProtocolHook(const zx::channel& c, const fuchsia_device_mock_HookInvocation& record,
                            uint32_t protocol_id,
                            fbl::Array<const fuchsia_device_mock_Action>* actions_out);
zx_status_t OpenHook(const zx::channel& c, const fuchsia_device_mock_HookInvocation& record,
                     uint32_t flags, fbl::Array<const fuchsia_device_mock_Action>* actions_out);
zx_status_t CloseHook(const zx::channel& c, const fuchsia_device_mock_HookInvocation& record,
                      uint32_t flags, fbl::Array<const fuchsia_device_mock_Action>* actions_out);
zx_status_t UnbindHook(const zx::channel& c, const fuchsia_device_mock_HookInvocation& record,
                       fbl::Array<const fuchsia_device_mock_Action>* actions_out);
zx_status_t ReadHook(const zx::channel& c, const fuchsia_device_mock_HookInvocation& record,
                     uint64_t count, zx_off_t off,
                     fbl::Array<const fuchsia_device_mock_Action>* actions_out);
zx_status_t WriteHook(const zx::channel& c, const fuchsia_device_mock_HookInvocation& record,
                      const uint8_t* buffer_data, size_t buffer_count, zx_off_t off,
                      fbl::Array<const fuchsia_device_mock_Action>* actions_out);
zx_status_t GetSizeHook(const zx::channel& c, const fuchsia_device_mock_HookInvocation& record,
                        fbl::Array<const fuchsia_device_mock_Action>* actions_out);
zx_status_t SuspendHook(const zx::channel& c, const fuchsia_device_mock_HookInvocation& record,
                        uint32_t flags, fbl::Array<const fuchsia_device_mock_Action>* actions_out);
zx_status_t ResumeHook(const zx::channel& c, const fuchsia_device_mock_HookInvocation& record,
                       uint32_t flags, fbl::Array<const fuchsia_device_mock_Action>* actions_out);

zx_status_t IoctlHook(const zx::channel& c, const fuchsia_device_mock_HookInvocation& record,
                      uint32_t op, const uint8_t* in_data, size_t in_count, uint64_t out_count,
                      fbl::Array<const fuchsia_device_mock_Action>* actions_out);
zx_status_t MessageHook(const zx::channel& c, const fuchsia_device_mock_HookInvocation& record,
                        fbl::Array<const fuchsia_device_mock_Action>* actions_out);
zx_status_t RxrpcHook(const zx::channel& c, const fuchsia_device_mock_HookInvocation& record,
                      fbl::Array<const fuchsia_device_mock_Action>* actions_out);

zx_status_t SendAddDeviceDone(const zx::channel& c, uint64_t action_id);
zx_status_t SendRemoveDeviceDone(const zx::channel& c, uint64_t action_id);

zx_status_t SendAddDeviceDoneFromThread(const zx::channel& c, uint64_t action_id);
zx_status_t SendRemoveDeviceDoneFromThread(const zx::channel& c, uint64_t action_id);

// Returns ZX_ERR_STOP if channel has been closed
// Returns ZX_OK and populates |actions_out| on success.
zx_status_t WaitForPerformActions(const zx::channel& c,
                                  fbl::Array<const fuchsia_device_mock_Action>* actions_out);

} // namespace mock_device

