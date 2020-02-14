// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_CONNECTIVITY_WEAVE_ADAPTATION_SOFTWARE_UPDATE_MANAGER_IMPL_H_
#define SRC_CONNECTIVITY_WEAVE_ADAPTATION_SOFTWARE_UPDATE_MANAGER_IMPL_H_

#if WEAVE_DEVICE_CONFIG_ENABLE_SOFTWARE_UPDATE_MANAGER

#include <Weave/DeviceLayer/internal/GenericSoftwareUpdateManagerImpl.h>
#include <Weave/DeviceLayer/internal/GenericSoftwareUpdateManagerImpl_BDX.h>

namespace nl {
namespace Weave {
namespace DeviceLayer {

/**
 * Concrete implementation of the SoftwareUpdateManager singleton object for the
 * Fuchsia platforms.
 */
class SoftwareUpdateManagerImpl final
    : public SoftwareUpdateManager,
      public Internal::GenericSoftwareUpdateManagerImpl<SoftwareUpdateManagerImpl>,
      public Internal::GenericSoftwareUpdateManagerImpl_BDX<SoftwareUpdateManagerImpl> {
  // Allow the SoftwareUpdateManager interface class to delegate method calls to
  // the implementation methods provided by this class.
  friend class SoftwareUpdateManager;

  // Allow the GenericSoftwareUpdateManagerImpl base class to access helper methods
  // and types defined on this class.
  friend class Internal::GenericSoftwareUpdateManagerImpl<SoftwareUpdateManagerImpl>;

  // Allow the GenericSoftwareUpdateManagerImpl_BDX base class to access helper methods
  // and types defined on this class.
  friend class Internal::GenericSoftwareUpdateManagerImpl_BDX<SoftwareUpdateManagerImpl>;

 public:
  // ===== Members for internal use by the following friends.

  friend ::nl::Weave::DeviceLayer::SoftwareUpdateManager& SoftwareUpdateMgr(void);
  friend SoftwareUpdateManagerImpl& SoftwareUpdateMgrImpl(void);

  static SoftwareUpdateManagerImpl sInstance;

 private:
  // ===== Members that implement the SoftwareUpdateManager abstract interface.

  WEAVE_ERROR _Init(void);
};

/**
 * Returns a reference to the public interface of the SoftwareUpdateManager singleton object.
 *
 * Internal components should use this to access features of the SoftwareUpdateManager object
 * that are common to all platforms.
 */
inline SoftwareUpdateManager& SoftwareUpdateMgr(void) {
  return SoftwareUpdateManagerImpl::sInstance;
}

/**
 * Returns the platform-specific implementation of the SoftwareUpdateManager singleton object.
 *
 * Internal components can use this to gain access to features of the SoftwareUpdateManager
 * that are specific to the Fuchsia platform.
 */
inline SoftwareUpdateManagerImpl& SoftwareUpdateMgrImpl(void) {
  return SoftwareUpdateManagerImpl::sInstance;
}

}  // namespace DeviceLayer
}  // namespace Weave
}  // namespace nl

#endif  // WEAVE_DEVICE_CONFIG_ENABLE_SOFTWARE_UPDATE_MANAGER
#endif  // SRC_CONNECTIVITY_WEAVE_ADAPTATION_SOFTWARE_UPDATE_MANAGER_IMPL_H_
