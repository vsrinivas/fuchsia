// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// clang-format off
#include <Weave/DeviceLayer/internal/WeaveDeviceLayerInternal.h>
#include <Weave/DeviceLayer/PlatformManager.h>

#include "generic_platform_manager_impl_fuchsia.ipp"
#include "configuration_manager_delegate_impl.h"
#include "thread_stack_manager_delegate_impl.h"
// clang-format on

#include <lib/async/default.h>
#include <lib/async/cpp/task.h>

namespace nl {
namespace Weave {
namespace DeviceLayer {

PlatformManagerImpl PlatformManagerImpl::sInstance;

WEAVE_ERROR PlatformManagerImpl::_InitWeaveStack(void) {
  ConfigurationMgrImpl().SetDelegate(std::make_unique<ConfigurationManagerDelegateImpl>());
  ThreadStackMgrImpl().SetDelegate(std::make_unique<ThreadStackManagerDelegateImpl>());
  return Internal::GenericPlatformManagerImpl_Fuchsia<PlatformManagerImpl>::_InitWeaveStack();
}

sys::ComponentContext *PlatformManagerImpl::GetComponentContextForProcess(void) {
  if (!context_) {
    context_ = sys::ComponentContext::CreateAndServeOutgoingDirectory();
  }
  return context_.get();
}

void PlatformManagerImpl::SetComponentContextForProcess(
    std::unique_ptr<sys::ComponentContext> context) {
  context_ = std::move(context);
}

void PlatformManagerImpl::SetDispatcher(async_dispatcher_t *dispatcher) {
  ZX_ASSERT(dispatcher != NULL);
  dispatcher_ = dispatcher;
}

void PlatformManagerImpl::_PostEvent(const WeaveDeviceEvent *event) {
  ZX_ASSERT(dispatcher_ != NULL);
  async::PostTask(dispatcher_, [ev = *event] { PlatformMgr().DispatchEvent(&ev); });
}

void PlatformManagerImpl::ShutdownWeaveStack(void) {
  Internal::GenericPlatformManagerImpl_Fuchsia<PlatformManagerImpl>::_ShutdownWeaveStack();
  ConfigurationMgrImpl().SetDelegate(nullptr);
  context_.reset();
}

InetLayer::FuchsiaPlatformData* PlatformManagerImpl::GetPlatformData(void) {
  platform_data_.ctx = GetComponentContextForProcess();
  platform_data_.dispatcher = dispatcher_;
  return &platform_data_;
}

}  // namespace DeviceLayer
}  // namespace Weave
}  // namespace nl
