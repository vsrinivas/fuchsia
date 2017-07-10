// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/test/cloud_server/fake_cloud_url_loader.h"

#include "lib/ftl/strings/string_view.h"
#include "lib/url/gurl.h"

namespace ledger {

namespace {
constexpr ftl::StringView kFirebaseHosts = ".firebaseio.com";
constexpr ftl::StringView kGcsPrefix =
    "https://firebasestorage.googleapis.com/v0/b/";

bool StringStartsWith(ftl::StringView str, ftl::StringView value) {
  if (str.size() < value.size()) {
    return false;
  }
  return str.substr(0, value.size()) == value;
}

bool StringEndsWith(ftl::StringView str, ftl::StringView value) {
  if (str.size() < value.size()) {
    return false;
  }
  return str.substr(str.size() - value.size()) == value;
}
}  // namespace

FakeCloudURLLoader::FakeCloudURLLoader() {}

FakeCloudURLLoader::~FakeCloudURLLoader() {}

void FakeCloudURLLoader::Start(network::URLRequestPtr request,
                               const StartCallback& callback) {
  url::GURL url(request->url);
  FTL_DCHECK(url.is_valid());

  if (StringEndsWith(url.host(), kFirebaseHosts)) {
    firebase_servers_[url.host()].Serve(std::move(request), callback);
    return;
  }

  ftl::StringView url_view = url.spec();
  if (StringStartsWith(url_view, kGcsPrefix)) {
    // Extract GCS bucket name:
    // https://firebasestorage.googleapis.com/v0/b/foo/... -> foo
    url_view = url_view.substr(kGcsPrefix.size());
    size_t slash_pos = url_view.find("/", 0);
    FTL_DCHECK(slash_pos != std::string::npos);
    std::string bucket_name = url_view.substr(0, slash_pos).ToString();
    gcs_servers_[std::move(bucket_name)].Serve(std::move(request), callback);
    return;
  }

  FTL_NOTREACHED() << "Unknown URL: " << url.spec();
}

void FakeCloudURLLoader::FollowRedirect(
    const FollowRedirectCallback& callback) {
  FTL_NOTIMPLEMENTED();
}

void FakeCloudURLLoader::QueryStatus(const QueryStatusCallback& callback) {
  FTL_NOTIMPLEMENTED();
}

}  // namespace ledger
