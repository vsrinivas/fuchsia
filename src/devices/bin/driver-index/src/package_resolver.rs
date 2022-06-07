// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Context;
use futures::TryStreamExt;
use {
    fidl_fuchsia_io as fio,
    fidl_fuchsia_pkg::PackageResolverRequestStream,
    fuchsia_url::{PackageVariant, UnpinnedAbsolutePackageUrl},
    futures::StreamExt,
};
pub async fn serve(stream: PackageResolverRequestStream) -> anyhow::Result<()> {
    stream
        .map(|result| result.context("failed request"))
        .try_for_each(|request| async {
            match request {
                fidl_fuchsia_pkg::PackageResolverRequest::Resolve {
                    package_url,
                    dir,
                    responder,
                } => {
                    let package_url = UnpinnedAbsolutePackageUrl::parse(&package_url)?;
                    let flags = fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::DIRECTORY;
                    fuchsia_fs::node::connect_in_namespace(
                        &format!(
                            "/pkgfs/packages/{}/{}",
                            package_url.name(),
                            package_url
                                .variant()
                                .cloned()
                                .unwrap_or_else(|| PackageVariant::zero())
                        ),
                        flags,
                        dir.into_channel(),
                    )?;
                    responder.send(&mut Ok(())).context("error sending response")?;
                }
                fidl_fuchsia_pkg::PackageResolverRequest::GetHash { package_url: _, responder } => {
                    responder
                        .send(&mut Err(fuchsia_zircon::sys::ZX_ERR_UNAVAILABLE))
                        .context("error sending response")?;
                }
            }
            Ok(())
        })
        .await?;
    Ok(())
}
