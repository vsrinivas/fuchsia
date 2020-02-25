// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// clang-format off
#include <Weave/DeviceLayer/internal/WeaveDeviceLayerInternal.h>
#include "fuchsia_config.h"
// clang-format on
#include "weave_config_manager.h"

namespace nl {
namespace Weave {
namespace DeviceLayer {
namespace Internal {

// clang-format off
const FuchsiaConfig::Key FuchsiaConfig::kConfigKey_SerialNum = "serial-num";
const FuchsiaConfig::Key FuchsiaConfig::kConfigKey_MfrDeviceId = "mfr-device-id";
const FuchsiaConfig::Key FuchsiaConfig::kConfigKey_MfrDeviceCert = "mfr-device-cert";
const FuchsiaConfig::Key FuchsiaConfig::kConfigKey_MfrDeviceICACerts = "mfr-device-ica-certs";
const FuchsiaConfig::Key FuchsiaConfig::kConfigKey_MfrDevicePrivateKey = "mfr-device-private-key";
const FuchsiaConfig::Key FuchsiaConfig::kConfigKey_ProductRevision = "product-revision";
const FuchsiaConfig::Key FuchsiaConfig::kConfigKey_ManufacturingDate = "manufacturing-date";
const FuchsiaConfig::Key FuchsiaConfig::kConfigKey_PairingCode = "pairing-code";
const FuchsiaConfig::Key FuchsiaConfig::kConfigKey_FabricId = "fabric-id";
const FuchsiaConfig::Key FuchsiaConfig::kConfigKey_ServiceConfig = "service-config";
const FuchsiaConfig::Key FuchsiaConfig::kConfigKey_PairedAccountId = "paired-account-id";
const FuchsiaConfig::Key FuchsiaConfig::kConfigKey_ServiceId = "service-id";
const FuchsiaConfig::Key FuchsiaConfig::kConfigKey_FabricSecret = "fabric-secret";
const FuchsiaConfig::Key FuchsiaConfig::kConfigKey_GroupKeyIndex = "group-key-index";
const FuchsiaConfig::Key FuchsiaConfig::kConfigKey_LastUsedEpochKeyId = "last-used-epoch-key-id";
const FuchsiaConfig::Key FuchsiaConfig::kConfigKey_FailSafeArmed = "fail-safe-armed";
const FuchsiaConfig::Key FuchsiaConfig::kConfigKey_WiFiStationSecType = "wifi-station-sec-type";
const FuchsiaConfig::Key FuchsiaConfig::kConfigKey_OperationalDeviceId = "operational-device-id";
const FuchsiaConfig::Key FuchsiaConfig::kConfigKey_OperationalDeviceCert = "operational-device-cert";
const FuchsiaConfig::Key FuchsiaConfig::kConfigKey_OperationalDeviceICACerts = "operational-device-ica-certs";
const FuchsiaConfig::Key FuchsiaConfig::kConfigKey_OperationalDevicePrivateKey = "operational-device-private-key";
// clang-format on

WEAVE_ERROR FuchsiaConfig::Init() { return WEAVE_NO_ERROR; }

WEAVE_ERROR FuchsiaConfig::ReadConfigValue(Key key, bool& val) {
  return WeaveConfigMgr().ReadConfigValue(key, &val);
}

WEAVE_ERROR FuchsiaConfig::ReadConfigValue(Key key, uint32_t& val) {
  return WeaveConfigMgr().ReadConfigValue(key, &val);
}

WEAVE_ERROR FuchsiaConfig::ReadConfigValue(Key key, uint64_t& val) {
  return WeaveConfigMgr().ReadConfigValue(key, &val);
}

WEAVE_ERROR FuchsiaConfig::ReadConfigValueStr(Key key, char* buf, size_t bufSize, size_t& outLen) {
  return WeaveConfigMgr().ReadConfigValueStr(key, buf, bufSize, &outLen);
}

WEAVE_ERROR FuchsiaConfig::ReadConfigValueBin(Key key, uint8_t* buf, size_t bufSize,
                                              size_t& outLen) {
  return WeaveConfigMgr().ReadConfigValueBin(key, buf, bufSize, &outLen);
}

WEAVE_ERROR FuchsiaConfig::WriteConfigValue(Key key, bool val) {
  return WeaveConfigMgr().WriteConfigValue(key, val);
}

WEAVE_ERROR FuchsiaConfig::WriteConfigValue(Key key, uint32_t val) {
  return WeaveConfigMgr().WriteConfigValue(key, val);
}

WEAVE_ERROR FuchsiaConfig::WriteConfigValue(Key key, uint64_t val) {
  return WeaveConfigMgr().WriteConfigValue(key, val);
}

WEAVE_ERROR FuchsiaConfig::WriteConfigValueStr(Key key, const char* str) {
  return WriteConfigValueStr(key, str, strlen(str));
}

WEAVE_ERROR FuchsiaConfig::WriteConfigValueStr(Key key, const char* str, size_t strLen) {
  return WeaveConfigMgr().WriteConfigValueStr(key, str, strLen);
}

WEAVE_ERROR FuchsiaConfig::WriteConfigValueBin(Key key, const uint8_t* data, size_t dataLen) {
  return WeaveConfigMgr().WriteConfigValueBin(key, data, dataLen);
}

WEAVE_ERROR FuchsiaConfig::ClearConfigValue(Key key) {
  return WeaveConfigMgr().ClearConfigValue(key);
}

bool FuchsiaConfig::ConfigValueExists(Key key) { return WeaveConfigMgr().ConfigValueExists(key); }

WEAVE_ERROR FuchsiaConfig::FactoryResetConfig(void) {
  return WeaveConfigMgr().FactoryResetConfig();
}

}  // namespace Internal
}  // namespace DeviceLayer
}  // namespace Weave
}  // namespace nl
