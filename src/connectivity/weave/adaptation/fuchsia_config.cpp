// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// clang-format off
#include <Weave/DeviceLayer/internal/WeaveDeviceLayerInternal.h>
#include "fuchsia_config.h"
// clang-format on

namespace nl {
namespace Weave {
namespace DeviceLayer {
namespace Internal {

WEAVE_ERROR FuchsiaConfig::Init() { return WEAVE_NO_ERROR; }

WEAVE_ERROR FuchsiaConfig::ReadConfigValue(Key key, bool& val) { return WEAVE_NO_ERROR; }

WEAVE_ERROR FuchsiaConfig::ReadConfigValue(Key key, uint32_t& val) { return WEAVE_NO_ERROR; }

WEAVE_ERROR FuchsiaConfig::ReadConfigValue(Key key, uint64_t& val) { return WEAVE_NO_ERROR; }

WEAVE_ERROR FuchsiaConfig::ReadConfigValueStr(Key key, char* buf, size_t bufSize, size_t& outLen) {
  return WEAVE_NO_ERROR;
}

WEAVE_ERROR FuchsiaConfig::ReadConfigValueBin(Key key, uint8_t* buf, size_t bufSize,
                                              size_t& outLen) {
  return WEAVE_NO_ERROR;
}

WEAVE_ERROR FuchsiaConfig::WriteConfigValue(Key key, bool val) { return WEAVE_NO_ERROR; }

WEAVE_ERROR FuchsiaConfig::WriteConfigValue(Key key, uint32_t val) { return WEAVE_NO_ERROR; }

WEAVE_ERROR FuchsiaConfig::WriteConfigValue(Key key, uint64_t val) { return WEAVE_NO_ERROR; }

WEAVE_ERROR FuchsiaConfig::WriteConfigValueStr(Key key, const char* str) { return WEAVE_NO_ERROR; }

WEAVE_ERROR FuchsiaConfig::WriteConfigValueStr(Key key, const char* str, size_t strLen) {
  return WEAVE_NO_ERROR;
}

WEAVE_ERROR FuchsiaConfig::WriteConfigValueBin(Key key, const uint8_t* data, size_t dataLen) {
  return WEAVE_NO_ERROR;
}

WEAVE_ERROR FuchsiaConfig::ClearConfigValue(Key key) { return WEAVE_NO_ERROR; }

bool FuchsiaConfig::ConfigValueExists(Key key) { return true; }

WEAVE_ERROR FuchsiaConfig::FactoryResetConfig(void) { return WEAVE_NO_ERROR; }

}  // namespace Internal
}  // namespace DeviceLayer
}  // namespace Weave
}  // namespace nl
