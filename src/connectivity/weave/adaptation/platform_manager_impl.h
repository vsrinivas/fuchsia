// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_CONNECTIVITY_WEAVE_ADAPTATION_PLATFORM_MANAGER_IMPL_H_
#define SRC_CONNECTIVITY_WEAVE_ADAPTATION_PLATFORM_MANAGER_IMPL_H_

#include <BuildConfig.h>
#include <lib/sys/cpp/component_context.h>

#include "generic_platform_manager_impl_fuchsia.h"

namespace nl::Weave::DeviceLayer {

/**
 * Concrete implementation of the PlatformManager singleton object for the Fuchsia platform.
 */
class NL_DLL_EXPORT PlatformManagerImpl final
    : public PlatformManager,
      public Internal::GenericPlatformManagerImpl_Fuchsia<PlatformManagerImpl> {
  // Allow the PlatformManager interface class to delegate method calls to
  // the implementation methods provided by this class.
  friend PlatformManager;

  // Allow the generic implementation base class to call helper methods on
  // this class.
  friend Internal::GenericPlatformManagerImpl_Fuchsia<PlatformManagerImpl>;

 public:
  // ===== Platform-specific members that may be accessed directly by the application.
  void ShutdownWeaveStack();
  sys::ComponentContext* GetComponentContextForProcess();
  void SetComponentContextForProcess(std::unique_ptr<sys::ComponentContext> context);

  // Sets the dispatcher to which tasks will be posted.
  //
  // This method will panic if |dispatcher| is NULL.
  void SetDispatcher(async_dispatcher_t* dispatcher);

  // Gets the dispatcher to which tasks should be posted.
  async_dispatcher_t* GetDispatcher();

  // Gets the platform data required for openweave.
  // Platform data contains component context and dispatcher.
  InetLayer::FuchsiaPlatformData* GetPlatformData();

  // Posts an event to dispatcher.
  void PostEvent(const WeaveDeviceEvent* event);

 private:
  // ===== Methods that implement the PlatformManager abstract interface.

  // Initializes the Weave stack.
  //
  // Precondition: Delegates have been set for ConfigurationManager, ConnectivityManager, and
  // ThreadStackManager.
  WEAVE_ERROR _InitWeaveStack();

  // Posts an event to the dispatcher. The event will be handled asynchronously
  // by the main async loop.
  //
  // This method will panic if the dispatcher is not set.
  void _PostEvent(const WeaveDeviceEvent* event);

  // ===== Members for internal use by the following friends.

  friend PlatformManager& PlatformMgr();
  friend PlatformManagerImpl& PlatformMgrImpl();

  static PlatformManagerImpl sInstance;
  std::unique_ptr<sys::ComponentContext> context_;
  async_dispatcher_t* dispatcher_;
  InetLayer::FuchsiaPlatformData platform_data_;
};

inline void PlatformManagerImpl::PostEvent(const WeaveDeviceEvent* event) {
  return _PostEvent(event);
}

/**
 * Returns the public interface of the PlatformManager singleton object.
 *
 * Weave applications should use this to access features of the PlatformManager object
 * that are common to all platforms.
 */
inline PlatformManager& PlatformMgr() { return PlatformManagerImpl::sInstance; }

/**
 * Returns the platform-specific implementation of the PlatformManager singleton object.
 *
 * Weave applications can use this to gain access to features of the PlatformManager
 * that are specific to the Fuchsia platform.
 */
inline PlatformManagerImpl& PlatformMgrImpl() { return PlatformManagerImpl::sInstance; }

}  // namespace nl::Weave::DeviceLayer

#endif  // SRC_CONNECTIVITY_WEAVE_ADAPTATION_PLATFORM_MANAGER_IMPL_H_
