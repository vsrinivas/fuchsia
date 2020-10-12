// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// clang-format off
#include <Weave/DeviceLayer/internal/WeaveDeviceLayerInternal.h>
#include "environment_config.h"
// clang-format on
#include "weave_config_manager.h"
#include "src/lib/files/file.h"

namespace nl {
namespace Weave {
namespace DeviceLayer {
namespace Internal {

// clang-format off
const EnvironmentConfig::Key EnvironmentConfig::kConfigKey_SerialNum = "serial-num";
const EnvironmentConfig::Key EnvironmentConfig::kConfigKey_MfrDeviceId = "mfr-device-id";
const EnvironmentConfig::Key EnvironmentConfig::kConfigKey_MfrDeviceCert = "mfr-device-cert";
const EnvironmentConfig::Key EnvironmentConfig::kConfigKey_MfrDeviceICACerts = "mfr-device-ica-certs";
const EnvironmentConfig::Key EnvironmentConfig::kConfigKey_MfrDevicePrivateKey = "mfr-device-private-key";
const EnvironmentConfig::Key EnvironmentConfig::kConfigKey_MfrDeviceCertAllowLocal = "mfr-device-cert-allow-local";
const EnvironmentConfig::Key EnvironmentConfig::kConfigKey_ProductRevision = "product-revision";
const EnvironmentConfig::Key EnvironmentConfig::kConfigKey_ManufacturingDate = "manufacturing-date";
const EnvironmentConfig::Key EnvironmentConfig::kConfigKey_PairingCode = "pairing-code";
const EnvironmentConfig::Key EnvironmentConfig::kConfigKey_FabricId = "fabric-id";
const EnvironmentConfig::Key EnvironmentConfig::kConfigKey_ServiceConfig = "service-config";
const EnvironmentConfig::Key EnvironmentConfig::kConfigKey_PairedAccountId = "paired-account-id";
const EnvironmentConfig::Key EnvironmentConfig::kConfigKey_ServiceId = "service-id";
const EnvironmentConfig::Key EnvironmentConfig::kConfigKey_FabricSecret = "fabric-secret";
const EnvironmentConfig::Key EnvironmentConfig::kConfigKey_GroupKeyIndex = "group-key-index";
const EnvironmentConfig::Key EnvironmentConfig::kConfigKey_LastUsedEpochKeyId = "last-used-epoch-key-id";
const EnvironmentConfig::Key EnvironmentConfig::kConfigKey_FailSafeArmed = "fail-safe-armed";
const EnvironmentConfig::Key EnvironmentConfig::kConfigKey_WiFiStationSecType = "wifi-station-sec-type";
const EnvironmentConfig::Key EnvironmentConfig::kConfigKey_OperationalDeviceId = "operational-device-id";
const EnvironmentConfig::Key EnvironmentConfig::kConfigKey_OperationalDeviceCert = "operational-device-cert";
const EnvironmentConfig::Key EnvironmentConfig::kConfigKey_OperationalDeviceICACerts = "operational-device-ica-certs";
const EnvironmentConfig::Key EnvironmentConfig::kConfigKey_OperationalDevicePrivateKey = "operational-device-private-key";
// clang-format on

namespace {

// Store path for default environment information. Keys in this file must match
// those in EnvironmentConfig and are used as the default environment state on a
// fresh boot or configuration reset. In general, this file should only be
// populated if explicit control over the initial variable state is required
// (e.g. for testing, or if the variable can be provided statically).
constexpr char kDefaultEnvironmentStorePath[] = "/config/data/default_environment.json";
// Accompanying schema file for the default environment store.
constexpr char kDefaultEnvironmentStoreSchemaPath[] = "/pkg/data/default_environment_schema.json";
}  // namespace

WEAVE_ERROR EnvironmentConfig::Init() {
  if (!files::IsFile(kDefaultEnvironmentStorePath) ||
      !files::IsFile(kDefaultEnvironmentStoreSchemaPath)) {
    return WEAVE_NO_ERROR;
  }
  return WeaveConfigMgr().SetDefaultConfiguration(kDefaultEnvironmentStorePath,
                                                  kDefaultEnvironmentStoreSchemaPath);
}

WEAVE_ERROR EnvironmentConfig::ReadConfigValue(Key key, bool& val) {
  return WeaveConfigMgr().ReadConfigValue(key, &val);
}

WEAVE_ERROR EnvironmentConfig::ReadConfigValue(Key key, uint32_t& val) {
  return WeaveConfigMgr().ReadConfigValue(key, &val);
}

WEAVE_ERROR EnvironmentConfig::ReadConfigValue(Key key, uint64_t& val) {
  return WeaveConfigMgr().ReadConfigValue(key, &val);
}

WEAVE_ERROR EnvironmentConfig::ReadConfigValueStr(Key key, char* buf, size_t bufSize,
                                                  size_t& outLen) {
  return WeaveConfigMgr().ReadConfigValueStr(key, buf, bufSize, &outLen);
}

WEAVE_ERROR EnvironmentConfig::ReadConfigValueBin(Key key, uint8_t* buf, size_t bufSize,
                                                  size_t& outLen) {
  return WeaveConfigMgr().ReadConfigValueBin(key, buf, bufSize, &outLen);
}

WEAVE_ERROR EnvironmentConfig::WriteConfigValue(Key key, bool val) {
  return WeaveConfigMgr().WriteConfigValue(key, val);
}

WEAVE_ERROR EnvironmentConfig::WriteConfigValue(Key key, uint32_t val) {
  return WeaveConfigMgr().WriteConfigValue(key, val);
}

WEAVE_ERROR EnvironmentConfig::WriteConfigValue(Key key, uint64_t val) {
  return WeaveConfigMgr().WriteConfigValue(key, val);
}

WEAVE_ERROR EnvironmentConfig::WriteConfigValueStr(Key key, const char* str) {
  return WriteConfigValueStr(key, str, strlen(str));
}

WEAVE_ERROR EnvironmentConfig::WriteConfigValueStr(Key key, const char* str, size_t strLen) {
  return WeaveConfigMgr().WriteConfigValueStr(key, str, strLen);
}

WEAVE_ERROR EnvironmentConfig::WriteConfigValueBin(Key key, const uint8_t* data, size_t dataLen) {
  return WeaveConfigMgr().WriteConfigValueBin(key, data, dataLen);
}

WEAVE_ERROR EnvironmentConfig::ClearConfigValue(Key key) {
  return WeaveConfigMgr().ClearConfigValue(key);
}

bool EnvironmentConfig::ConfigValueExists(Key key) {
  return WeaveConfigMgr().ConfigValueExists(key);
}

WEAVE_ERROR EnvironmentConfig::FactoryResetConfig(void) {
  return WeaveConfigMgr().FactoryResetConfig();
}

}  // namespace Internal
}  // namespace DeviceLayer
}  // namespace Weave
}  // namespace nl
