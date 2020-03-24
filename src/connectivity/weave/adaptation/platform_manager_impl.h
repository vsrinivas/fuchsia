// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_CONNECTIVITY_WEAVE_ADAPTATION_PLATFORM_MANAGER_IMPL_H_
#define SRC_CONNECTIVITY_WEAVE_ADAPTATION_PLATFORM_MANAGER_IMPL_H_

#include <BuildConfig.h>

#include "generic_platform_manager_impl_fuchsia.h"

namespace nl {
namespace Weave {
namespace DeviceLayer {

/**
 * Concrete implementation of the PlatformManager singleton object for the Fuchsia platform.
 */
class PlatformManagerImpl final
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
 void ShutdownWeaveStack(void);
 private:
  // ===== Methods that implement the PlatformManager abstract interface.

  WEAVE_ERROR _InitWeaveStack(void);

  // ===== Members for internal use by the following friends.

  friend PlatformManager& PlatformMgr(void);
  friend PlatformManagerImpl& PlatformMgrImpl(void);

  static PlatformManagerImpl sInstance;
};

/**
 * Returns the public interface of the PlatformManager singleton object.
 *
 * Weave applications should use this to access features of the PlatformManager object
 * that are common to all platforms.
 */
inline PlatformManager& PlatformMgr(void) { return PlatformManagerImpl::sInstance; }

/**
 * Returns the platform-specific implementation of the PlatformManager singleton object.
 *
 * Weave applications can use this to gain access to features of the PlatformManager
 * that are specific to the Fuchsia platform.
 */
inline PlatformManagerImpl& PlatformMgrImpl(void) { return PlatformManagerImpl::sInstance; }

}  // namespace DeviceLayer
}  // namespace Weave
}  // namespace nl

#endif  // SRC_CONNECTIVITY_WEAVE_ADAPTATION_PLATFORM_MANAGER_IMPL_H_
