// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// [START code]
use bind_fuchsia_example_library;
use bind_fuchsia_example_library::bind_fuchsia_pci;

fn main() {
    let _a: &str = bind_fuchsia_example_library::NAME;
    let _b: u32 = bind_fuchsia_example_library::BIND_PCI_VID_GIZMOTRONICS;
    let _c: u32 = bind_fuchsia_pci::BIND_PROTOCOL_DEVICE;
}
// [END code]
