// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WEAVE_ADAPTATION_GROUP_KEY_STORE_IMPL_H_
#define SRC_CONNECTIVITY_WEAVE_ADAPTATION_GROUP_KEY_STORE_IMPL_H_

#include <Weave/Core/WeaveKeyIds.h>
#include <Weave/DeviceLayer/internal/WeaveDeviceLayerInternal.h>
#include <Weave/Profiles/security/WeaveApplicationKeys.h>

#include "fuchsia_config.h"

namespace nl {
namespace Weave {
namespace DeviceLayer {
namespace Internal {

/**
 * An implementation of the Weave GroupKeyStoreBase API for the Fuchsia.
 */
class GroupKeyStoreImpl final : public ::nl::Weave::Profiles::Security::AppKeys::GroupKeyStoreBase {
  using WeaveGroupKey = ::nl::Weave::Profiles::Security::AppKeys::WeaveGroupKey;

 public:
  enum {
    kMaxGroupKeys =
        WEAVE_CONFIG_MAX_APPLICATION_EPOCH_KEYS +  // Maximum number of Epoch keys
        WEAVE_CONFIG_MAX_APPLICATION_GROUPS +  // Maximum number of Application Group Master keys
        1 +  // Maximum number of Root keys (1 for Service root key)
        1    // Fabric secret
  };

  WEAVE_ERROR Init();

  WEAVE_ERROR RetrieveGroupKey(uint32_t keyId, WeaveGroupKey& key) override;
  WEAVE_ERROR StoreGroupKey(const WeaveGroupKey& key) override;
  WEAVE_ERROR DeleteGroupKey(uint32_t keyId) override;
  WEAVE_ERROR DeleteGroupKeysOfAType(uint32_t keyType) override;
  WEAVE_ERROR EnumerateGroupKeys(uint32_t keyType, uint32_t* keyIds, uint8_t keyIdsArraySize,
                                 uint8_t& keyCount) override;
  WEAVE_ERROR Clear(void) override;
  WEAVE_ERROR RetrieveLastUsedEpochKeyId(void) override;
  WEAVE_ERROR StoreLastUsedEpochKeyId(void) override;
};

}  // namespace Internal
}  // namespace DeviceLayer
}  // namespace Weave
}  // namespace nl

#endif  // SRC_CONNECTIVITY_WEAVE_ADAPTATION_GROUP_KEY_STORE_IMPL_H_
