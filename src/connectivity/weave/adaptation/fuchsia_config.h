// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_CONNECTIVITY_WEAVE_ADAPTATION_FUCHSIA_CONFIG_H_
#define SRC_CONNECTIVITY_WEAVE_ADAPTATION_FUCHSIA_CONFIG_H_

#include <Weave/DeviceLayer/internal/WeaveDeviceLayerInternal.h>

namespace nl {
namespace Weave {
namespace DeviceLayer {
namespace Internal {

/**
 * Provides functions and definitions for accessing persisted device configuration
 * on platforms based on the Fuchsia SDK.
 *
 * NOTE: This class is designed to be mixed-in to the concrete subclass of the
 * GenericConfigurationManagerImpl<> template.  When used this way, the class
 * naturally provides implementations for the delegated members referenced by
 * the template class (e.g. the ReadConfigValue() method).
 */
class FuchsiaConfig {
 public:
  using Key = uint32_t;
  static const Key kConfigKey_SerialNum = 0;
  static const Key kConfigKey_MfrDeviceId = 0;
  static const Key kConfigKey_MfrDeviceCert = 0;
  static const Key kConfigKey_MfrDeviceICACerts = 0;
  static const Key kConfigKey_MfrDevicePrivateKey = 0;
  static const Key kConfigKey_ProductRevision = 0;
  static const Key kConfigKey_ManufacturingDate = 0;
  static const Key kConfigKey_PairingCode = 0;
  static const Key kConfigKey_FabricId = 0;
  static const Key kConfigKey_ServiceConfig = 0;
  static const Key kConfigKey_PairedAccountId = 0;
  static const Key kConfigKey_ServiceId = 0;
  static const Key kConfigKey_FabricSecret = 0;
  static const Key kConfigKey_GroupKeyIndex = 0;
  static const Key kConfigKey_LastUsedEpochKeyId = 0;
  static const Key kConfigKey_FailSafeArmed = 0;
  static const Key kConfigKey_WiFiStationSecType = 0;
  static const Key kConfigKey_OperationalDeviceId = 0;
  static const Key kConfigKey_OperationalDeviceCert = 0;
  static const Key kConfigKey_OperationalDeviceICACerts = 0;
  static const Key kConfigKey_OperationalDevicePrivateKey = 0;

  static WEAVE_ERROR Init(void);

  // Configuration methods used by the GenericConfigurationManagerImpl<> template.
  static WEAVE_ERROR ReadConfigValue(Key key, bool& val);
  static WEAVE_ERROR ReadConfigValue(Key key, uint32_t& val);
  static WEAVE_ERROR ReadConfigValue(Key key, uint64_t& val);
  static WEAVE_ERROR ReadConfigValueStr(Key key, char* buf, size_t bufSize, size_t& outLen);
  static WEAVE_ERROR ReadConfigValueBin(Key key, uint8_t* buf, size_t bufSize, size_t& outLen);
  static WEAVE_ERROR WriteConfigValue(Key key, bool val);
  static WEAVE_ERROR WriteConfigValue(Key key, uint32_t val);
  static WEAVE_ERROR WriteConfigValue(Key key, uint64_t val);
  static WEAVE_ERROR WriteConfigValueStr(Key key, const char* str);
  static WEAVE_ERROR WriteConfigValueStr(Key key, const char* str, size_t strLen);
  static WEAVE_ERROR WriteConfigValueBin(Key key, const uint8_t* data, size_t dataLen);
  static WEAVE_ERROR ClearConfigValue(Key key);
  static bool ConfigValueExists(Key key);
  static WEAVE_ERROR FactoryResetConfig(void);
};

}  // namespace Internal
}  // namespace DeviceLayer
}  // namespace Weave
}  // namespace nl

#endif  // SRC_CONNECTIVITY_WEAVE_ADAPTATION_FUCHSIA_CONFIG_H_
