// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{Error, ResultExt},
    fidl_test_inspect_validate::ValidateMarker,
    fuchsia_async as fasync,
    fuchsia_component::client::{launch, launcher},
    log::*,
};

#[cfg(test)]
mod tests {
    use super::*;

    #[fasync::run_singlethreaded(test)]
    async fn test_everything() -> Result<(), Error> {
        crate::init_syslog();
        let server_url = "fuchsia-pkg://fuchsia.com/inspect_validator_puppet_rust\
                          #meta/inspect_validator_puppet_rust.cmx"
            .to_owned();

        let launcher = launcher().context("Failed to open launcher service")?;
        let app =
            launch(&launcher, server_url, None).context("Failed to launch validate puppet")?;

        let test_puppet = app
            .connect_to_service::<ValidateMarker>()
            .context("Failed to connect to validate puppet")?;
        info!("Client about to await");

        let res = test_puppet.echo_the_string(Some("hello world!")).await?;
        info!("response: {:?}", res);
        assert_eq!(res.unwrap_or("This Will Fail".to_owned()), "hello world!");
        Ok(())
    }
}
