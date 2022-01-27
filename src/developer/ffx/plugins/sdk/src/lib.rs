// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Result},
    ffx_config::{
        sdk::{Sdk, SdkVersion},
        set, ConfigLevel,
    },
    ffx_core::ffx_plugin,
    ffx_sdk_args::{
        ListCommand, ListSubCommand, SdkCommand, SetCommand, SetRootCommand, SetSubCommand,
        SubCommand,
    },
    pbms::get_pbms,
    sdk_metadata::Metadata,
    std::io::{stdout, Write},
};

#[ffx_plugin()]
pub async fn exec_sdk(command: SdkCommand) -> Result<()> {
    let mut writer = Box::new(stdout());
    let sdk = ffx_config::get_sdk().await;

    match &command.sub {
        SubCommand::Version(_) => exec_version(sdk?, writer).await,
        SubCommand::Set(cmd) => exec_set(cmd).await,
        SubCommand::List(cmd) => exec_list(&mut writer, sdk?, cmd).await,
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

async fn exec_set(cmd: &SetCommand) -> Result<()> {
    match &cmd.sub {
        SetSubCommand::Root(SetRootCommand { path }) => {
            let abs_path =
                path.canonicalize().context(format!("making path absolute: {:?}", path))?;
            set(("sdk.root", ConfigLevel::User), abs_path.to_string_lossy().into()).await?;
            Ok(())
        }
    }
}

/// Execute the `list` sub-command.
async fn exec_list<W: Write + Sync>(writer: &mut W, _sdk: Sdk, cmd: &ListCommand) -> Result<()> {
    match &cmd.sub {
        ListSubCommand::ProductBundles(_) => exec_list_pbms(writer).await,
    }
}

/// Execute `list product-bundles`.
///
/// The list will be written to `writer`.
async fn exec_list_pbms<W: Write + Sync>(writer: &mut W) -> Result<()> {
    let entries = get_pbms(/*update_metadata=*/ true).await.context("get pbms entries")?;
    for entry in entries.iter() {
        match entry {
            Metadata::ProductBundleV1(bundle) => writeln!(writer, "{}", bundle.name)?,
            _ => {}
        }
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
