// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context, Result},
    errors::{ffx_bail, ffx_error},
    ffx_config::{
        sdk::{Sdk, SdkVersion},
        set, ConfigLevel,
    },
    ffx_core::ffx_plugin,
    ffx_sdk_args::{
        ListCommand, ListSubCommand, SdkCommand, SetCommand, SetRootCommand, SetSubCommand,
        SubCommand,
    },
    fms::Entries,
    gcs::{
        client::ClientFactory,
        token_store::{read_boto_refresh_token, TokenStore},
    },
    sdk_metadata::Metadata,
    serde::Deserialize,
    std::fs::File,
    std::io::{BufReader, Write},
    std::path::PathBuf,
    tempfile::tempdir,
};

#[ffx_plugin()]
pub async fn exec_sdk(command: SdkCommand) -> Result<()> {
    let mut writer = Box::new(std::io::stdout());
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
async fn exec_list<W: Write + Sync>(writer: &mut W, sdk: Sdk, cmd: &ListCommand) -> Result<()> {
    match &cmd.sub {
        ListSubCommand::ProductBundles(_) => match sdk.get_version() {
            SdkVersion::Version(v) => exec_list_pbms_sdk(writer, v).await,
            SdkVersion::InTree => exec_list_pbms_in_tree(writer, sdk),
            SdkVersion::Unknown => ffx_bail!("Unknown SDK version"),
        },
    }
}

/// Execute `list product-bundles` for the SDK (rather than in-tree).
async fn exec_list_pbms_sdk<W: Write + Sync>(writer: &mut W, version: &String) -> Result<()> {
    let temp_dir = tempdir()?;
    // TODO(fxb/89584): Change to using ffx client Id and consent screen.
    let boto: Option<PathBuf> = ffx_config::file("flash.gcs.token").await?;
    let auth = match boto {
        Some(boto_path) => {
            TokenStore::new_with_auth(
                read_boto_refresh_token(&boto_path)?
                    .ok_or(anyhow!("Could not read boto token store"))?,
                /*access_token=*/ None,
            )?
        }
        None => ffx_bail!("GCS authentication not found."),
    };

    let client_factory = ClientFactory::new(auth);
    let client = client_factory.create_client();

    let product_bundle_container_path = temp_dir.path().join("product_bundles.json");
    let gcs_path = format!("development/{}/sdk/product_bundles.json", version);
    client.fetch("fuchsia-sdk", &gcs_path, &product_bundle_container_path).await?;

    let mut container = File::open(&product_bundle_container_path).map(BufReader::new)?;
    let mut entries = Entries::new();
    entries.add_json(&mut container)?;
    for entry in entries.iter() {
        match entry {
            Metadata::ProductBundleV1(bundle) => writeln!(writer, "{}", bundle.name)?,
            _ => {}
        }
    }

    Ok(())
}

#[derive(Default, Deserialize)]
pub struct Images(Vec<Image>);

#[derive(Default, Deserialize)]
pub struct Image {
    pub name: String,
    pub path: String,
    // Ignore the rest of the fields
}

/// Execute `list product-bundles` for in-tree (rather than the SDK).
fn exec_list_pbms_in_tree<W: Write + Sync>(writer: &mut W, sdk: Sdk) -> Result<()> {
    let mut path = sdk.get_path_prefix().to_path_buf();
    let manifest_path = path.join("images.json");
    let images: Images = File::open(manifest_path.clone())
        .map_err(|e| ffx_error!("Cannot open file {:?} \nerror: {:?}", manifest_path, e))
        .map(BufReader::new)
        .map(serde_json::from_reader)?
        .map_err(|e| anyhow!("json parsing errored {}", e))?;
    let product_bundle =
        images.0.iter().find(|i| i.name == "product_bundle").map(|i| i.path.clone());
    if let Some(pb) = product_bundle {
        path.push(pb);
    } else {
        ffx_bail!("Could not find the Product Bundle in the SDK. Update your SDK and retry");
    }
    let mut entries = Entries::new();
    let file = File::open(path)?;
    entries.add_json(&mut BufReader::new(file))?;
    match entries.iter().next() {
        Some(Metadata::ProductBundleV1(bundle)) => writeln!(writer, "{}", bundle.name)?,
        _ => {}
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
