// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context, Error},
    fidl_fuchsia_component as fcomponent, fidl_fuchsia_component_decl as fdecl,
    fuchsia_component::client,
    futures::future,
    tracing::*,
};

/// Name of the collection that contains BankAccount service providers.
const ACCOUNT_PROVIDERS_COLLECTION: &str = "account_providers";

#[fuchsia::component]
async fn main() -> Result<(), Error> {
    // Create two BankAccount providers into the `account_providers` collection.
    // The providers are not eagerly started. The test should start them as needed.
    let realm = client::connect_to_protocol::<fcomponent::RealmMarker>()
        .context("connect to Realm service")?;
    create_provider(&realm, "a", "#meta/provider-a.cm").await?;
    create_provider(&realm, "b", "#meta/provider-b.cm").await?;

    // Wait indefinitely to keep this component running.
    future::pending::<()>().await;

    unreachable!();
}

/// Creates a BankAccount provider component in `ACCOUNT_PROVIDERS_COLLECTION`.
///
/// This does not start the component.
async fn create_provider(
    realm: &fcomponent::RealmProxy,
    name: &str,
    url: &str,
) -> Result<(), Error> {
    info!("creating BankAccount provider \"{}\" with url={}", name, url);
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
            fcomponent::CreateChildArgs::EMPTY,
        )
        .await
        .context("failed to call CreateChild")?
        .map_err(|e| format_err!("Failed to create child: {:?}", e))?;

    Ok(())
}
