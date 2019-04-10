// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_COBALT_APP_SYSTEM_DATA_UPDATER_IMPL_H_
#define GARNET_BIN_COBALT_APP_SYSTEM_DATA_UPDATER_IMPL_H_

#include <stdlib.h>

#include <fuchsia/cobalt/cpp/fidl.h>

#include "src/lib/fxl/macros.h"
#include "third_party/cobalt/encoder/system_data.h"

namespace cobalt {

class SystemDataUpdaterImpl : public fuchsia::cobalt::SystemDataUpdater {
 public:
  SystemDataUpdaterImpl(encoder::SystemData* system_data);

 private:
  // Resets Cobalt's view of the system-wide experiment state and replaces it
  // with the given values.
  //
  // |experiments|  All experiments the device has a notion of and the
  // arms the device belongs to for each of them. These are the only
  // experiments the device can collect data for.
  void SetExperimentState(
      std::vector<fuchsia::cobalt::Experiment> experiments,
      SetExperimentStateCallback callback) override;

  void SetChannel(::fidl::StringPtr current_channel, ::fidl::StringPtr target_channel, SetChannelCallback callback) override;

  encoder::SystemData* system_data_;  // Not owned.

  FXL_DISALLOW_COPY_AND_ASSIGN(SystemDataUpdaterImpl);
};

}  // namespace cobalt

#endif  // GARNET_BIN_COBALT_APP_SYSTEM_DATA_UPDATER_IMPL_H_
