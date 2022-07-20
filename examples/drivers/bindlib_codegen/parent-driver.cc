// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// [START code]
#include <bind/fuchsia/example/library/cpp/bind.h>

std::string a = bind_fuchsia_example_library::NAME;
uint32_t b = bind_fuchsia_example_library::BIND_PCI_VID_GIZMOTRONICS;
uint32_t c = bind_fuchsia_pci::BIND_PROTOCOL_DEVICE;
// [END code]
