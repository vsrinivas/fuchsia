// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::test::*,
    anyhow::*,
    errors::ffx_bail,
    fuchsia_async::{unblock, TimeoutExt},
    std::fs::{create_dir_all, File},
    std::io::Write,
    std::io::{BufRead, BufReader},
    std::path::Path,
    std::process::Stdio,
    std::sync::mpsc::channel,
    std::time::Duration,
};

pub mod include_target {
    use super::*;

    pub(crate) async fn test_debug_run_crasher() -> Result<()> {
        // If the test is running on CI/CQ bots, it's isolated with only files listed as test_data
        // available. We have added zxdb and zxdb-meta.json in ffx-e2e-test-data but we have to
        // also provide an index file at host_x64/sdk/manifest/host_tools.modular.
        // Only when invoked from ffx-e2e-with-target.sh we could get sdk.root=.
        if ffx_config::get::<String, _>("sdk.root").await.unwrap_or_default() == "." {
            ensure!(cfg!(target_arch = "x86_64"), "The test only supports x86_64 for now.");
            let manifest_file = Path::new("sdk/manifest/core");
            if !manifest_file.exists() {
                create_dir_all(manifest_file.parent().unwrap())?;
                File::create(manifest_file)?.write(br#"{
                    "atoms": [{
                        "category": "partner",
                        "deps": [],
                        "files": [
                        {
                            "destination": "tools/x64/zxdb",
                            "source": "host_x64/zxdb"
                        },
                        {
                            "destination": "tools/x64/zxdb-meta.json",
                            "source": "host_x64/gen/src/developer/debug/zxdb/zxdb_sdk.meta.json"
                        }
                        ],
                        "gn-label": "//src/developer/debug/zxdb:zxdb_sdk(//build/toolchain:host_x64)",
                        "id": "sdk://tools/x64/zxdb",
                        "meta": "tools/x64/zxdb-meta.json",
                        "plasa": [],
                        "type": "host_tool"
                    }],
                    "ids": []
                }"#)?;
            }
        }

        let target = get_target_nodename().await?;
        let sdk = ffx_config::get_sdk().await?;
        let isolate = Isolate::new("target-debug-run-crasher").await?;
        let mut config = "sdk.root=".to_owned();
        config.push_str(sdk.get_path_prefix().to_str().unwrap());
        if sdk.get_version() == &ffx_config::sdk::SdkVersion::InTree {
            config.push_str(",sdk.type=in-tree");
        }
        let mut child = isolate
            .ffx_cmd(&[
                "--target",
                &target,
                "--config",
                &config,
                "debug",
                "connect",
                "--",
                "--run",
                "/boot/bin/crasher",
            ])
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .stderr(Stdio::null())
            .spawn()?;

        let (sender, receiver) = channel();

        unblock(move || {
            let mut stdout_reader = BufReader::new(child.stdout.take().unwrap());

            loop {
                let mut line = String::new();
                let size = stdout_reader.read_line(&mut line)?;
                if size == 0 {
                    ffx_bail!("Unexpected EOF");
                }

                let possible_patterns = [
                    "Page fault writing address 0x0", // x64
                    "Page fault reading address 0x0", // x64-asan
                    "Data fault writing address 0x0", // arm64
                    "Data fault reading address 0x0", // arm64-asan
                ];
                if possible_patterns.iter().any(|pat| line.contains(pat)) {
                    child.wait()?; // close the stdin and the process should exit.
                    return Ok(());
                }
                sender.send(line)?;
            }
        })
        // ffx self-test has a per-case timeout of 12 seconds.
        .on_timeout(Duration::from_secs(11), || ffx_bail!("timeout after 11s"))
        .await
        .map_err(|e| {
            while let Ok(line) = receiver.recv() {
                eprint!("{}", line);
            }
            e
        })
    }
}
