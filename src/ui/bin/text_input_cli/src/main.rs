// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context, Result};
use argh::{self, FromArgs};
use fidl_fuchsia_input_keymap as fkeymap;
use fuchsia_async as fasync;
use fuchsia_component;
use fuchsia_syslog;

#[derive(FromArgs, Debug)]
#[argh(description = "A minimal program to set the keymap ID")]
struct Args {
    #[argh(
        option,
        from_str_fn(to_keymap_id),
        description = "the keymap to use, one of US_QWERTY,FR_AZERTY"
    )]
    keymap: fkeymap::Id,
}

fn to_keymap_id(value: &str) -> Result<fkeymap::Id, String> {
    if value == "US_QWERTY" {
        return Ok(fkeymap::Id::UsQwerty);
    }
    if value == "FR_AZERTY" {
        return Ok(fkeymap::Id::FrAzerty);
    }
    Err(format!("keymap identifier not recognized: {}", value))
}

async fn run(args: Args, proxy: fkeymap::ConfigurationProxy) -> Result<()> {
    let keymap = &args.keymap;
    proxy.set_layout(keymap.clone()).await.with_context(|| {
        format!("while trying to call fuchsia.input.keymap.Configuration/SetLayout({:?})", &keymap)
    })?;
    println!("keymap ID set to: {:?}", &keymap);
    Ok(())
}

#[fasync::run_singlethreaded]
async fn main() -> Result<()> {
    fuchsia_syslog::init_with_tags(&["text_input_cli"]).expect("init logging");
    let args: Args = argh::from_env();
    let proxy = fuchsia_component::client::connect_to_protocol::<fkeymap::ConfigurationMarker>()
        .context("while trying to connect to fuchsia.input.keymap.Configuration")?;
    run(args, proxy).await
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl::endpoints::RequestStream;
    use fuchsia_async as fasync;
    use input_pipeline::text_settings_handler;

    #[fasync::run_singlethreaded(test)]
    async fn try_a_setting() {
        let handler = text_settings_handler::Instance::new(None);

        let (client_end, server_end) =
            fidl::endpoints::create_proxy_and_stream::<fkeymap::ConfigurationMarker>().unwrap();
        handler.get_serving_fn()(server_end);

        run(Args { keymap: fkeymap::Id::FrAzerty }, client_end).await.unwrap();
        assert_eq!(fkeymap::Id::FrAzerty, handler.get_keymap_id().await);
    }

    #[fasync::run_singlethreaded(test)]
    #[should_panic]
    async fn try_a_setting_with_error() {
        let (client_end, server_end) =
            fidl::endpoints::create_proxy_and_stream::<fkeymap::ConfigurationMarker>().unwrap();

        // Server end closes, so client should error out.
        server_end.control_handle().shutdown();

        run(Args { keymap: fkeymap::Id::FrAzerty }, client_end).await.unwrap();
    }
}
