// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WEAVE_ADAPTATION_NETWORK_PROVISIONING_SERVER_IMPL_H_
#define SRC_CONNECTIVITY_WEAVE_ADAPTATION_NETWORK_PROVISIONING_SERVER_IMPL_H_

#include <Weave/DeviceLayer/internal/GenericNetworkProvisioningServerImpl.h>

namespace nl {
namespace Weave {
namespace DeviceLayer {
namespace Internal {

/**
 * Concrete implementation of the NetworkProvisioningServer singleton object for the Fuchsia
 * platform.
 */
class NL_DLL_EXPORT NetworkProvisioningServerImpl final
    : public NetworkProvisioningServer,
      public GenericNetworkProvisioningServerImpl<NetworkProvisioningServerImpl> {
 private:
  using GenericImplClass = GenericNetworkProvisioningServerImpl<NetworkProvisioningServerImpl>;

  // Allow the NetworkProvisioningServer interface class to delegate method calls to
  // the implementation methods provided by this class.
  friend class ::nl::Weave::DeviceLayer::Internal::NetworkProvisioningServer;

  // Allow the GenericNetworkProvisioningServerImpl base class to access helper methods
  // and types defined on this class.
  friend class GenericNetworkProvisioningServerImpl<NetworkProvisioningServerImpl>;

 public:
  /**
   * Delegate class to handle platform-specific implementation of the
   * NetworkProvisioningServer API surface. This enables tests to swap out the
   * implementation of the static NetworkProvisioningServer instance.
   */
  class Delegate {
   public:
    virtual ~Delegate() = default;

    // Provides a handle to NetworkProvisioningServerImpl object that this delegate
    // was attached to.
    void SetNetworkProvisioningServerImpl(NetworkProvisioningServerImpl* impl) { impl_ = impl; }

    // NetworkProvisioningServer APIs.

    // Initializes delegate state.
    virtual WEAVE_ERROR Init() = 0;
    // Returns current WiFi station provision info.
    virtual WEAVE_ERROR GetWiFiStationProvision(NetworkInfo& net_info,
                                                bool include_credentials) = 0;

    // Set WLAN Network Config Provider which can be used to watch WLAN network provision updates.
    virtual void SetWlanNetworkConfigProvider(
        ::fidl::InterfaceHandle<class ::fuchsia::weave::WlanNetworkConfigProvider> provider) = 0;
    // Callback function for WLAN network provision updates.
    virtual void OnWlanNetworkUpdate(
        ::fuchsia::wlan::policy::NetworkConfig current_network_config) = 0;

   private:
    NetworkProvisioningServerImpl* impl_;
  };

  // Sets the delegate containing the platform-specific implementation. It is
  // invalid to invoke the NetworkProvisioningServer without setting a delegate
  // first. However, the OpenWeave surface requires a no-constructor
  // instantiation of this class, so it is up to the caller to enforce this.
  void SetDelegate(std::unique_ptr<Delegate> delegate);

  // Gets the delegate currently in use. This may return nullptr if no delegate
  // was set on this class.
  Delegate* GetDelegate();

  void SetWlanNetworkConfigProvider(
      ::fidl::InterfaceHandle<class ::fuchsia::weave::WlanNetworkConfigProvider> provider);

 private:
  // ===== Members that implement the NetworkProvisioningServer public interface.

  WEAVE_ERROR _Init(void);
  void _OnPlatformEvent(const WeaveDeviceEvent* event);

  // NOTE: Other public interface methods are implemented by GenericNetworkProvisioningServerImpl<>.

  // ===== Members used by GenericNetworkProvisioningServerImpl<> to invoke platform-specific
  //       operations.

  WEAVE_ERROR GetWiFiStationProvision(NetworkInfo& net_info, bool include_credentials);
  WEAVE_ERROR SetWiFiStationProvision(const NetworkInfo& net_info);
  WEAVE_ERROR ClearWiFiStationProvision(void);
  WEAVE_ERROR InitiateWiFiScan(void);
  void HandleScanDone(void);
  static NetworkProvisioningServerImpl& Instance(void);
  static void HandleScanTimeOut(::nl::Weave::System::Layer* a_layer, void* a_app_state,
                                ::nl::Weave::System::Error a_error);
  static bool IsSupportedWiFiSecurityType(WiFiSecurityType_t wifi_sec_type);

  // ===== Members for internal use by the following friends.

  friend ::nl::Weave::DeviceLayer::Internal::NetworkProvisioningServer& NetworkProvisioningSvr(
      void);
  friend NetworkProvisioningServerImpl& NetworkProvisioningSvrImpl(void);

  static NetworkProvisioningServerImpl sInstance;

  // ===== Private members reserved for use by this class only.
  std::unique_ptr<Delegate> delegate_;
};

/**
 * Returns a reference to the public interface of the NetworkProvisioningServer singleton object.
 *
 * Internal components should use this to access features of the NetworkProvisioningServer object
 * that are common to all platforms.
 */
inline NetworkProvisioningServer& NetworkProvisioningSvr(void) {
  return NetworkProvisioningServerImpl::sInstance;
}

/**
 * Returns the platform-specific implementation of the NetworkProvisioningServer singleton object.
 *
 * Internal components can use this to gain access to features of the NetworkProvisioningServer
 * that are specific to the Fuchsia platform.
 */
inline NetworkProvisioningServerImpl& NetworkProvisioningSvrImpl(void) {
  return NetworkProvisioningServerImpl::sInstance;
}

}  // namespace Internal
}  // namespace DeviceLayer
}  // namespace Weave
}  // namespace nl

#endif  // SRC_CONNECTIVITY_WEAVE_ADAPTATION_NETWORK_PROVISIONING_SERVER_IMPL_H_
