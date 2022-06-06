// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Result;
use errors::ffx_bail;
use ffx_core::ffx_plugin;
use ffx_emulator_commands::get_engine_by_name;
use ffx_emulator_common::{config::FfxConfigWrapper, instances::get_all_instances};
use ffx_emulator_list_args::ListCommand;

// TODO(fxbug.dev/94232): Update this error message once shut down is more robust.
const BROKEN_MESSAGE: &str = r#"
One or more emulators are in a 'Broken' state. This is an uncommon state, but usually happens if
the Fuchsia source tree or SDK is updated while the emulator is still running. Communication with
a "Broken" emulator may still be possible, but errors will be encountered for any further `ffx emu`
commands. Running `ffx emu stop` will not shut down a broken emulator (this should be fixed as part
of fxbug.dev/94232), but it will clear that emulator's state from the system, so this error won't
appear anymore.
"#;

#[ffx_plugin()]
pub async fn list(_cmd: ListCommand) -> Result<()> {
    let ffx_config = FfxConfigWrapper::new();
    let instance_list: Vec<Option<String>> = match get_all_instances(&ffx_config).await {
        Ok(list) => list.into_iter().map(|v| Some(v)).collect(),
        Err(e) => ffx_bail!("Error encountered looking up emulator instances: {:?}", e),
    };
    let mut broken = false;
    for mut some_name in instance_list {
        match get_engine_by_name(&ffx_config, &mut some_name).await {
            Ok(engine) => {
                let name = some_name.unwrap();
                if engine.is_running() {
                    println!("[Active]    {}", name);
                } else {
                    println!("[Inactive]  {}", name);
                }
            }
            Err(_) => {
                println!("[Broken]    {}", some_name.unwrap_or("<unspecified>".to_string()));
                broken = true;
                continue;
            }
        };
    }
    if broken {
        eprintln!("{}", BROKEN_MESSAGE);
    }
    Ok(())
}
