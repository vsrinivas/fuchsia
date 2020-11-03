// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    ffx_config::sdk::{Sdk, SdkVersion},
    ffx_core::ffx_plugin,
    ffx_sdk_args::{SdkCommand, SubCommand},
    std::io::Write,
};

#[ffx_plugin()]
pub async fn exec_sdk(command: SdkCommand) -> Result<()> {
    let writer = Box::new(std::io::stdout());
    let sdk = ffx_config::get_sdk().await?;

    match &command.sub {
        SubCommand::Version(_) => exec_version(sdk, writer).await,
    }
}

async fn exec_version<W: Write + Sync>(sdk: Sdk, mut writer: W) -> Result<()> {
    match sdk.get_version() {
        SdkVersion::Version(v) => writeln!(writer, "{}", v)?,
        SdkVersion::InTree => writeln!(writer, "<in tree>")?,
        SdkVersion::Unknown => writeln!(writer, "<unknown>")?,
    }

    Ok(())
}

#[cfg(test)]
mod test {
    use super::*;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_version_with_string() {
        let mut out = Vec::new();
        let sdk = Sdk::get_empty_sdk_with_version(SdkVersion::Version("Test.0".to_owned()));

        exec_version(sdk, &mut out).await.unwrap();
        let out = String::from_utf8(out).unwrap();

        assert_eq!("Test.0\n", out);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_version_in_tree() {
        let mut out = Vec::new();
        let sdk = Sdk::get_empty_sdk_with_version(SdkVersion::InTree);

        exec_version(sdk, &mut out).await.unwrap();
        let out = String::from_utf8(out).unwrap();

        assert_eq!("<in tree>\n", out);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_version_unknown() {
        let mut out = Vec::new();
        let sdk = Sdk::get_empty_sdk_with_version(SdkVersion::Unknown);

        exec_version(sdk, &mut out).await.unwrap();
        let out = String::from_utf8(out).unwrap();

        assert_eq!("<unknown>\n", out);
    }
}
