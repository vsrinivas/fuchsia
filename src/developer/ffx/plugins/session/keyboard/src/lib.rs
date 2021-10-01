// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Result},
    errors::ffx_bail,
    ffx_core::ffx_plugin,
    ffx_session_keyboard_args::Command,
    fidl_fuchsia_input_keymap::ConfigurationProxy,
};

#[ffx_plugin(
    "experimental_keyboard",
    ConfigurationProxy = "core/session-manager/session\\:session:expose:fuchsia.input.keymap.Configuration"
)]
pub async fn set_keymap(proxy: Option<ConfigurationProxy>, cmd: Command) -> Result<()> {
    if let Some(proxy) = proxy {
        let keymap = &cmd.keymap;
        proxy.set_layout(keymap.clone()).await.with_context(|| {
            format!(
                "while trying to call fuchsia.input.keymap.Configuration/SetLayout({:?})",
                &keymap
            )
        })?;
        println!("keymap ID set to: {:?}", &keymap);
    } else {
        ffx_bail!("The session does not seem to have fuchsia.input.keymap.Configuration exposed.");
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl::prelude::*;
    use fuchsia_async as fasync;
    use input_pipeline::text_settings_handler;

    #[fasync::run_singlethreaded(test)]
    async fn try_a_setting() {
        let handler = text_settings_handler::Instance::new(None);

        let (client_end, server_end) =
            fidl::endpoints::create_proxy_and_stream::<fkeymap::ConfigurationMarker>().unwrap();
        handler.get_serving_fn()(server_end);

        set_keymap(Command { keymap: fkeymap::Id::FrAzerty }, client_end).await.unwrap();
        assert_eq!(fkeymap::Id::FrAzerty, handler.get_keymap_id().await);
    }

    #[fasync::run_singlethreaded(test)]
    #[should_panic]
    async fn try_a_setting_with_error() {
        let (client_end, server_end) =
            fidl::endpoints::create_proxy_and_stream::<fkeymap::ConfigurationMarker>().unwrap();

        // Server end closes, so client should error out.
        server_end.control_handle().shutdown();

        set_keymap(Command { keymap: fkeymap::Id::FrAzerty }, client_end).await.unwrap();
    }
}
