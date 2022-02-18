// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::device::{binder::DevBinder, wayland::serve_wayland};
use crate::task::CurrentTask;
use crate::types::*;

/// Parses and runs the features from the provided "program strvec."
pub fn run_features<'a>(entries: &'a Vec<String>, current_task: &CurrentTask) -> Result<(), Errno> {
    for entry in entries {
        match entry.as_str() {
            "wayland" => {
                // TODO: The paths for the display and memory allocation file currently hard coded
                // to wayland-0 and wayland-1. In the future this will need to match the environment
                // variables set for the component.
                serve_wayland(
                    current_task,
                    b"/data/tmp/wayland-0".to_vec(),
                    b"/data/tmp/wayland-1".to_vec(),
                )?;
            }
            "binder" => {
                // Load the binder driver.
                let kernel = current_task.kernel();
                let mut device_registry = kernel.device_registry.write();
                device_registry.register_chrdev(DevBinder::new())?;

                // Add a device file to /dev
                let devtmpfs = crate::fs::devtmpfs::dev_tmp_fs(&*kernel);
                devtmpfs.root().create_node(
                    b"binder",
                    FileMode::IFCHR | FileMode::from_bits(0o666),
                    DevBinder::DEVICE_ID,
                )?;
            }
            feature => {
                log::warn!("Unsupported feature: {:?}", feature);
            }
        }
    }
    Ok(())
}
