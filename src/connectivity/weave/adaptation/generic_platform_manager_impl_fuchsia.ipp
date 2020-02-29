// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef GENERIC_PLATFORM_MANAGER_IMPL_FUCHSIA_IPP
#define GENERIC_PLATFORM_MANAGER_IMPL_FUCHSIA_IPP

// clang-format off
#include <Weave/DeviceLayer/internal/WeaveDeviceLayerInternal.h>
#include <Weave/DeviceLayer/PlatformManager.h>
#include "generic_platform_manager_impl_fuchsia.h"

// Include the non-inline definitions for the GenericPlatformManagerImpl<> template,
// from which the GenericPlatformManagerImpl_Fuchsia<> template inherits.
#include <Weave/DeviceLayer/internal/GenericPlatformManagerImpl.ipp>
// clang-format on

#include <lib/syslog/cpp/logger.h>

namespace nl {
namespace Weave {
namespace DeviceLayer {
namespace Internal {

template<class ImplClass>
WEAVE_ERROR GenericPlatformManagerImpl_Fuchsia<ImplClass>::_InitWeaveStack(void)
{
    WEAVE_ERROR err = WEAVE_NO_ERROR;
    nl::Weave::WeaveMessageLayer::InitContext initContext;

    err = system_layer_.Init(nullptr);
    if (err != WEAVE_NO_ERROR) {
      FX_LOGS(ERROR) << "System layer init failed: " << ErrorStr(err);
      return err;
    }

    err = inet_layer_.Init(system_layer_, nullptr);
    if (err != WEAVE_NO_ERROR) {
      FX_LOGS(ERROR) << "Inet layer init failed: " << ErrorStr(err);
      return err;
    }

    err = fabric_state_.Init();
    if (err != WEAVE_NO_ERROR) {
      FX_LOGS(ERROR) << "FabricState init failed: " << ErrorStr(err);
      return err;
    }

    initContext.inet = &inet_layer_;
    initContext.systemLayer = &system_layer_;
    initContext.fabricState = &fabric_state_;
    initContext.listenTCP = true;
    initContext.listenUDP = true;

    err = message_layer_.Init(&initContext);
    if (err != WEAVE_NO_ERROR) {
      FX_LOGS(ERROR) << "Message layer init failed: "<< ErrorStr(err);
      return err;
    }

    err = ExchangeMgr.Init(&message_layer_);
    if (err != WEAVE_NO_ERROR) {
      FX_LOGS(ERROR) << "Exchange manager init failed: "<< ErrorStr(err);
      return err;
    }

    err = security_manager_.Init(ExchangeMgr, system_layer_);
    if (err != WEAVE_NO_ERROR) {
      FX_LOGS(ERROR) << "Security manager init failed: " << ErrorStr(err);
      return err;
    }

    err = DeviceDescriptionSvr().Init();
    if (err != WEAVE_NO_ERROR) {
      FX_LOGS(ERROR) << "device DeviceDescription init failed: "<< ErrorStr(err);
      return err;
    }

    err = DeviceControlSvr().Init();
    if (err != WEAVE_NO_ERROR) {
      FX_LOGS(ERROR) << "device control svr init failed: " << ErrorStr(err);
      return err;
    }

    err = FabricProvisioningSvr().Init();
    if (err != WEAVE_NO_ERROR) {
      FX_LOGS(ERROR) << "FabricProvisioningSvr init failed: " << ErrorStr(err);
      return err;
    }

    return err;
}

template<class ImplClass>
void GenericPlatformManagerImpl_Fuchsia<ImplClass>::_LockWeaveStack(void) {}

template<class ImplClass>
bool GenericPlatformManagerImpl_Fuchsia<ImplClass>::_TryLockWeaveStack(void)
{
  return true;
}

template<class ImplClass>
void GenericPlatformManagerImpl_Fuchsia<ImplClass>::_UnlockWeaveStack(void) {}

template<class ImplClass>
void GenericPlatformManagerImpl_Fuchsia<ImplClass>::_PostEvent(const WeaveDeviceEvent * event) {}

template<class ImplClass>
void GenericPlatformManagerImpl_Fuchsia<ImplClass>::_RunEventLoop(void) {}

template<class ImplClass>
WEAVE_ERROR GenericPlatformManagerImpl_Fuchsia<ImplClass>::_StartEventLoopTask(void) {
    return WEAVE_NO_ERROR;
}

template<class ImplClass>
void GenericPlatformManagerImpl_Fuchsia<ImplClass>::EventLoopTaskMain(void * arg) {}

template<class ImplClass>
WEAVE_ERROR GenericPlatformManagerImpl_Fuchsia<ImplClass>::_StartWeaveTimer(uint32_t aMilliseconds)
{
    return WEAVE_NO_ERROR;
}

template<class ImplClass>
System::Layer& GenericPlatformManagerImpl_Fuchsia<ImplClass>::GetSystemLayer()
{
  return system_layer_;
}

template<class ImplClass>
nl::Inet::InetLayer& GenericPlatformManagerImpl_Fuchsia<ImplClass>::GetInetLayer()
{
  return inet_layer_;
}

// Fully instantiate the generic implementation class in whatever compilation unit includes this file.
template class GenericPlatformManagerImpl_Fuchsia<PlatformManagerImpl>;

} // namespace Internal
} // namespace DeviceLayer
} // namespace Weave
} // namespace nl

#endif // GENERIC_PLATFORM_MANAGER_IMPL_FUCHSIA_IPP
