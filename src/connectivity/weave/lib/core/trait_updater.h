// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WEAVE_LIB_CORE_TRAIT_UPDATER_H_
#define SRC_CONNECTIVITY_WEAVE_LIB_CORE_TRAIT_UPDATER_H_

// clang-format off
#pragma GCC diagnostic push
#include <Weave/DeviceLayer/internal/WeaveDeviceLayerInternal.h>
#pragma GCC diagnostic pop
// clang-format on

#include <lib/syslog/cpp/macros.h>

#include <memory>

namespace nl::Weave::DeviceLayer {

class TraitUpdaterImpl {
 private:
  TraitUpdaterImpl() = default;

 public:
  class Delegate {
   public:
    virtual ~Delegate() = default;

    // Provides a handle to ConnectivityManagerImpl object that this delegate
    // was attached to. This allows the delegate to invoke functions on
    // GenericConnectivityManagerImpl if required.
    void SetTraitUpdaterImpl(TraitUpdaterImpl* impl) { impl_ = impl; }

    // Receives |event| and sends it to the all the registered applets.
    virtual void HandleWeaveDeviceEvent(const WeaveDeviceEvent* event) = 0;

    virtual WEAVE_ERROR Init() = 0;

   private:
    TraitUpdaterImpl* impl_;
  };

  // Sets the delegate containing the platform-specific implementation. It is
  // invalid to invoke the TraitUpdaterImpl without setting a delegate
  // first.
  void SetDelegate(std::unique_ptr<Delegate> delegate);

  // Gets the delegate currently in use. This may return nullptr if no delegate
  // was set on this class.
  Delegate* GetDelegate();

  WEAVE_ERROR Init();

  // Receives |event| and sends it to the all the registered applets.
  void HandleWeaveDeviceEvent(const WeaveDeviceEvent* event);

  // Static handler to trampoline event calls into an instance. |arg|
  // is a pointer to the instance.
  static void TrampolineEvent(const WeaveDeviceEvent* event, intptr_t arg);

 private:
  friend TraitUpdaterImpl& TraitUpdater();
  static TraitUpdaterImpl sInstance;

  std::unique_ptr<Delegate> delegate_;
};

inline TraitUpdaterImpl& TraitUpdater() { return TraitUpdaterImpl::sInstance; }

}  // namespace nl::Weave::DeviceLayer

#endif  // SRC_CONNECTIVITY_WEAVE_LIB_CORE_TRAIT_UPDATER_H_
