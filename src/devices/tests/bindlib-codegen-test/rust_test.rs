// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Result;
use bind_bindlib_codegen_testlib;
use bind_bindlib_codegen_testlib::bind_fuchsia_pci;
use fuchsia_async as fasync;

#[fasync::run_singlethreaded(test)]
async fn test_constants_accessible() -> Result<()> {
    assert_eq!("bindlib.codegen.testlib.kinglet", bind_bindlib_codegen_testlib::KINGLET);
    assert_eq!(3839, bind_fuchsia_pci::BIND_PCI_VID_TEST);
    Ok(())
}
