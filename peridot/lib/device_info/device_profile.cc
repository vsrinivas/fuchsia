// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/device_info/device_profile.h>

#include "peridot/lib/rapidjson/rapidjson.h"

namespace modular {

// TODO(zbowling): this is a hack. we need to later decide how we want to
// identfiy a devices intent. For now just a flag to say the intends to act
// "like" a remote display server
constexpr char kPresentationServer[] = "remote_presentor";

DeviceProfile::DeviceProfile() {}

bool DeviceProfile::Parse(const std::string& jsonProfile) {
  fuchsia::modular::JsonDoc document;
  document.Parse(jsonProfile);
  if (!document.IsObject()) {
    return false;
  }

  if (document.HasMember(kPresentationServer) &&
      document[kPresentationServer].IsBool()) {
    presentation_server = document[kPresentationServer].GetBool();
  }

  return true;
}

bool DeviceProfile::ParseDefaultProfile() { return Parse(LoadDeviceProfile()); }

}  // namespace modular
