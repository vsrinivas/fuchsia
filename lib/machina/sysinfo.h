// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_SYSINFO_H_
#define GARNET_LIB_MACHINA_SYSINFO_H_

#include <fuchsia/sysinfo/cpp/fidl.h>

namespace machina {

static constexpr char kSysInfoPath[] = "/dev/misc/sysinfo";

static fidl::SynchronousInterfacePtr<fuchsia::sysinfo::Device> get_sysinfo() {
  fidl::SynchronousInterfacePtr<fuchsia::sysinfo::Device> device;
  fdio_service_connect(kSysInfoPath,
                       device.NewRequest().TakeChannel().release());
  return device;
}

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_SYSINFO_H_
