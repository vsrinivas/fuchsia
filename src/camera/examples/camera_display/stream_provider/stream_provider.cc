// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "stream_provider.h"

#include "controller_stream_provider.h"
#include "isp_stream_provider.h"
#include "manager_stream_provider.h"

std::unique_ptr<StreamProvider> StreamProvider::Create(Source source) {
  switch (source) {
    case Source::ISP:
      return IspStreamProvider::Create();
    case Source::CONTROLLER:
      return ControllerStreamProvider::Create();
    case Source::MANAGER:
      return ManagerStreamProvider::Create();
    default:
      return nullptr;
  }
}
