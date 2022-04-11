// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;

#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "debug-bind",
    description = "Allows you to debug bind decisions.",
    example = "To debug why a driver did or didn't bind to a particular device:

    $ driver debug-bind '/boot/driver/usb_video.so' 'sys/platform/pci/00:1f.6'",
    error_code(1, "Failed to connect to the bind debugger service")
)]
pub struct DebugBindCommand {
    // TODO(surajmalhotra): Make this a URL once drivers are components.
    /// the path of the driver to debug, e.g. "/system/driver/usb_video.so"
    #[argh(positional)]
    pub driver_path: String,

    /// the path of the device to debug, relative to the /dev directory.
    /// E.g. "sys/platform/pci/00:1f.6" or "class/usb-device/000"
    #[argh(positional)]
    pub device_path: String,

    /// print out the device properties.
    #[argh(switch, short = 'p', long = "print-properties")]
    pub print_properties: bool,

    /// print out the bind program instructions.
    #[argh(switch, short = 'i', long = "print-instructions")]
    pub print_instructions: bool,

    /// if this exists, the user will be prompted for a component to select.
    #[argh(switch, short = 's', long = "select")]
    pub select: bool,
}
