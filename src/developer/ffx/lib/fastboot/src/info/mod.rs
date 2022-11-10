// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::common::prepare,
    anyhow::{anyhow, Result},
    fidl::endpoints::{create_endpoints, ServerEnd},
    fidl_fuchsia_developer_ffx::{FastbootProxy, VariableListenerMarker, VariableListenerRequest},
    futures::prelude::*,
    futures::try_join,
    std::io::Write,
};

/// Aggregates fastboot variables from a callback listener.
async fn handle_variables_for_fastboot<W: Write>(
    writer: &mut W,
    var_server: ServerEnd<VariableListenerMarker>,
) -> Result<()> {
    let mut stream = var_server.into_stream()?;
    loop {
        match stream.try_next().await? {
            Some(VariableListenerRequest::OnVariable { name, value, .. }) => {
                writeln!(writer, "{}: {}", name, value)?;
            }
            None => return Ok(()),
        }
    }
}

pub async fn info<W: Write>(writer: &mut W, fastboot_proxy: &FastbootProxy) -> Result<()> {
    prepare(writer, &fastboot_proxy).await?;
    let (var_client, var_server) = create_endpoints::<VariableListenerMarker>()?;
    let _ = try_join!(
        fastboot_proxy.get_all_vars(var_client).map_err(|e| {
            tracing::error!("FIDL Communication error: {}", e);
            anyhow!(
                "There was an error communicating with the daemon. Try running\n\
                `ffx doctor` for further diagnositcs."
            )
        }),
        handle_variables_for_fastboot(writer, var_server),
    )?;
    Ok(())
}

////////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use super::*;
    use crate::test::setup;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_showing_variables() -> Result<()> {
        let (_, proxy) = setup();
        let mut writer = Vec::<u8>::new();
        info(&mut writer, &proxy).await?;
        let output = String::from_utf8(writer).expect("utf-8 string");
        assert!(output.contains("test: test"));
        Ok(())
    }
}
