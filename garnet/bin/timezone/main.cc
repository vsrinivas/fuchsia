// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fxl/logging.h>
#include <lib/sys/cpp/component_context.h>

#include "garnet/bin/timezone/timezone.h"

namespace time_zone {

constexpr char kIcuDataPath[] = "/pkg/data/icudtl.dat";
constexpr char kTzIdPath[] = "/data/tz_id";

class MainService {
 public:
  MainService()
      : timezone_(sys::ComponentContext::Create(), kIcuDataPath, kTzIdPath) {}

 private:
  TimezoneImpl timezone_;
};

}  // namespace time_zone

int main(int argc, char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  time_zone::MainService svc;
  loop.Run();
  return 0;
}
