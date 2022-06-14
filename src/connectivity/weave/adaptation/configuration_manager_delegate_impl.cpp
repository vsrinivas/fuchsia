// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// clang-format off
#include <Weave/DeviceLayer/internal/WeaveDeviceLayerInternal.h>
#include <Weave/DeviceLayer/internal/GenericConfigurationManagerImpl.ipp>

#include "src/connectivity/weave/adaptation/configuration_manager_delegate_impl.h"
#include "src/connectivity/weave/adaptation/group_key_store_impl.h"
// clang-format on

#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fit/defer.h>
#include <lib/syslog/cpp/macros.h>
#include <net/ethernet.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zircon/status.h>

#include <filesystem>

#include "src/lib/files/file.h"
#include "src/lib/fsl/vmo/file.h"
#include "src/lib/fsl/vmo/strings.h"
#include "weave_inspector.h"

namespace nl::Weave::DeviceLayer {
namespace {

using nl::Weave::WeaveInspector;
using ::nl::Weave::DeviceLayer::Internal::EnvironmentConfig;
using ::nl::Weave::DeviceLayer::Internal::GenericConfigurationManagerImpl;
using ::nl::Weave::DeviceLayer::Internal::WeaveConfigManager;
using ::nl::Weave::Profiles::Security::AppKeys::GroupKeyStoreBase;

// Store path and keys for static device information.
constexpr char kDeviceInfoStorePath[] = "/config/data/device_info.json";
constexpr char kDeviceInfoRuntimePath[] = "/data/device_info.json";
constexpr char kDeviceInfoSchemaPath[] = "/pkg/data/device_info_schema.json";
constexpr char kDeviceInfoConfigKey_BleDeviceNamePrefix[] = "ble-device-name-prefix";
constexpr char kDeviceInfoConfigKey_DeviceId[] = "device-id";
constexpr char kDeviceInfoConfigKey_DeviceIdPath[] = "device-id-path";
constexpr char kDeviceInfoConfigKey_EnableThread[] = "enable-thread";
constexpr char kDeviceInfoConfigKey_EnableIpForwarding[] = "enable-ipv6-forwarding";
constexpr char kDeviceInfoConfigKey_EnableWoBLE[] = "enable-woble";
constexpr char kDeviceInfoConfigKey_EnableWoBLEAdvertisement[] = "enable-woble-advertisement";
constexpr char kDeviceInfoConfigKey_FirmwareRevision[] = "firmware-revision";
constexpr char kDeviceInfoConfigKey_MfrDeviceCertPath[] = "mfr-device-cert-path";
constexpr char kDeviceInfoConfigKey_MfrDeviceCertAllowLocal[] = "mfr-device-cert-allow-local";
constexpr char kDeviceInfoConfigKey_PrivateKeyPath[] = "mfr-private-key-path";
constexpr char kDeviceInfoConfigKey_ProductId[] = "product-id";
constexpr char kDeviceInfoConfigKey_ProductIdDescription[] = "product-id-description";
constexpr char kDeviceInfoConfigKey_SerialNumber[] = "serial-number";
constexpr char kDeviceInfoConfigKey_ThreadJoinableDurationSec[] = "thread-joinable-duration-sec";
constexpr char kDeviceInfoConfigKey_VendorId[] = "vendor-id";
constexpr char kDeviceInfoConfigKey_VendorIdDescription[] = "vendor-id-description";
constexpr char kDeviceInfoConfigKey_AppletPaths[] = "applet-paths";

// Maximum number of chars in hex for a uint64_t.
constexpr int kWeaveDeviceIdMaxLength = 16;

// Maximum size of Weave certificate.
constexpr int kWeaveCertificateMaxLength = UINT16_MAX;

// Maximum size of a YYYY-MM-DD date string.
constexpr size_t kMaxDateStringSize = 10;

// Storage path for data files.
const std::string kDataPath = "/data/";

// The required size of a buffer supplied to GetPrimaryWiFiMACAddress.
constexpr size_t kWiFiMacAddressBufSize =
    sizeof(Profiles::DeviceDescription::WeaveDeviceDescriptor::PrimaryWiFiMACAddress);
// Fake MAC address returned by GetPrimaryWiFiMACAddress
constexpr uint8_t kFakeMacAddress[kWiFiMacAddressBufSize] = {0xFF};

}  // unnamed namespace

ConfigurationManagerDelegateImpl::ConfigurationManagerDelegateImpl()
    : device_info_(WeaveConfigManager::CreateInstance(kDeviceInfoStorePath)) {}

WEAVE_ERROR ConfigurationManagerDelegateImpl::Init() {
  WEAVE_ERROR err = WEAVE_NO_ERROR;
  auto context = PlatformMgrImpl().GetComponentContextForProcess();
  bool fail_safe_armed = false;

  FX_CHECK(context->svc()->Connect(buildinfo_provider_.NewRequest()) == ZX_OK)
      << "Failed to connect to buildinfo device service.";
  FX_CHECK(context->svc()->Connect(hwinfo_device_.NewRequest()) == ZX_OK)
      << "Failed to connect to hwinfo device service.";
  FX_CHECK(context->svc()->Connect(hwinfo_product_.NewRequest()) == ZX_OK)
      << "Failed to connect to hwinfo product service.";
  FX_CHECK(context->svc()->Connect(weave_factory_data_manager_.NewRequest()) == ZX_OK)
      << "Failed to connect to weave factory data manager service.";
  FX_CHECK(context->svc()->Connect(factory_store_provider_.NewRequest()) == ZX_OK)
      << "Failed to connect to factory store";

  err = EnvironmentConfig::Init();
  if (err != WEAVE_NO_ERROR) {
    return err;
  }

  // If the fail-safe was armed when the device last shutdown or weavestack crashed,
  // erase weave data.
  if (GetFailSafeArmed(fail_safe_armed) == WEAVE_NO_ERROR && fail_safe_armed) {
    FX_LOGS(WARNING)
        << "Fail safe was not disarmed before weavestack was shutdown, erasing Weave data.";
    InitiateFactoryReset();
  }

  if (files::IsFile(kDeviceInfoRuntimePath)) {
    err = device_info_->SetConfiguration(kDeviceInfoRuntimePath, kDeviceInfoSchemaPath,
                                         /*should_replace*/ true);
    // The runtime device info is primarily used for testing and is applied as best-effort.
    // Failures are logged but proceed with the built-in configuration.
    if (err != WEAVE_NO_ERROR) {
      FX_LOGS(WARNING) << "Failed to apply runtime device info: " << ErrorStr(err);
    }
  }

  err = static_cast<GenericConfigurationManagerImpl<ConfigurationManagerImpl>*>(impl_)->_Init();
  if (err != WEAVE_NO_ERROR) {
    return err;
  }

  err = GetAndStoreSerialNumber();
  if (err != WEAVE_NO_ERROR && err != WEAVE_DEVICE_ERROR_CONFIG_NOT_FOUND) {
    return err;
  }

  err = GetAndStoreFirmwareRevision();
  if (err != WEAVE_NO_ERROR && err != WEAVE_DEVICE_ERROR_CONFIG_NOT_FOUND) {
    return err;
  }

  err = GetAndStorePairingCode();
  if (err != WEAVE_NO_ERROR && err != WEAVE_DEVICE_ERROR_CONFIG_NOT_FOUND) {
    return err;
  }

  err = GetAndStoreManufacturingDate();
  if (err != WEAVE_NO_ERROR && err != WEAVE_DEVICE_ERROR_CONFIG_NOT_FOUND) {
    return err;
  }

  return WEAVE_NO_ERROR;
}

WEAVE_ERROR ConfigurationManagerDelegateImpl::GetDeviceId(uint64_t& device_id) {
  WEAVE_ERROR err =
      EnvironmentConfig::ReadConfigValue(EnvironmentConfig::kConfigKey_MfrDeviceId, device_id);
  if (err == WEAVE_NO_ERROR) {
    return WEAVE_NO_ERROR;
  }

  err = device_info_->ReadConfigValue(kDeviceInfoConfigKey_DeviceId, &device_id);
  if (err != WEAVE_DEVICE_ERROR_CONFIG_NOT_FOUND) {
    return err;
  }

  char path[PATH_MAX] = {'\0'};
  size_t out_size;
  err = device_info_->ReadConfigValueStr(kDeviceInfoConfigKey_DeviceIdPath, path, sizeof(path),
                                         &out_size);
  if (err == WEAVE_NO_ERROR) {
    err = GetDeviceIdFromFactory(path, &device_id);
    if (err != WEAVE_NO_ERROR) {
      FX_LOGS(ERROR) << "Failed getting device id from factory at path: " << path;
      return err;
    }
    return impl_->StoreManufacturerDeviceId(device_id);
  }

  err = GetDeviceIdFromFactory(path, &device_id);
  if (err != WEAVE_NO_ERROR) {
    return err;
  }

  return impl_->StoreManufacturerDeviceId(device_id);
}

WEAVE_ERROR ConfigurationManagerDelegateImpl::GetManufacturerDeviceCertificate(uint8_t* buf,
                                                                               size_t buf_size,
                                                                               size_t& out_len) {
  WEAVE_ERROR err = EnvironmentConfig::ReadConfigValueBin(
      EnvironmentConfig::kConfigKey_MfrDeviceCert, buf, buf_size, out_len);
  if (err == WEAVE_NO_ERROR) {
    return err;
  }

  err = GetAndStoreMfrDeviceCert();
  if (err != WEAVE_NO_ERROR) {
    return err;
  }

  return EnvironmentConfig::ReadConfigValueBin(EnvironmentConfig::kConfigKey_MfrDeviceCert, buf,
                                               buf_size, out_len);
}

bool ConfigurationManagerDelegateImpl::IsFullyProvisioned() {
  return ConnectivityMgr().IsWiFiStationProvisioned() && ConfigurationMgr().IsPairedToAccount() &&
         ConfigurationMgr().IsMemberOfFabric() &&
         (!IsThreadEnabled() || ConnectivityMgr().IsThreadProvisioned());
}

bool ConfigurationManagerDelegateImpl::IsPairedToAccount() {
  // By default, just use the generic implementation in Weave Device Layer.
  return static_cast<GenericConfigurationManagerImpl<ConfigurationManagerImpl>*>(impl_)
      ->_IsPairedToAccount();
}

bool ConfigurationManagerDelegateImpl::IsMemberOfFabric() {
  // By default, just use the generic implementation in Weave Device Layer.
  return static_cast<GenericConfigurationManagerImpl<ConfigurationManagerImpl>*>(impl_)
      ->_IsMemberOfFabric();
}

GroupKeyStoreBase* ConfigurationManagerDelegateImpl::GetGroupKeyStore() {
  return &group_key_store_;
}

bool ConfigurationManagerDelegateImpl::CanFactoryReset() { return true; }

void ConfigurationManagerDelegateImpl::InitiateFactoryReset() {
  EnvironmentConfig::FactoryResetConfig();
  ThreadStackMgrImpl()._ClearThreadProvision();
}

WEAVE_ERROR ConfigurationManagerDelegateImpl::ReadPersistedStorageValue(Key key, uint32_t& value) {
  WEAVE_ERROR err = EnvironmentConfig::ReadConfigValue(key, value);
  return (err == WEAVE_DEVICE_ERROR_CONFIG_NOT_FOUND)
             ? WEAVE_ERROR_PERSISTED_STORAGE_VALUE_NOT_FOUND
             : err;
}

WEAVE_ERROR ConfigurationManagerDelegateImpl::WritePersistedStorageValue(Key key, uint32_t value) {
  WEAVE_ERROR err = EnvironmentConfig::WriteConfigValue(key, value);
  return (err != WEAVE_NO_ERROR) ? WEAVE_ERROR_PERSISTED_STORAGE_FAIL : err;
}

WEAVE_ERROR ConfigurationManagerDelegateImpl::GetVendorId(uint16_t& vendor_id) {
  return device_info_->ReadConfigValue(kDeviceInfoConfigKey_VendorId, &vendor_id);
}

WEAVE_ERROR ConfigurationManagerDelegateImpl::GetVendorIdDescription(char* buf, size_t buf_size,
                                                                     size_t& out_len) {
  return device_info_->ReadConfigValueStr(kDeviceInfoConfigKey_VendorIdDescription, buf, buf_size,
                                          &out_len);
}

WEAVE_ERROR ConfigurationManagerDelegateImpl::GetProductId(uint16_t& product_id) {
  return device_info_->ReadConfigValue(kDeviceInfoConfigKey_ProductId, &product_id);
}

WEAVE_ERROR ConfigurationManagerDelegateImpl::GetProductIdDescription(char* buf, size_t buf_size,
                                                                      size_t& out_len) {
  return device_info_->ReadConfigValueStr(kDeviceInfoConfigKey_ProductIdDescription, buf, buf_size,
                                          &out_len);
}

WEAVE_ERROR ConfigurationManagerDelegateImpl::GetFirmwareRevision(char* buf, size_t buf_size,
                                                                  size_t& out_len) {
  if (firmware_revision_.empty()) {
    return WEAVE_DEVICE_ERROR_CONFIG_NOT_FOUND;
  }

  if (buf_size < firmware_revision_.size()) {
    return WEAVE_ERROR_BUFFER_TOO_SMALL;
  }

  memcpy(buf, firmware_revision_.data(), firmware_revision_.size());
  out_len = firmware_revision_.size();
  return WEAVE_NO_ERROR;
}

WEAVE_ERROR ConfigurationManagerDelegateImpl::GetBleDeviceNamePrefix(char* device_name_prefix,
                                                                     size_t device_name_prefix_size,
                                                                     size_t* out_len) {
  return device_info_->ReadConfigValueStr(kDeviceInfoConfigKey_BleDeviceNamePrefix,
                                          device_name_prefix, device_name_prefix_size, out_len);
}

bool ConfigurationManagerDelegateImpl::IsThreadEnabled() {
  bool is_enabled = false;
  device_info_->ReadConfigValue(kDeviceInfoConfigKey_EnableThread, &is_enabled);
  return is_enabled;
}

bool ConfigurationManagerDelegateImpl::IsIPv6ForwardingEnabled() {
  bool is_enabled = false;
  device_info_->ReadConfigValue(kDeviceInfoConfigKey_EnableIpForwarding, &is_enabled);
  return is_enabled;
}

bool ConfigurationManagerDelegateImpl::IsWoBLEEnabled() {
  bool is_enabled = false;
  device_info_->ReadConfigValue(kDeviceInfoConfigKey_EnableWoBLE, &is_enabled);
  return is_enabled;
}

bool ConfigurationManagerDelegateImpl::IsWoBLEAdvertisementEnabled() {
  bool is_enabled = IsWoBLEEnabled();
  if (is_enabled) {
    device_info_->ReadConfigValue(kDeviceInfoConfigKey_EnableWoBLEAdvertisement, &is_enabled);
  }
  return is_enabled;
}

WEAVE_ERROR ConfigurationManagerDelegateImpl::GetDeviceDescriptorTLV(uint8_t* buf, size_t buf_size,
                                                                     size_t& encoded_len) {
  return static_cast<GenericConfigurationManagerImpl<ConfigurationManagerImpl>*>(impl_)
      ->_GetDeviceDescriptorTLV(buf, buf_size, encoded_len);
}

WEAVE_ERROR ConfigurationManagerDelegateImpl::GetAndStoreSerialNumber() {
  fuchsia::hwinfo::DeviceInfo device_info;
  WEAVE_ERROR err;
  char serial[ConfigurationManager::kMaxSerialNumberLength + 1];
  size_t serial_size = 0;
  if (ZX_OK == hwinfo_device_->GetInfo(&device_info) && device_info.has_serial_number()) {
    return impl_->StoreSerialNumber(device_info.serial_number().c_str(),
                                    device_info.serial_number().length());
  }
  if ((err = device_info_->ReadConfigValueStr(kDeviceInfoConfigKey_SerialNumber, serial,
                                              ConfigurationManager::kMaxSerialNumberLength + 1,
                                              &serial_size)) == WEAVE_NO_ERROR) {
    return impl_->StoreSerialNumber(serial, serial_size);
  }
  return WEAVE_DEVICE_ERROR_CONFIG_NOT_FOUND;
}

WEAVE_ERROR ConfigurationManagerDelegateImpl::GetAndStoreFirmwareRevision() {
  fuchsia::buildinfo::BuildInfo build_info;
  zx_status_t status = buildinfo_provider_->GetBuildInfo(&build_info);
  if (status == ZX_OK && !build_info.version().empty()) {
    firmware_revision_ = build_info.version();
    return WEAVE_NO_ERROR;
  }

  size_t firmware_revision_out = 0;
  firmware_revision_.resize(ConfigurationManager::kMaxFirmwareRevisionLength);
  WEAVE_ERROR err = device_info_->ReadConfigValueStr(
      kDeviceInfoConfigKey_FirmwareRevision, firmware_revision_.data(), firmware_revision_.size(),
      &firmware_revision_out);
  if (err != WEAVE_NO_ERROR) {
    return err;
  }

  firmware_revision_.resize(firmware_revision_out);
  return WEAVE_NO_ERROR;
}

WEAVE_ERROR ConfigurationManagerDelegateImpl::GetAndStorePairingCode() {
  fuchsia::weave::FactoryDataManager_GetPairingCode_Result pairing_code_result;
  fuchsia::weave::FactoryDataManager_GetPairingCode_Response pairing_code_response;
  std::string pairing_code;
  char read_value[ConfigurationManager::kMaxPairingCodeLength + 1];
  size_t read_value_size = 0;
  WEAVE_ERROR err;

  // If a pairing code is provided in config-data, it takes precedent over the
  // value provided by the factory data manager.
  err = device_info_->ReadConfigValueStr(EnvironmentConfig::kConfigKey_PairingCode, read_value,
                                         ConfigurationManager::kMaxPairingCodeLength + 1,
                                         &read_value_size);
  if (err == WEAVE_NO_ERROR) {
    return impl_->StorePairingCode(read_value, read_value_size);
  }

  // Otherwise, use the factory data manager as the source of truth.
  zx_status_t status = weave_factory_data_manager_->GetPairingCode(&pairing_code_result);
  if (ZX_OK != status || !pairing_code_result.is_response()) {
    return WEAVE_DEVICE_ERROR_CONFIG_NOT_FOUND;
  }

  pairing_code_response = pairing_code_result.response();
  if (pairing_code_response.pairing_code.size() > ConfigurationManager::kMaxPairingCodeLength) {
    return WEAVE_DEVICE_ERROR_CONFIG_NOT_FOUND;
  }

  return impl_->StorePairingCode(
      reinterpret_cast<const char*>(pairing_code_response.pairing_code.data()),
      pairing_code_response.pairing_code.size());
}

WEAVE_ERROR ConfigurationManagerDelegateImpl::GetAndStoreMfrDeviceCert() {
  char path[PATH_MAX] = {'\0'};
  char mfr_cert[kWeaveCertificateMaxLength];
  std::string cert;
  size_t out_size;
  zx_status_t status;
  WEAVE_ERROR err;
  bool allow_local = false;
  std::filesystem::path full_path(kDataPath);

  // Check if a test cert was provided and read the same.
  err = EnvironmentConfig::ReadConfigValue(EnvironmentConfig::kConfigKey_MfrDeviceCertAllowLocal,
                                           allow_local);
  if (err != WEAVE_NO_ERROR) {
    device_info_->ReadConfigValue(kDeviceInfoConfigKey_MfrDeviceCertAllowLocal, &allow_local);
  }
  if (allow_local) {
    err = device_info_->ReadConfigValueStr(kDeviceInfoConfigKey_MfrDeviceCertPath, path,
                                           sizeof(path), &out_size);
    if (err != WEAVE_NO_ERROR) {
      FX_LOGS(ERROR) << "Local manufacturer cert path not found";
      return err;
    }

    full_path.append(path);
    if (!files::ReadFileToString(full_path.string(), &cert)) {
      FX_LOGS(ERROR) << "failed reading " << path;
      return ZX_ERR_INTERNAL;
    }

    return impl_->StoreManufacturerDeviceCertificate(reinterpret_cast<uint8_t*>(cert.data()),
                                                     cert.size());
  }

  // Read file from factory.
  err = device_info_->ReadConfigValueStr(kDeviceInfoConfigKey_MfrDeviceCertPath, path, sizeof(path),
                                         &out_size);

  if (err != WEAVE_NO_ERROR) {
    FX_LOGS(ERROR) << "No manufacturer device certificate was found";
    return err;
  }

  status = ReadFactoryFile(path, mfr_cert, sizeof(mfr_cert), &out_size);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed getting manufacturer certificate from factory with status "
                   << zx_status_get_string(status) << " for path: " << path;
    return WEAVE_DEVICE_ERROR_CONFIG_NOT_FOUND;
  }

  return impl_->StoreManufacturerDeviceCertificate(reinterpret_cast<uint8_t*>(mfr_cert), out_size);
}

WEAVE_ERROR ConfigurationManagerDelegateImpl::GetAndStoreManufacturingDate() {
  fuchsia::hwinfo::ProductInfo product_info;
  zx_status_t status = hwinfo_product_->GetInfo(&product_info);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to retrieve product hwinfo.";
    return WEAVE_DEVICE_ERROR_CONFIG_NOT_FOUND;
  }

  if (!product_info.has_build_date()) {
    FX_LOGS(WARNING) << "Manufacturing date not supplied.";
    return WEAVE_DEVICE_ERROR_CONFIG_NOT_FOUND;
  }

  size_t date_size = std::min(product_info.build_date().size(), kMaxDateStringSize);
  return impl_->StoreManufacturingDate(product_info.build_date().data(), date_size);
}

zx_status_t ConfigurationManagerDelegateImpl::GetPrivateKeyForSigning(
    std::vector<uint8_t>* signing_key) {
  char path[PATH_MAX] = {'\0'};
  std::filesystem::path full_path(kDataPath);
  WEAVE_ERROR err;
  size_t out_len;

  err = device_info_->ReadConfigValueStr(kDeviceInfoConfigKey_PrivateKeyPath, path, sizeof(path),
                                         &out_len);
  if (err == WEAVE_DEVICE_ERROR_CONFIG_NOT_FOUND) {
    return ZX_ERR_NOT_FOUND;
  }
  if (err != WEAVE_NO_ERROR) {
    FX_LOGS(ERROR) << "Failed to read private key.";
    return ZX_ERR_INTERNAL;
  }

  full_path.append(path);
  if (!files::ReadFileToVector(full_path.string(), signing_key)) {
    return ZX_ERR_INTERNAL;
  }
  return ZX_OK;
}

zx_status_t ConfigurationManagerDelegateImpl::ReadFactoryFile(const char* path, char* buf,
                                                              size_t buf_size, size_t* out_len) {
  fuchsia::io::DirectorySyncPtr factory_directory;
  zx_status_t status;
  struct stat statbuf;
  int dir_fd;

  // Open the factory store directory as a file descriptor.
  status = factory_store_provider_->GetFactoryStore(factory_directory.NewRequest());
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to get factory store: " << zx_status_get_string(status);
    return status;
  }
  status = fdio_fd_create(factory_directory.Unbind().TakeChannel().release(), &dir_fd);
  if (status != ZX_OK || dir_fd < 0) {
    FX_LOGS(ERROR) << "Failed to open factory store: " << zx_status_get_string(status);
    return status;
  }

  auto close_dir_defer = fit::defer([&] { close(dir_fd); });

  // Grab the fd of the corresponding file path and validate.
  int fd = openat(dir_fd, path, O_RDONLY);
  if (fd < 0) {
    FX_LOGS(ERROR) << "Failed to open " << path << ": " << strerror(errno);
    return ZX_ERR_IO;
  }

  auto close_fd_defer = fit::defer([&] { close(fd); });

  // Check the size of the file.
  if (fstat(fd, &statbuf) < 0) {
    FX_LOGS(ERROR) << "Could not stat file: " << path << ": " << strerror(errno);
    return ZX_ERR_IO;
  }
  size_t file_size = static_cast<size_t>(statbuf.st_size);
  if (file_size > buf_size) {
    FX_LOGS(ERROR) << "File too large for buffer: File size = " << file_size
                   << ", buffer size = " << buf_size;
    return ZX_ERR_BUFFER_TOO_SMALL;
  }

  // Read up to buf_size bytes into buf.
  size_t total_read = 0;
  ssize_t current_read = 0;
  while ((current_read = read(fd, buf + total_read, buf_size - total_read)) > 0) {
    total_read += current_read;
  }

  // Store the total size read.
  if (out_len) {
    *out_len = total_read;
  }

  // Confirm that the last read was successful.
  if (current_read < 0) {
    FX_LOGS(ERROR) << "Failed to read from file: " << strerror(errno);
    status = ZX_ERR_IO;
  }

  return status;
}

zx_status_t ConfigurationManagerDelegateImpl::GetDeviceIdFromFactory(const char* path,
                                                                     uint64_t* factory_device_id) {
  zx_status_t status;
  char output[kWeaveDeviceIdMaxLength + 1] = {'\0'};

  status = ReadFactoryFile(path, output, kWeaveDeviceIdMaxLength, nullptr);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to read " << path << ": " << zx_status_get_string(status);
    return status;
  }

  if (output[0] == '\0') {
    FX_LOGS(ERROR) << "File containing device ID was empty.";
    return ZX_ERR_IO;
  }

  *factory_device_id = strtoull(output, nullptr, 16);
  if (errno == ERANGE) {
    FX_LOGS(ERROR) << "Failed to strtoull device ID: " << strerror(errno);
    return ZX_ERR_IO;
  }

  return ZX_OK;
}

zx_status_t ConfigurationManagerDelegateImpl::GetAppletPathList(std::vector<std::string>& out) {
  return device_info_->ReadConfigValueArray(kDeviceInfoConfigKey_AppletPaths, out);
}

WEAVE_ERROR ConfigurationManagerDelegateImpl::GetPrimaryWiFiMACAddress(uint8_t* mac_address) {
  // This is setting the MAC address to FF:0:0:0:0:0; this is for a few reasons:
  //   1. The actual value of the MAC address in the descriptor is not currently used.
  //   2. The MAC address is PII, so it should not be transmitted unless necessary.
  //   3. Some value should still be transmitted as some tools or other devices use the presence of
  //      an WiFi MAC address to determine if WiFi is supported.
  // The best way to meet these requirements is to provide a faked-out MAC address instead.
  std::memcpy(mac_address, kFakeMacAddress, kWiFiMacAddressBufSize);
  return WEAVE_NO_ERROR;
}

WEAVE_ERROR ConfigurationManagerDelegateImpl::GetThreadJoinableDuration(uint32_t* duration) {
  return device_info_->ReadConfigValue(kDeviceInfoConfigKey_ThreadJoinableDurationSec, duration);
}

WEAVE_ERROR ConfigurationManagerDelegateImpl::GetFailSafeArmed(bool& fail_safe_armed) {
  return static_cast<GenericConfigurationManagerImpl<ConfigurationManagerImpl>*>(impl_)
      ->_GetFailSafeArmed(fail_safe_armed);
}

WEAVE_ERROR ConfigurationManagerDelegateImpl::SetFailSafeArmed(bool fail_safe_armed) {
  WEAVE_ERROR err = static_cast<GenericConfigurationManagerImpl<ConfigurationManagerImpl>*>(impl_)
                        ->_SetFailSafeArmed(fail_safe_armed);
  std::string inspect_reason = WeaveInspector::kFailSafeReason_Nominal;
  std::string inspect_status = fail_safe_armed ? WeaveInspector::kFailSafeState_Armed
                                               : WeaveInspector::kFailSafeState_Disarmed;
  if (err != WEAVE_NO_ERROR) {
    inspect_reason = fail_safe_armed ? WeaveInspector::kFailSafeReason_FailsafeArmFailed
                                     : WeaveInspector::kFailSafeReason_FailsafeDisarmFailed;
    inspect_status = fail_safe_armed ? WeaveInspector::kFailSafeState_Disarmed
                                     : WeaveInspector::kFailSafeState_Armed;
  }

  auto& inspector = WeaveInspector::GetWeaveInspector();
  inspector.NotifyFailSafeStateChange(inspect_status, inspect_reason);

  return err;
}

WEAVE_ERROR ConfigurationManagerDelegateImpl::StoreFabricId(uint64_t fabric_id) {
  WEAVE_ERROR err = static_cast<GenericConfigurationManagerImpl<ConfigurationManagerImpl>*>(impl_)
                        ->_StoreFabricId(fabric_id);
  if (err != WEAVE_NO_ERROR) {
    return err;
  }

  auto& inspector = WeaveInspector::GetWeaveInspector();
  if (fabric_id != kFabricIdNotSpecified) {
    inspector.NotifyPairingStateChange(WeaveInspector::kPairingState_FabricCreatedOrJoined);
    return err;
  }
  inspector.NotifyPairingStateChange(WeaveInspector::kPairingState_LeftFabric);
  return err;
}

WEAVE_ERROR ConfigurationManagerDelegateImpl::StoreServiceProvisioningData(
    uint64_t service_id, const uint8_t* service_config, size_t service_config_len,
    const char* account_id, size_t account_id_len) {
  WEAVE_ERROR err =
      static_cast<GenericConfigurationManagerImpl<ConfigurationManagerImpl>*>(impl_)
          ->_StoreServiceProvisioningData(service_id, service_config, service_config_len,
                                          account_id, account_id_len);
  if (err != WEAVE_NO_ERROR) {
    return err;
  }

  auto& inspector = WeaveInspector::GetWeaveInspector();
  inspector.NotifyPairingStateChange(WeaveInspector::kPairingState_RegisterServicePending);
  return err;
}

WEAVE_ERROR ConfigurationManagerDelegateImpl::StoreServiceConfig(const uint8_t* service_config,
                                                                 size_t service_config_len) {
  WEAVE_ERROR err = static_cast<GenericConfigurationManagerImpl<ConfigurationManagerImpl>*>(impl_)
                        ->_StoreServiceConfig(service_config, service_config_len);
  if (err != WEAVE_NO_ERROR) {
    return err;
  }

  auto& inspector = WeaveInspector::GetWeaveInspector();
  inspector.NotifyPairingStateChange(WeaveInspector::kPairingState_ServiceConfigUpdated);
  return err;
}

WEAVE_ERROR ConfigurationManagerDelegateImpl::StorePairedAccountId(const char* account_id,
                                                                   size_t account_id_len) {
  WEAVE_ERROR err = static_cast<GenericConfigurationManagerImpl<ConfigurationManagerImpl>*>(impl_)
                        ->_StorePairedAccountId(account_id, account_id_len);
  if (err != WEAVE_NO_ERROR) {
    return err;
  }

  auto& inspector = WeaveInspector::GetWeaveInspector();
  inspector.NotifyPairingStateChange(WeaveInspector::kPairingState_RegisterServiceCompleted);
  return err;
}

}  // namespace nl::Weave::DeviceLayer
