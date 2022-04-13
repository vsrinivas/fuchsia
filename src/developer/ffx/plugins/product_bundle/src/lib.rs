// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A tool to:
//! - acquire and display product bundle information (metadata)
//! - acquire related data files, such as disk partition images (data)

use {
    anyhow::{Context, Result},
    ffx_core::ffx_plugin,
    ffx_product_bundle_args::{
        CreateCommand, GetCommand, ListCommand, ProductBundleCommand, SubCommand,
    },
    pbms::{get_pbms, get_product_data, is_pb_ready},
    sdk_metadata::Metadata,
    std::io::{stdout, Write},
};

mod create;

/// Provide functionality to list product-bundle metadata, fetch metadata, and
/// pull images and related data.
#[ffx_plugin()]
pub async fn product_bundle(cmd: ProductBundleCommand) -> Result<()> {
    product_bundle_plugin_impl(cmd, &mut stdout()).await
}

/// Dispatch to a sub-command.
pub async fn product_bundle_plugin_impl<W>(
    command: ProductBundleCommand,
    writer: &mut W,
) -> Result<()>
where
    W: Write + Sync,
{
    match &command.sub {
        SubCommand::List(cmd) => pb_list(writer, &cmd).await?,
        SubCommand::Get(cmd) => pb_get(writer, &cmd).await?,
        SubCommand::Create(cmd) => pb_create(&cmd).await?,
    }
    Ok(())
}

/// `ffx product-bundle list` sub-command.
async fn pb_list<W: Write + Sync>(writer: &mut W, cmd: &ListCommand) -> Result<()> {
    let entries = get_pbms(/*update_metadata=*/ !cmd.cached, /*verbose=*/ false, writer)
        .await
        .context("list pbms")?;
    let mut entry_names = entries
        .iter()
        .filter(|entry| match entry {
            Metadata::ProductBundleV1(_) => true,
            _ => false,
        })
        .map(|entry| entry.name().to_owned())
        .collect::<Vec<_>>();
    entry_names.sort();
    writeln!(writer, "")?;
    for entry in entry_names {
        let ready = if is_pb_ready(&entry).await? { "*" } else { "" };
        writeln!(writer, "{}{}", entry, ready)?;
    }
    writeln!(
        writer,
        "\
        \n*No need to fetch with `ffx product-bundle get ...`. \
        The '*' is not part of the name.\
        \n"
    )?;
    Ok(())
}

/// `ffx product-bundle get` sub-command.
async fn pb_get<W: Write + Sync>(writer: &mut W, cmd: &GetCommand) -> Result<()> {
    get_product_data(&cmd.product_bundle_name, cmd.verbose, writer).await
}

/// `ffx product-bundle create` sub-command.
async fn pb_create(cmd: &CreateCommand) -> Result<()> {
    create::create_product_bundle(cmd).await
}

#[cfg(test)]
mod test {}
