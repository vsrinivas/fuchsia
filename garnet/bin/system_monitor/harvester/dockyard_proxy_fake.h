// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_SYSTEM_MONITOR_HARVESTER_DOCKYARD_PROXY_FAKE_H_
#define GARNET_BIN_SYSTEM_MONITOR_HARVESTER_DOCKYARD_PROXY_FAKE_H_

#include <string>

#include "dockyard_proxy.h"

namespace harvester {

class DockyardProxyFake : public DockyardProxy {
 public:
  DockyardProxyFake() {}

  // |DockyardProxy|.
  DockyardProxyStatus Init() override;

  // |DockyardProxy|.
  DockyardProxyStatus SendInspectJson(const std::string& stream_name,
                                      const std::string& json) override;

  // |DockyardProxy|.
  DockyardProxyStatus SendSample(const std::string& stream_name,
                                 uint64_t value) override;

  // |DockyardProxy|.
  DockyardProxyStatus SendSampleList(const SampleList list) override;

  // |DockyardProxy|.
  DockyardProxyStatus SendStringSampleList(
      const StringSampleList list) override;
};

}  // namespace harvester

#endif  // GARNET_BIN_SYSTEM_MONITOR_HARVESTER_DOCKYARD_PROXY_FAKE_H_
