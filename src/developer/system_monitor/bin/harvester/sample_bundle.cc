// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sample_bundle.h"

#include <lib/syslog/cpp/macros.h>

namespace harvester {

std::ostream& operator<<(std::ostream& out, const SampleBundle& bundle) {
  out << "SampleBundle:" << std::endl;
  out << "  Strings:" << std::endl;
  for (const auto& str : bundle.string_sample_list_) {
    out << "    " << str.first << ": " << str.second << std::endl;
  }
  out << "  Values:" << std::endl;
  for (const auto& value : bundle.int_sample_list_) {
    out << "    " << value.first << ": " << value.second << std::endl;
  }
  return out;
}

// After gathering the data, upload it to |dockyard|.
void SampleBundle::Upload(DockyardProxy* dockyard_proxy) {
  DockyardProxyStatus status =
      dockyard_proxy->SendSamples(int_sample_list_, string_sample_list_);

  if (FX_VLOG_IS_ON(2)) {
    FX_VLOGS(2) << DockyardErrorString("SendSamples", status);
    for (const auto& int_sample : int_sample_list_) {
      FX_VLOGS(2) << int_sample.first << ": " << int_sample.second;
    }
    for (const auto& string_sample : string_sample_list_) {
      FX_VLOGS(2) << string_sample.first << ": " << string_sample.second;
    }
  }

  int_sample_list_.clear();
  string_sample_list_.clear();
}

}  // namespace harvester.
