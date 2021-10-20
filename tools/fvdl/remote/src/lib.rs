// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use ffx_emulator_common::vdl_files::VDLFiles;
use ffx_emulator_remote_args::RemoteCommand;

pub async fn remote(cmd: RemoteCommand) -> Result<(), anyhow::Error> {
    VDLFiles::new(cmd.sdk, false)?.remote_emulator(&cmd)
}
