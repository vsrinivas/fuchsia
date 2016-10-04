// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/icu_data/icu_data_provider_impl.h"

#include <magenta/syscalls.h>
#include <utility>

#include "lib/ftl/files/file.h"
#include "lib/ftl/logging.h"
#include "mojo/services/icu_data/cpp/constants.h"

namespace icu_data {
namespace {

constexpr char kICUDataPath[] = "/boot/data/icu_data/icudtl.dat";
constexpr mx_rights_t kICUDataRights =
    MX_RIGHT_DUPLICATE | MX_RIGHT_TRANSFER | MX_RIGHT_READ | MX_RIGHT_MAP;

} // namespace

ICUDataProviderImpl::ICUDataProviderImpl() = default;

ICUDataProviderImpl::~ICUDataProviderImpl() = default;

bool ICUDataProviderImpl::LoadData() {
  // TODO(mikejurka): get the underlying VMO of the data file, so we don't need
  // to load it and then copy it
  std::string data;
  if (!files::ReadFileToString(kICUDataPath, &data)) {
    FTL_LOG(ERROR) << "Loading ICU data failed: Failed to read ICU data from '"
                   << kICUDataPath << "'.";
    return false;
  }

  icu_data_vmo_.reset(mx_vmo_create(data.size()));
  if (icu_data_vmo_.get() < 0) {
    FTL_LOG(ERROR)
        << "Loading ICU data failed: Failed to create VMO for ICU data.";
    icu_data_vmo_.reset();
    return false;
  }

  mx_ssize_t result =
      mx_vmo_write(icu_data_vmo_.get(), data.data(), 0, data.size());
  if (result < 0) {
    FTL_LOG(ERROR)
        << "Loading ICU data failed: Failed to write ICU data to VMO.";
    icu_data_vmo_.reset();
    return false;
  }

  return true;
}

void ICUDataProviderImpl::AddBinding(
    mojo::InterfaceRequest<mojo::ICUDataProvider> request) {
  bindings_.AddBinding(this, std::move(request));
}

void ICUDataProviderImpl::ICUDataWithSha1(
    const mojo::String &sha1hash, const ICUDataWithSha1Callback &callback) {
  if (!icu_data_vmo_.is_valid()) {
    callback.Run(nullptr);
    return;
  }

  if (sha1hash != kDataHash) {
    callback.Run(nullptr);
    return;
  }

  mx_handle_t vmo = mx_handle_duplicate(icu_data_vmo_.get(), kICUDataRights);
  if (vmo < 0) {
    callback.Run(nullptr);
    return;
  }

  auto data = mojo::ICUData::New();
  data->vmo.reset(mojo::Handle(vmo));
  callback.Run(std::move(data));
}

} // namespace icu_data
