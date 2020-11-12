// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WEAVE_LIB_APPLETS_WEAVE_APPLETS_H_
#define SRC_CONNECTIVITY_WEAVE_LIB_APPLETS_WEAVE_APPLETS_H_

#include <lib/sys/cpp/component_context.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <Weave/DeviceLayer/TraitManager.h>

__BEGIN_CDECLS

typedef void* fuchsia_weave_applets_handle_t;
const fuchsia_weave_applets_handle_t FUCHSIA_WEAVE_APPLETS_INVALID_HANDLE = nullptr;

struct FuchsiaWeaveAppletsCallbacksV1 {
  // Registers the provided source trait with Weave (mimics the TraitManager interface). It
  // is expected that |source_trait| exists until unpublished.
  WEAVE_ERROR(*publish_trait)
  (const nl::Weave::Profiles::DataManagement_Current::ResourceIdentifier res_id,
   const uint64_t instance_id,
   nl::Weave::Profiles::DataManagement_Current::TraitDataSource* source_trait);

  // Registers the provided sink trait with Weave (mimics the TraitManager interface). It is
  // expected that a |sink_trait| exists until unsubscribed.
  WEAVE_ERROR(*subscribe_trait)
  (const nl::Weave::Profiles::DataManagement_Current::ResourceIdentifier res_id,
   const uint64_t instance_id,
   nl::Weave::Profiles::DataManagement_Current::PropertyPathHandle base_path_handle,
   nl::Weave::Profiles::DataManagement_Current::TraitDataSink* sink_trait);
};

struct FuchsiaWeaveAppletsModuleV1 {
  // Returns a 64-bit handle representing an applet instance. In case of
  // failure, FUCHSIA_WEAVE_APPLETS_INVALID_HANDLE is returned. The callback
  // handle is meant to be used by the applet created by the call to create_applet.
  fuchsia_weave_applets_handle_t (*create_applet)(FuchsiaWeaveAppletsCallbacksV1 callbacks);

  // Deletes an active applet instance.
  bool (*delete_applet)(fuchsia_weave_applets_handle_t);
};

// Declare an exported module instance from a loadable plugin module:
//
// DECLARE_FUCHSIA_WEAVE_APPLETS_MODULE_V1 {
//   .create_applet = &my_create_applet,
//   .delete_applet = &my_delete_applet,
//   ...
// }
#define DECLARE_FUCHSIA_WEAVE_APPLETS_MODULE_V1 \
  __EXPORT FuchsiaWeaveAppletsModuleV1 FuchsiaWeaveAppletsModuleV1_instance

__END_CDECLS

#endif  // SRC_CONNECTIVITY_WEAVE_LIB_APPLETS_WEAVE_APPLETS_H_
