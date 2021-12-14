// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context, Error},
    fidl_fuchsia_component as fcomponent, fidl_fuchsia_component_decl as fdecl,
    fidl_fuchsia_io as fio,
    fuchsia_component::client,
    futures::future,
    tracing::*,
};

/// Name of the collection that contains BankAccount service providers.
const ACCOUNT_PROVIDERS_COLLECTION: &str = "account_providers";

#[fuchsia::component]
async fn main() -> Result<(), Error> {
    // Launch two BankAccount providers into the `account_providers` collection.
    let realm = client::connect_to_protocol::<fcomponent::RealmMarker>()
        .context("connect to Realm service")?;
    start_provider(&realm, "a", "#meta/provider-a.cm").await?;
    start_provider(&realm, "b", "#meta/provider-b.cm").await?;

    // Wait indefinitely to keep this component running.
    future::pending::<()>().await;

    unreachable!();
}

/// Starts a BankAccount provider component in `ACCOUNT_PROVIDERS_COLLECTION`.
async fn start_provider(
    realm: &fcomponent::RealmProxy,
    name: &str,
    url: &str,
) -> Result<(), Error> {
    info!("creating BankAccount provider \"{}\" with url={}", name, url);
    let child_args = fcomponent::CreateChildArgs {
        numbered_handles: None,
        ..fcomponent::CreateChildArgs::EMPTY
    };
    realm
        .create_child(
            &mut fdecl::CollectionRef { name: ACCOUNT_PROVIDERS_COLLECTION.to_string() },
            fdecl::Child {
                name: Some(name.to_string()),
                url: Some(url.to_string()),
                startup: Some(fdecl::StartupMode::Lazy),
                environment: None,
                ..fdecl::Child::EMPTY
            },
            child_args,
        )
        .await
        .context("failed to call CreateChild")?
        .map_err(|e| format_err!("Failed to create child: {:?}", e))?;

    let (exposed_dir, exposed_dir_server_end) =
        fidl::endpoints::create_proxy::<fio::DirectoryMarker>()
            .context("failed to create Directory endpoints")?;
    info!("open exposed dir of BankAccount provider \"{}\" with url={}", name, url);
    realm
        .open_exposed_dir(
            &mut fdecl::ChildRef {
                name: name.to_string(),
                collection: Some(ACCOUNT_PROVIDERS_COLLECTION.to_string()),
            },
            exposed_dir_server_end,
        )
        .await
        .context("failed to call OpenExposedDir")?
        .map_err(|e| format_err!("Failed to open exposed dir: {:?}", e))?;

    // Connect to Binder to launch the component.
    let _ = client::connect_to_protocol_at_dir_root::<fcomponent::BinderMarker>(&exposed_dir)
        .context("failed to connect to fuchsia.component.Binder")?;

    Ok(())
}
