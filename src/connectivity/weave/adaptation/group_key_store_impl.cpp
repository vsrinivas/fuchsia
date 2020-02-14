// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// clang-format off
#include <Weave/DeviceLayer/internal/WeaveDeviceLayerInternal.h>
#include "group_key_store_impl.h"
// clang-format on

using namespace ::nl;
using namespace ::nl::Weave;
using namespace ::nl::Weave::Profiles::Security::AppKeys;

namespace nl {
namespace Weave {
namespace DeviceLayer {
namespace Internal {

WEAVE_ERROR GroupKeyStoreImpl::RetrieveGroupKey(uint32_t keyId, WeaveGroupKey& key) {
  return WEAVE_NO_ERROR;
}

WEAVE_ERROR GroupKeyStoreImpl::StoreGroupKey(const WeaveGroupKey& key) { return WEAVE_NO_ERROR; }

WEAVE_ERROR GroupKeyStoreImpl::DeleteGroupKey(uint32_t keyId) { return WEAVE_NO_ERROR; }

WEAVE_ERROR GroupKeyStoreImpl::DeleteGroupKeysOfAType(uint32_t keyType) { return WEAVE_NO_ERROR; }

WEAVE_ERROR GroupKeyStoreImpl::EnumerateGroupKeys(uint32_t keyType, uint32_t* keyIds,
                                                  uint8_t keyIdsArraySize, uint8_t& keyCount) {
  return WEAVE_NO_ERROR;
}

WEAVE_ERROR GroupKeyStoreImpl::Clear(void) { return WEAVE_NO_ERROR; }

WEAVE_ERROR GroupKeyStoreImpl::RetrieveLastUsedEpochKeyId(void) { return WEAVE_NO_ERROR; }

WEAVE_ERROR GroupKeyStoreImpl::StoreLastUsedEpochKeyId(void) { return WEAVE_NO_ERROR; }

WEAVE_ERROR GroupKeyStoreImpl::Init() { return WEAVE_NO_ERROR; }

}  // namespace Internal
}  // namespace DeviceLayer
}  // namespace Weave
}  // namespace nl
