// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/icu_data/icu_data_provider_impl.h"

#include <magenta/syscalls.h>
#include <utility>

#include "lib/icu_data/cpp/constants.h"
#include "lib/fxl/logging.h"
#include "lib/fsl/vmo/file.h"

namespace icu_data {
namespace {

constexpr char kICUDataPath[] = "/pkg/data/icudtl.dat";
constexpr mx_rights_t kICUDataRights =
    MX_RIGHT_DUPLICATE | MX_RIGHT_TRANSFER | MX_RIGHT_READ | MX_RIGHT_MAP;

}  // namespace

ICUDataProviderImpl::ICUDataProviderImpl() = default;

ICUDataProviderImpl::~ICUDataProviderImpl() = default;

bool ICUDataProviderImpl::LoadData() {
  if (!fsl::VmoFromFilename(kICUDataPath, &icu_data_vmo_)) {
    FXL_LOG(ERROR)
        << "Loading ICU data failed: Failed to create VMO from file '"
        << kICUDataPath << "'.";
    icu_data_vmo_.reset();
    return false;
  }
  return true;
}

void ICUDataProviderImpl::AddBinding(
    fidl::InterfaceRequest<ICUDataProvider> request) {
  bindings_.AddBinding(this, std::move(request));
}

void ICUDataProviderImpl::ICUDataWithSha1(
    const fidl::String& sha1hash,
    const ICUDataWithSha1Callback& callback) {
  if (!icu_data_vmo_) {
    callback(nullptr);
    return;
  }

  if (sha1hash != kDataHash) {
    callback(nullptr);
    return;
  }

  auto data = ICUData::New();
  if (icu_data_vmo_.duplicate(kICUDataRights, &data->vmo) < 0) {
    callback(nullptr);
    return;
  }

  callback(std::move(data));
}

}  // namespace icu_data
