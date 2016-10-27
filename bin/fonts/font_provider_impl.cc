// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/fonts/font_provider_impl.h"

#include <magenta/syscalls.h>
#include <utility>

#include "lib/ftl/files/file.h"
#include "lib/ftl/logging.h"

namespace fonts {
namespace {

constexpr char kRobotoPath[] = "/boot/data/fonts/Roboto-Regular.ttf";
constexpr mx_rights_t kFontDataRights =
    MX_RIGHT_DUPLICATE | MX_RIGHT_TRANSFER | MX_RIGHT_READ | MX_RIGHT_MAP;

}  // namespace

FontProviderImpl::FontProviderImpl() = default;

FontProviderImpl::~FontProviderImpl() = default;

bool FontProviderImpl::LoadFonts() {
  // TODO(abarth): Rather than loading Roboto directly, we should load a config
  // file that describes what fonts are available on the system.
  std::string data;
  if (!files::ReadFileToString(kRobotoPath, &data)) {
    FTL_LOG(ERROR) << "Failed to read Roboto from '" << kRobotoPath << "'.";
    return false;
  }

  mx_status_t rv = mx::vmo::create(data.size(), 0, &roboto_regular_vmo_);
  if (rv < 0) {
    roboto_regular_vmo_.reset();
    FTL_LOG(ERROR) << "Failed to create VMO for Roboto.";
    return false;
  }

  rv = roboto_regular_vmo_.write(data.data(), 0, data.size(), nullptr);
  if (rv < 0) {
    FTL_LOG(ERROR) << "Failed to write data to VMO for Roboto.";
    roboto_regular_vmo_.reset();
    return false;
  }

  return true;
}

void FontProviderImpl::AddBinding(
    mojo::InterfaceRequest<mojo::FontProvider> request) {
  bindings_.AddBinding(this, std::move(request));
}

void FontProviderImpl::GetFont(mojo::FontRequestPtr request,
                               const GetFontCallback& callback) {
  if (!roboto_regular_vmo_) {
    callback.Run(nullptr);
    return;
  }

  // TODO(abarth): Use the data from |request| to select a font instead of
  // spamming Roboto-Regular.
  mx::vmo vmo;
  if (roboto_regular_vmo_.duplicate(kFontDataRights, &vmo) < 0) {
    callback.Run(nullptr);
    return;
  }

  auto data = mojo::FontData::New();
  data->vmo.reset(mojo::Handle(vmo.release()));
  auto response = mojo::FontResponse::New();
  response->data = std::move(data);
  callback.Run(std::move(response));
}

}  // namespace fonts
