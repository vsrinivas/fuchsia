// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_wlan_common as fidl_common;
use fuchsia_inspect_contrib::{inspect_insert, log::WriteInspect, nodes::NodeWriter};

pub struct InspectWlanChan<'a>(pub &'a fidl_common::WlanChan);

impl<'a> WriteInspect for InspectWlanChan<'a> {
    fn write_inspect(&self, writer: &mut NodeWriter<'_>, key: &str) {
        inspect_insert!(writer, var key: {
            primary: self.0.primary,
            cbw: format!("{:?}", self.0.cbw),
            secondary80: self.0.secondary80,
        });
    }
}
