// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dockyard_proxy_local.h"

#include "src/lib/syslog/cpp/logger.h"

namespace harvester {

DockyardProxyStatus DockyardProxyLocal::Init() {
  FX_LOGS(INFO) << "DockyardProxyLocal::Init";
  return DockyardProxyStatus::OK;
}

DockyardProxyStatus DockyardProxyLocal::SendInspectJson(
    const std::string& /*stream_name*/, const std::string& /*json*/) {
  FX_LOGS(INFO) << "DockyardProxyLocal::SendInspectJson";
  return DockyardProxyStatus::OK;
}

DockyardProxyStatus DockyardProxyLocal::SendSample(
    const std::string& /*stream_name*/, uint64_t /*value*/) {
  FX_LOGS(INFO) << "DockyardProxyLocal::SendSample";
  return DockyardProxyStatus::OK;
}

DockyardProxyStatus DockyardProxyLocal::SendSampleList(
    const SampleList& /*list*/) {
  FX_LOGS(INFO) << "DockyardProxyLocal::SendSampleList";
  return DockyardProxyStatus::OK;
}

DockyardProxyStatus DockyardProxyLocal::SendStringSampleList(
    const StringSampleList& /*list*/) {
  FX_LOGS(INFO) << "DockyardProxyLocal::StringSampleList";
  return DockyardProxyStatus::OK;
}

DockyardProxyStatus DockyardProxyLocal::SendSamples(
    const SampleList& /*int_samples*/,
    const StringSampleList& /*string_samples*/) {
  FX_LOGS(INFO) << "DockyardProxyLocal::SendSamples";
  return DockyardProxyStatus::OK;
}

}  // namespace harvester
