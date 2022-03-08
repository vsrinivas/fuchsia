// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/build_info/testing/build_info.h"

#include <fuchsia/buildinfo/test/cpp/fidl.h>

void FakeProviderImpl::GetBuildInfo(GetBuildInfoCallback callback) {
  fuchsia::buildinfo::BuildInfo build_info;
  build_info.set_product_config(info_ref_->product_config_);
  build_info.set_board_config(info_ref_->board_config_);
  build_info.set_version(info_ref_->version_);
  build_info.set_latest_commit_date(info_ref_->latest_commit_date_);

  callback(std::move(build_info));
}

void BuildInfoTestControllerImpl::SetBuildInfo(::fuchsia::buildinfo::BuildInfo build_info,
                                               SetBuildInfoCallback callback) {
  info_ref_->product_config_ = build_info.product_config();
  info_ref_->board_config_ = build_info.board_config();
  info_ref_->version_ = build_info.version();
  info_ref_->latest_commit_date_ = build_info.latest_commit_date();

  callback();
}
