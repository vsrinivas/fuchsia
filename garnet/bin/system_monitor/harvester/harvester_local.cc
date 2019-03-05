// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "harvester_local.h"
#include "lib/fxl/logging.h"

namespace harvester {

bool HarvesterLocal::Init() {
  FXL_LOG(INFO) << "HarvesterLocal::Init";
  return true;
}

HarvesterStatus HarvesterLocal::SendSample(const std::string& stream_name,
                                           uint64_t value) {
  FXL_LOG(INFO) << "HarvesterLocal::SendSample";
  return OK;
}

HarvesterStatus HarvesterLocal::SendSampleList(const SampleList list) {
  FXL_LOG(INFO) << "HarvesterLocal::SendSampleList";
  return OK;
}

}  // namespace harvester
