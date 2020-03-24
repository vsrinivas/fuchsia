// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_CONNECTIVITY_WEAVE_ADAPTATION_GENERIC_PLATFORM_MANAGER_IMPL_FUCHSIA_H_
#define SRC_CONNECTIVITY_WEAVE_ADAPTATION_GENERIC_PLATFORM_MANAGER_IMPL_FUCHSIA_H_

#include <Weave/DeviceLayer/internal/GenericPlatformManagerImpl.h>
#include <mutex>

namespace nl {
namespace Weave {
namespace DeviceLayer {
namespace Internal {

/**
 * Provides a generic implementation of PlatformManager features that works on Fuchsia platforms.
 *
 * This template contains implementations of select features from the PlatformManager abstract
 * interface that are suitable for use on Fuchsia-based platforms.  It is intended to be inherited
 * (directly or indirectly) by the PlatformManagerImpl class, which also appears as the template's
 * ImplClass parameter.
 */
template <class ImplClass>
class GenericPlatformManagerImpl_Fuchsia : public GenericPlatformManagerImpl<ImplClass> {
 protected:
  // ===== Methods that implement the PlatformManager abstract interface.

  WEAVE_ERROR _InitWeaveStack();
  void _ShutdownWeaveStack(void);
  void _LockWeaveStack(void);
  bool _TryLockWeaveStack(void);
  void _UnlockWeaveStack(void);
  void _PostEvent(const WeaveDeviceEvent* event);
  void _RunEventLoop(void);
  WEAVE_ERROR _StartEventLoopTask(void);
  WEAVE_ERROR _StartWeaveTimer(uint32_t durationMS);

 private:
  // ===== Private members for use by this class only.

  inline ImplClass* Impl() { return static_cast<ImplClass*>(this); }

  static void EventLoopTaskMain(void* arg);
  std::mutex mEventLoopLock;

  // Weave submodule instances.
  nl::Weave::System::Layer system_layer_;
  nl::Inet::InetLayer inet_layer_;
  nl::Weave::WeaveFabricState fabric_state_;
  nl::Weave::WeaveMessageLayer message_layer_;
  nl::Weave::WeaveSecurityManager security_manager_;
 public:
  System::Layer& GetSystemLayer();
  nl::Inet::InetLayer& GetInetLayer();
};

// Instruct the compiler to instantiate the template only when explicitly told to do so.
extern template class GenericPlatformManagerImpl_Fuchsia<PlatformManagerImpl>;

}  // namespace Internal
}  // namespace DeviceLayer
}  // namespace Weave
}  // namespace nl

#endif  // SRC_CONNECTIVITY_WEAVE_ADAPTATION_GENERIC_PLATFORM_MANAGER_IMPL_FUCHSIA_H_
