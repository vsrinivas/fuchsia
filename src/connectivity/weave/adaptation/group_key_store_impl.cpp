// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// clang-format off
#include <Weave/DeviceLayer/internal/WeaveDeviceLayerInternal.h>
#include "group_key_store_impl.h"
// clang-format on
#include "src/lib/fxl/logging.h"

using namespace ::nl;
using namespace ::nl::Weave;
using namespace ::nl::Weave::Profiles::Security::AppKeys;

namespace nl {
namespace Weave {
namespace DeviceLayer {
namespace Internal {
namespace {
constexpr char kGroupKeyNamePrefix[] = "gk-";
constexpr size_t kGroupKeyNamePrefixSize = sizeof(kGroupKeyNamePrefix);
constexpr size_t kGroupKeyNameSuffixSize = 8;  // length of uint32_t hex.
constexpr size_t kGroupKeyNameFabricSize = sizeof(FuchsiaConfig::kConfigKey_FabricSecret);
constexpr size_t kGroupKeyNameMaxSize =
    kGroupKeyNamePrefixSize + kGroupKeyNameSuffixSize + kGroupKeyNameFabricSize;
}  // namespace

WEAVE_ERROR GroupKeyStoreImpl::Init() {
  WEAVE_ERROR error = WEAVE_NO_ERROR;
  uint32_t key_index_raw[kMaxGroupKeys];
  size_t index_size;

  key_index_.clear();
  error =
      FuchsiaConfig::ReadConfigValueBin(FuchsiaConfig::kConfigKey_GroupKeyIndex,
                                        (uint8_t*)key_index_raw, sizeof(key_index_raw), index_size);
  if (error == WEAVE_DEVICE_ERROR_CONFIG_NOT_FOUND) {
    index_size = 0;
    return WEAVE_NO_ERROR;
  } else if (error != WEAVE_NO_ERROR) {
    return error;
  } else if (index_size % sizeof(uint32_t) != 0) {
    return WEAVE_ERROR_DATA_NOT_ALIGNED;
  }

  // Insert keys into vector for easier processing and enforce consistency
  // between vector and config store.
  size_t key_count = index_size / sizeof(uint32_t);
  key_index_.insert(key_index_.begin(), std::begin(key_index_raw),
                    std::begin(key_index_raw) + key_count);
  for (auto& key_id : key_index_) {
    char formed_key_name[kGroupKeyNameMaxSize + 1];
    FormKeyName(key_id, formed_key_name, sizeof(formed_key_name));
    if (!FuchsiaConfig::ConfigValueExists(formed_key_name)) {
      error = RemoveKeyFromIndex(key_id);
      if (error != WEAVE_NO_ERROR) {
        return error;
      }
    }
  }
  return StoreKeyIndex();
}

WEAVE_ERROR GroupKeyStoreImpl::RetrieveGroupKey(uint32_t key_id, WeaveGroupKey& key) {
  WEAVE_ERROR error = WEAVE_NO_ERROR;
  char formed_key_name[kGroupKeyNameMaxSize + 1];
  FormKeyName(key_id, formed_key_name, sizeof(formed_key_name));

  size_t key_len = 0;
  memset(key.Key, 0, sizeof(key.Key));
  error = FuchsiaConfig::ReadConfigValueBin(formed_key_name, key.Key, sizeof(key.Key), key_len);
  if (error != WEAVE_NO_ERROR) {
    return error;
  }

  if (key_id != WeaveKeyId::kFabricSecret) {
    memcpy(&key.StartTime, key.Key + kWeaveAppGroupKeySize, sizeof(uint32_t));
    memset(key.Key + kWeaveAppGroupKeySize, 0, sizeof(key.Key) - kWeaveAppGroupKeySize);
    key_len -= sizeof(uint32_t);
  }

  key.KeyId = key_id;
  key.KeyLen = key_len;
  return WEAVE_NO_ERROR;
}

WEAVE_ERROR GroupKeyStoreImpl::StoreGroupKey(const WeaveGroupKey& key) {
  WEAVE_ERROR error = WEAVE_NO_ERROR;
  uint8_t key_data[WeaveGroupKey::MaxKeySize];
  uint32_t key_data_len = key.KeyLen;
  char formed_key_name[kGroupKeyNameMaxSize + 1];
  FormKeyName(key.KeyId, formed_key_name, sizeof(formed_key_name));

  memset(key_data, 0, sizeof(key_data));
  memcpy(key_data, key.Key, key.KeyLen);
  if (key.KeyId != WeaveKeyId::kFabricSecret) {
    memcpy(key_data + kWeaveAppGroupKeySize, (const void*)&key.StartTime, sizeof(uint32_t));
    key_data_len += sizeof(uint32_t);
  }

  if ((error = AddKeyToIndex(key.KeyId)) != WEAVE_NO_ERROR) {
    return error;
  }
  if ((error = StoreKeyIndex()) != WEAVE_NO_ERROR) {
    return error;
  }

  error = FuchsiaConfig::WriteConfigValueBin(formed_key_name, key_data, key_data_len);
  // Regardless of any errors, zero-out the copied key material.
  memset(key_data, 0, sizeof(key_data));
  if (error != WEAVE_NO_ERROR) {
    // If writing the key material fails, attempt to remove the key, but forward
    // the original error. The destructor will resolve the inconsistent state.
    FXL_DLOG(ERROR) << "Failed to write key: " << ErrorStr(error);
    RemoveKeyFromIndex(key.KeyId);
    StoreKeyIndex();
    return error;
  }
  return WEAVE_NO_ERROR;
}

WEAVE_ERROR GroupKeyStoreImpl::DeleteGroupKey(uint32_t key_id) {
  return DeleteGroupKey(key_id, true);
}

WEAVE_ERROR GroupKeyStoreImpl::DeleteGroupKey(uint32_t key_id, bool update_index) {
  WEAVE_ERROR error = WEAVE_NO_ERROR;
  char formed_key_name[kGroupKeyNameMaxSize + 1];
  FormKeyName(key_id, formed_key_name, sizeof(formed_key_name));

  if ((error = FuchsiaConfig::ClearConfigValue(formed_key_name)) != WEAVE_NO_ERROR) {
    return error;
  }
  if (update_index) {
    if ((error = RemoveKeyFromIndex(key_id)) != WEAVE_NO_ERROR) {
      return error;
    }
    if ((error = StoreKeyIndex()) != WEAVE_NO_ERROR) {
      return error;
    }
  }
  return WEAVE_NO_ERROR;
}

WEAVE_ERROR GroupKeyStoreImpl::DeleteGroupKeysOfAType(uint32_t key_type) {
  for (auto it = key_index_.begin(); it != key_index_.end();) {
    uint32_t key_id = *it;
    if (key_type == WeaveKeyId::kNone || key_type == WeaveKeyId::GetType(key_id)) {
      WEAVE_ERROR error = DeleteGroupKey(key_id, false);
      if (error != WEAVE_NO_ERROR) {
        return error;
      }
      it = key_index_.erase(it);
    } else {
      it++;
    }
  }
  return StoreKeyIndex();
}

WEAVE_ERROR GroupKeyStoreImpl::EnumerateGroupKeys(uint32_t key_type, uint32_t* key_ids,
                                                  uint8_t key_ids_array_size, uint8_t& key_count) {
  key_count = 0;
  for (auto& key_id : key_index_) {
    if (key_type == WeaveKeyId::kNone || key_type == WeaveKeyId::GetType(key_id)) {
      if (key_count >= key_ids_array_size) {
        return WEAVE_ERROR_BUFFER_TOO_SMALL;
      }
      key_ids[key_count++] = key_id;
    }
  }
  return WEAVE_NO_ERROR;
}

WEAVE_ERROR GroupKeyStoreImpl::Clear(void) { return DeleteGroupKeysOfAType(WeaveKeyId::kNone); }

WEAVE_ERROR GroupKeyStoreImpl::RetrieveLastUsedEpochKeyId(void) {
  WEAVE_ERROR error = FuchsiaConfig::ReadConfigValue(FuchsiaConfig::kConfigKey_LastUsedEpochKeyId,
                                                     LastUsedEpochKeyId);
  if (error == WEAVE_DEVICE_ERROR_CONFIG_NOT_FOUND) {
    LastUsedEpochKeyId = WeaveKeyId::kNone;
    return WEAVE_NO_ERROR;
  }
  return error;
}

WEAVE_ERROR GroupKeyStoreImpl::StoreLastUsedEpochKeyId(void) {
  return FuchsiaConfig::WriteConfigValue(FuchsiaConfig::kConfigKey_LastUsedEpochKeyId,
                                         LastUsedEpochKeyId);
}

WEAVE_ERROR GroupKeyStoreImpl::FormKeyName(uint32_t key_id, char* key_name, size_t key_name_size) {
  if (key_name_size < kGroupKeyNameMaxSize) {
    return WEAVE_ERROR_BUFFER_TOO_SMALL;
  }
  if (key_id == WeaveKeyId::kFabricSecret) {
    strcpy(key_name, FuchsiaConfig::kConfigKey_FabricSecret);
  } else {
    snprintf(key_name, key_name_size, "%s%08" PRIX32, kGroupKeyNamePrefix, key_id);
  }
  return WEAVE_NO_ERROR;
}

WEAVE_ERROR GroupKeyStoreImpl::AddKeyToIndex(uint32_t key_id) {
  if (key_index_.size() == kMaxGroupKeys) {
    return WEAVE_ERROR_TOO_MANY_KEYS;
  }
  key_index_.push_back(key_id);
  return WEAVE_NO_ERROR;
}

WEAVE_ERROR GroupKeyStoreImpl::RemoveKeyFromIndex(uint32_t key_id) {
  auto it = std::find(key_index_.begin(), key_index_.end(), key_id);
  if (it == key_index_.end()) {
    return WEAVE_ERROR_KEY_NOT_FOUND;
  }
  key_index_.erase(it);
  return WEAVE_NO_ERROR;
}

WEAVE_ERROR GroupKeyStoreImpl::StoreKeyIndex(void) {
  return FuchsiaConfig::WriteConfigValueBin(FuchsiaConfig::kConfigKey_GroupKeyIndex,
                                            (uint8_t*)(key_index_.data()),
                                            sizeof(uint32_t) * key_index_.size());
}

}  // namespace Internal
}  // namespace DeviceLayer
}  // namespace Weave
}  // namespace nl
