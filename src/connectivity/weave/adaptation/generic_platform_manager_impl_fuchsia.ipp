// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef GENERIC_PLATFORM_MANAGER_IMPL_FUCHSIA_IPP
#define GENERIC_PLATFORM_MANAGER_IMPL_FUCHSIA_IPP

// clang-format off
#pragma GCC diagnostic push
#include <Weave/DeviceLayer/internal/WeaveDeviceLayerInternal.h>
#include <Weave/DeviceLayer/PlatformManager.h>
#pragma GCC diagnostic pop
#include "generic_platform_manager_impl_fuchsia.h"
#include "thread_stack_manager_impl.h"

// Include the non-inline definitions for the GenericPlatformManagerImpl<> template,
// from which the GenericPlatformManagerImpl_Fuchsia<> template inherits.
#pragma GCC diagnostic push
#include <Weave/DeviceLayer/internal/GenericPlatformManagerImpl.ipp>
#pragma GCC diagnostic pop
// clang-format on

#include <lib/syslog/cpp/macros.h>
#pragma GCC diagnostic push
#include <Warm/Warm.h>
#pragma GCC diagnostic pop

#include "platform_auth_delegate.h"
#include "weave_inspector.h"

namespace nl {
namespace Weave {
namespace DeviceLayer {
namespace Internal {
namespace {
using nl::Weave::WeaveInspector;
}

template<class ImplClass>
WEAVE_ERROR GenericPlatformManagerImpl_Fuchsia<ImplClass>::_InitWeaveStack(void)
{
    WEAVE_ERROR err = WEAVE_NO_ERROR;
    nl::Weave::WeaveMessageLayer::InitContext initContext;

    err = ConfigurationMgr().Init();
    if (err != WEAVE_NO_ERROR) {
      FX_LOGS(ERROR) << "ConfigurationManager init failed: " << ErrorStr(err);
      return err;
    }

    // Initialize the Weave system layer.
    // Placement new operation here constructs the SystemLayer object
    // that is already allocated as global variable.
    new (&SystemLayer) System::Layer();
    err = SystemLayer.Init(nullptr);
    if (err != WEAVE_SYSTEM_NO_ERROR) {
      FX_LOGS(ERROR) << "SystemLayer init failed: " << ErrorStr(err);
      return err;
    }

    // Initialize the Weave Inet layer.
    new (&InetLayer) Inet::InetLayer();
    err = InetLayer.Init(SystemLayer, nullptr);
    if (err != INET_NO_ERROR) {
      FX_LOGS(ERROR) << "InetLayer init failed: " << ErrorStr(err);
      return err;
    }

    InetLayer.SetPlatformData(PlatformMgrImpl().GetPlatformData());

    // Initialize the Weave fabric state object.
    new (&FabricState) WeaveFabricState();
    err = FabricState.Init();
    if (err != WEAVE_NO_ERROR) {
      FX_LOGS(ERROR) << "FabricState init failed: " << ErrorStr(err);
      return err;
    }

    initContext.inet = &InetLayer;
    initContext.systemLayer = &SystemLayer;
    initContext.fabricState = &FabricState;
    initContext.listenTCP = true;
    initContext.listenUDP = true;
#if WEAVE_DEVICE_CONFIG_ENABLE_WOBLE
    if(ConfigurationMgrImpl().IsWoBLEEnabled()) {
      initContext.ble = BLEMgr().GetBleLayer();
      initContext.listenBLE = true;
    }
#endif

    // Initialize the Weave message layer.
    new (&MessageLayer) WeaveMessageLayer();
    err = MessageLayer.Init(&initContext);
    if (err != WEAVE_NO_ERROR) {
      FX_LOGS(ERROR) << "MessageLayer init failed: "<< ErrorStr(err);
      return err;
    }

    err = ExchangeMgr.Init(&MessageLayer);
    if (err != WEAVE_NO_ERROR) {
      FX_LOGS(ERROR) << "ExchangeManager init failed: "<< ErrorStr(err);
      return err;
    }

    // Initialize the Weave security manager.
    new (&SecurityMgr) WeaveSecurityManager();
    err = SecurityMgr.Init(ExchangeMgr, SystemLayer);
    if (err != WEAVE_NO_ERROR) {
      FX_LOGS(ERROR) << "SecurityManager init failed: " << ErrorStr(err);
      return err;
    }

    SecurityMgr.OnSessionEstablished = HandleSessionSuccess;
    err = InitCASEAuthDelegate();
    if (err != WEAVE_NO_ERROR) {
      FX_LOGS(ERROR) << "PlatformCASEAuthDelegate init failed: " << ErrorStr(err);
      return err;
    }

#if WEAVE_CONFIG_ENABLE_KEY_EXPORT_RESPONDER
    err = InitKeyExportDelegate();
    if (err != WEAVE_NO_ERROR) {
      FX_LOGS(ERROR) << "PlatformKeyExportDelegate init failed: " << ErrorStr(err);
      return err;
    }
#endif  // WEAVE_CONFIG_ENABLE_KEY_EXPORT_RESPONDER

    // Perform dynamic configuration of the core Weave objects based on stored settings.
    //
    // NB: In general, initialization of Device Layer objects should happen *after* this call
    // as their initialization methods may rely on the proper initialization of the core
    // objects.
    err = ConfigurationMgr().ConfigureWeaveStack();
    if (err != WEAVE_NO_ERROR) {
      FX_LOGS(ERROR) << "ConfigurationManager dynamic config failed: " << ErrorStr(err);
    }

    // Initialize the service directory manager.
    err = InitServiceDirectoryManager();
    if (err != WEAVE_NO_ERROR) {
      FX_LOGS(ERROR) << "ServiceDirectoryManager init failed: " << ErrorStr(err);
    }

    err = DeviceDescriptionSvr().Init();
    if (err != WEAVE_NO_ERROR) {
      FX_LOGS(ERROR) << "DeviceDescriptionServer init failed: "<< ErrorStr(err);
      return err;
    }

    err = DeviceControlSvr().Init();
    if (err != WEAVE_NO_ERROR) {
      FX_LOGS(ERROR) << "DeviceControlServer init failed: " << ErrorStr(err);
      return err;
    }

    // Initialize the Network Provisioning server.
    err = NetworkProvisioningSvr().Init();
    if (err != WEAVE_NO_ERROR)
    {
      FX_LOGS(ERROR) << "NetworkProvisioningServer init failed: " << ErrorStr(err);
      return err;
    }

    err = FabricProvisioningSvr().Init();
    if (err != WEAVE_NO_ERROR) {
      FX_LOGS(ERROR) << "FabricProvisioningServer init failed: " << ErrorStr(err);
      return err;
    }

#if WEAVE_DEVICE_CONFIG_ENABLE_WOBLE
    if(ConfigurationMgrImpl().IsWoBLEEnabled()) {
      err = BLEMgr().Init();
      if (err != WEAVE_NO_ERROR)
      {
          FX_LOGS(ERROR) << "BLEManager initialization failed: " << ErrorStr(err);
          return err;
      }
    }
#endif

    err = ServiceProvisioningSvr().Init();
    if (err != WEAVE_NO_ERROR) {
      FX_LOGS(ERROR) << "ServiceProvisioningSvr init failed: " << ErrorStr(err);
      return err;
    }

    err = EchoSvr().Init();
    if (err != WEAVE_NO_ERROR) {
      FX_LOGS(ERROR) << "EchoSvr init failed: " << ErrorStr(err);
      return err;
    }

    err = ConnectivityMgr().Init();
    if (err != WEAVE_NO_ERROR) {
      FX_LOGS(ERROR) << "ConnectivityMgr init failed: " << ErrorStr(err);
      return err;
    }

    err = TimeSyncMgr().Init();
    if (err != WEAVE_NO_ERROR) {
      FX_LOGS(ERROR) << "TimeSyncMgr init failed: " << ErrorStr(err);
      return err;
    }

    // Initialize Weave Event Logging.
    err = InitWeaveEventLogging();
    if (err != WEAVE_NO_ERROR) {
      FX_LOGS(ERROR) << "Event Logging initialization failed: " << ErrorStr(err);
      return err;
    }

    err = TraitMgr().Init();
    if (err != WEAVE_NO_ERROR) {
      FX_LOGS(ERROR) << "TraitMgr init failed: " << ErrorStr(err);
      return err;
    }

    err = ThreadStackMgr().InitThreadStack();
    if (err != WEAVE_NO_ERROR) {
      FX_LOGS(ERROR) << "ThreadStackMgr init failed: " << ErrorStr(err);
      return err;
    }

    // Create WeaveINspectore and record initialization status
    WeaveInspector::GetWeaveInspector().NotifyInit();

    return WEAVE_NO_ERROR;
}


template <class ImplClass>
void GenericPlatformManagerImpl_Fuchsia<ImplClass>::HandleSessionSuccess(
    WeaveSecurityManager* sm, WeaveConnection* con, void* req_state, uint16_t session_key_id,
    uint64_t peer_node_id, uint8_t enc_type) {
  // Post event to notify listeners of the new session auth mode.
  GenericPlatformManagerImpl<ImplClass>::HandleSessionEstablished(
      sm, con, req_state, session_key_id, peer_node_id, enc_type);

#if WEAVE_CONFIG_ENABLE_KEY_EXPORT_RESPONDER
  // When establishing a session to the core router, add bastion-type services
  // to the session to allow them to make requests via the shared-session.
  if (!con && peer_node_id == kServiceEndpoint_CoreRouter && sm->FabricState->IsSharedSession(session_key_id, peer_node_id)) {
    WEAVE_ERROR err = sm->FabricState->AddSharedSessionEndNode(kServiceEndpoint_Bastion, kServiceEndpoint_CoreRouter, session_key_id);
    if (err != WEAVE_NO_ERROR) {
      FX_LOGS(ERROR) << "Failed to add shared-session end node: " << ErrorStr(err);
    }
  }
#endif  // WEAVE_CONFIG_ENABLE_KEY_EXPORT_RESPONDER

  // Notify inspector to update session auth mode state.
  WeaveSessionKey* session_key;
  FabricState.GetSessionKey(session_key_id, peer_node_id, session_key);
  WeaveAuthMode auth_mode =
      (session_key != NULL) ? session_key->AuthMode : (WeaveAuthMode)kWeaveAuthMode_NotSpecified;
  auto& inspector = WeaveInspector::GetWeaveInspector();
  if (auth_mode & kWeaveAuthMode_PASE_PairingCode) {
    inspector.NotifySetupStateChange(WeaveInspector::kSetupState_PASESessionEstablished);
  }
  if (auth_mode & kWeaveAuthMode_CASE_Device) {
    inspector.NotifySetupStateChange(WeaveInspector::kSetupState_CASESessionEstablished);
  }
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
  return SystemLayer;
}

template<class ImplClass>
nl::Inet::InetLayer& GenericPlatformManagerImpl_Fuchsia<ImplClass>::GetInetLayer()
{
  return InetLayer;
}

template<class ImplClass>
nl::Weave::Profiles::ServiceDirectory::WeaveServiceManager&
GenericPlatformManagerImpl_Fuchsia<ImplClass>::GetServiceDirectoryManager()
{
  return Internal::ServiceDirectoryMgr;
}

template<class ImplClass>
nl::Weave::Profiles::DeviceControl::DeviceControlDelegate&
GenericPlatformManagerImpl_Fuchsia<ImplClass>::GetDeviceControl()
{
  return DeviceControlSvr();
}

template<class ImplClass>
void GenericPlatformManagerImpl_Fuchsia<ImplClass>::_ShutdownWeaveStack(void)
{
  Warm::Shutdown(FabricState);
  EchoSvr().Shutdown();
  ServiceProvisioningSvr().Shutdown();
  FabricProvisioningSvr().Shutdown();
  DeviceControlSvr().Shutdown();
  DeviceDescriptionSvr().Shutdown();
  Internal::ServiceDirectoryMgr.reset();
  SecurityMgr.Shutdown();
  ExchangeMgr.Shutdown();
  MessageLayer.Shutdown();
  FabricState.Shutdown();
  InetLayer.Shutdown();
  SystemLayer.Shutdown();
}

// Fully instantiate the generic implementation class in whatever compilation unit includes this file.
template class GenericPlatformManagerImpl_Fuchsia<PlatformManagerImpl>;

} // namespace Internal
} // namespace DeviceLayer
} // namespace Weave
} // namespace nl

#endif // GENERIC_PLATFORM_MANAGER_IMPL_FUCHSIA_IPP
