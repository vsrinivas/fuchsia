// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WEAVE_LIB_CORE_TRAIT_UPDATER_DELEGATE_IMPL_H_
#define SRC_CONNECTIVITY_WEAVE_LIB_CORE_TRAIT_UPDATER_DELEGATE_IMPL_H_

// clang-format off
#pragma GCC diagnostic push
#include <Weave/DeviceLayer/internal/DeviceIdentityTraitDataSource.h>
#include <Weave/DeviceLayer/internal/WeaveDeviceLayerInternal.h>
#pragma GCC diagnostic pop
// clang-format on

#include <lib/syslog/cpp/macros.h>

#include "src/connectivity/weave/lib/applets_loader/applets_loader.h"
#include "src/connectivity/weave/lib/applets_loader/applets_module.h"
#include "trait_updater.h"

namespace nl::Weave::DeviceLayer {

class TraitUpdaterDelegateImpl : public TraitUpdaterImpl::Delegate {
 public:
  TraitUpdaterDelegateImpl() = default;
  ~TraitUpdaterDelegateImpl() override = default;

  // Handles the incoming weave device event and sends it down to registered
  // applets.
  void HandleWeaveDeviceEvent(const WeaveDeviceEvent* event) override;

  // Perform the required initialization.
  WEAVE_ERROR Init() override;

  // Initialize applets list.
  WEAVE_ERROR InitApplets(std::vector<std::string>& applet_names);

  // Static function that publishes traits.
  static WEAVE_ERROR PublishTrait(const Profiles::DataManagement_Current::ResourceIdentifier res_id,
                                  const uint64_t instance_id,
                                  Profiles::DataManagement_Current::TraitDataSource* source_trait) {
    return TraitMgr().PublishTrait(res_id, instance_id, source_trait);
  }

  // Static function that subscribes to traits.
  static WEAVE_ERROR SubscribeTrait(
      const Profiles::DataManagement_Current::ResourceIdentifier res_id, const uint64_t instance_id,
      Profiles::DataManagement_Current::PropertyPathHandle base_path_handle,
      Profiles::DataManagement_Current::TraitDataSink* sink_trait) {
    return TraitMgr().SubscribeServiceTrait(res_id, instance_id, base_path_handle, sink_trait);
  }

 private:
  FuchsiaWeaveAppletsCallbacksV1 callbacks_ = {
      .publish_trait = &TraitUpdaterDelegateImpl::PublishTrait,
      .subscribe_trait = &TraitUpdaterDelegateImpl::SubscribeTrait,
  };
  std::vector<std::unique_ptr<weavestack::applets::AppletsLoader>> applets_loader_;
  std::vector<weavestack::applets::Applet> applets_;
};

}  // namespace nl::Weave::DeviceLayer

#endif  // SRC_CONNECTIVITY_WEAVE_LIB_CORE_TRAIT_UPDATER_DELEGATE_IMPL_H_
