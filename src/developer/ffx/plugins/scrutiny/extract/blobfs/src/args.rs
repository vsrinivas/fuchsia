// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "blobfs",
    description = "Extracts a Blobfs block file",
    example = "To extract a Blobfs block file:

        $ffx scrutiny extract blobfs blob.blk /tmp/blobs",
    note = "Extracts a blobfs block file to a specific directory."
)]
pub struct ScrutinyBlobfsCommand {
    #[argh(positional)]
    pub input: String,
    #[argh(positional)]
    pub output: String,
}
