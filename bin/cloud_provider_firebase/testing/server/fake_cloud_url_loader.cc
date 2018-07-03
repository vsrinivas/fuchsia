// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/cloud_provider_firebase/testing/server/fake_cloud_url_loader.h"

#include <lib/fxl/strings/string_view.h>
#include <lib/url/gurl.h>

namespace ledger {

namespace http = ::fuchsia::net::oldhttp;

namespace {
constexpr fxl::StringView kFirebaseHosts = ".firebaseio.com";
constexpr fxl::StringView kGcsPrefix =
    "https://firebasestorage.googleapis.com/v0/b/";

bool StringStartsWith(fxl::StringView str, fxl::StringView value) {
  if (str.size() < value.size()) {
    return false;
  }
  return str.substr(0, value.size()) == value;
}

bool StringEndsWith(fxl::StringView str, fxl::StringView value) {
  if (str.size() < value.size()) {
    return false;
  }
  return str.substr(str.size() - value.size()) == value;
}
}  // namespace

FakeCloudURLLoader::FakeCloudURLLoader() {}

FakeCloudURLLoader::~FakeCloudURLLoader() {}

void FakeCloudURLLoader::Start(http::URLRequest request,
                               StartCallback callback) {
  url::GURL url(request.url);
  FXL_DCHECK(url.is_valid());

  if (StringEndsWith(url.host(), kFirebaseHosts)) {
    firebase_servers_[url.host()].Serve(std::move(request), std::move(callback));
    return;
  }

  fxl::StringView url_view = url.spec();
  if (StringStartsWith(url_view, kGcsPrefix)) {
    // Extract GCS bucket name:
    // https://firebasestorage.googleapis.com/v0/b/foo/... -> foo
    url_view = url_view.substr(kGcsPrefix.size());
    size_t slash_pos = url_view.find("/", 0);
    FXL_DCHECK(slash_pos != std::string::npos);
    std::string bucket_name = url_view.substr(0, slash_pos).ToString();
    gcs_servers_[std::move(bucket_name)].Serve(std::move(request), std::move(callback));
    return;
  }

  FXL_NOTREACHED() << "Unknown URL: " << url.spec();
}

void FakeCloudURLLoader::FollowRedirect(FollowRedirectCallback /*callback*/) {
  FXL_NOTIMPLEMENTED();
}

void FakeCloudURLLoader::QueryStatus(QueryStatusCallback /*callback*/) {
  FXL_NOTIMPLEMENTED();
}

}  // namespace ledger
