// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl_helpers.h"

namespace lib_gap = ::bluetooth::gap;
namespace fidl_bt = ::bluetooth;
namespace fidl_control = ::bluetooth::control;

namespace bluetooth_service {
namespace fidl_helpers {

::fidl_control::AdapterInfoPtr NewAdapterInfo(const ::lib_gap::Adapter& adapter) {
  auto adapter_info = ::fidl_control::AdapterInfo::New();
  adapter_info->state = ::fidl_control::AdapterState::New();

  // TODO(armansito): Most of these fields have not been implemented yet. Assign the correct values
  // when they are supported.
  adapter_info->state->powered = ::fidl_bt::Bool::New();
  adapter_info->state->powered->value = true;
  adapter_info->state->discovering = ::fidl_bt::Bool::New();
  adapter_info->state->discoverable = ::fidl_bt::Bool::New();

  adapter_info->identifier = adapter.identifier();
  adapter_info->address = adapter.state().controller_address().ToString();
  adapter_info->appearance = ::fidl_control::Appearance::UNKNOWN;

  return adapter_info;
}

}  // namespace fidl_helpers
}  // namespace bluetooth_service
