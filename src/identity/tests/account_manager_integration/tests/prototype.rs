// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fidl::endpoints::{create_proxy, ServiceMarker};
use fidl_fuchsia_identity_account::{
    AccountManagerMarker, AccountManagerProxy, Error as ApiError, Lifetime,
};
use fidl_fuchsia_identity_prototype::{
    PrototypeAccountTransferControlMarker, PrototypeAccountTransferControlProxy,
};
use fidl_fuchsia_overnet_protocol::NodeId;
use fuchsia_async as fasync;
use fuchsia_component::client::{launch, App};
use fuchsia_component::fuchsia_single_component_package_url;
use fuchsia_component::server::{NestedEnvironment, ServiceFs, ServiceObj};
use fuchsia_zircon as zx;
use futures::future::join;
use futures::prelude::*;
use lazy_static::lazy_static;
use std::ops::Deref;

lazy_static! {
    /// URL for the account manager component run as a system service.
    static ref DEFAULT_ACCOUNT_MANAGER_URL: String =
        String::from(fuchsia_single_component_package_url!("account_manager"));

    /// URL for account manager with prototype interfaces.
    static ref PROTOTYPE_ACCOUNT_MANAGER_URL: String =
        String::from(fuchsia_single_component_package_url!("account_manager_prototype"));

    /// Location where account transfer control interface is published.
    static ref PROTOTYPE_INTERFACE_DIR: String =
        format!("debug/{}", PrototypeAccountTransferControlMarker::NAME);

    /// Arguments passed to account manager started in test environment.
    static ref ACCOUNT_MANAGER_ARGS: Vec<String> = vec![String::from("--dev-auth-providers")];
}

/// Fake Node ID used for tests
const TEST_NODE_ID: NodeId = NodeId { id: 0xabcdu64 };
/// Fake local ID used for tests
const TEST_ACCOUNT_ID: u64 = 0x1234u64;

/// A proxy to a prototype account transfer control service exposed by an
/// account manager running in an enclosed environment.
struct NestedAccountTransferControlProxy {
    /// Proxy to the exposed prototype protocol.
    account_transfer_proxy: PrototypeAccountTransferControlProxy,

    /// Proxy to account manager.  Kept in scope to keep Account Manager alive.
    _account_manager_proxy: AccountManagerProxy,

    /// Application object for account manager.  Needs to be kept in scope to
    /// keep the nested environment alive.
    _app: App,

    /// The nested environment account manager is running in.  Needs to be kept
    /// in scope to keep the nested environment alive.
    _nested_envronment: NestedEnvironment,
}

impl Deref for NestedAccountTransferControlProxy {
    type Target = PrototypeAccountTransferControlProxy;

    fn deref(&self) -> &PrototypeAccountTransferControlProxy {
        &self.account_transfer_proxy
    }
}

/// Start account manager in an isolated environment and return a proxy to the
/// prototype account transfer control proxy it exposes and a future that
/// serves connection requests to account manager when polled.
fn create_account_manager_transfer(
) -> Result<(NestedAccountTransferControlProxy, impl Future<Output = Vec<()>>), Error> {
    let mut service_fs = ServiceFs::<ServiceObj<'_, ()>>::new();

    let nested_environment = service_fs.create_salted_nested_environment("account_test_env")?;

    let app = launch(
        nested_environment.launcher(),
        PROTOTYPE_ACCOUNT_MANAGER_URL.clone(),
        Some(ACCOUNT_MANAGER_ARGS.clone()),
    )?;

    let account_manager_proxy = app.connect_to_service::<AccountManagerMarker>()?;

    let (account_transfer_proxy, server) = create_proxy::<PrototypeAccountTransferControlMarker>()?;
    app.pass_to_named_service(&PROTOTYPE_INTERFACE_DIR, server.into_channel())?;

    Ok((
        NestedAccountTransferControlProxy {
            account_transfer_proxy,
            _account_manager_proxy: account_manager_proxy,
            _app: app,
            _nested_envronment: nested_environment,
        },
        service_fs.collect(),
    ))
}

/// Ensure that the default account manager component does not expose the
/// prototype interface.
#[fasync::run_singlethreaded(test)]
async fn test_prototype_interface_not_exposed() -> Result<(), Error> {
    let mut service_fs = ServiceFs::<ServiceObj<'_, ()>>::new();
    let nested_environment = service_fs.create_salted_nested_environment("account_test_env")?;
    let app = launch(
        nested_environment.launcher(),
        DEFAULT_ACCOUNT_MANAGER_URL.clone(),
        Some(ACCOUNT_MANAGER_ARGS.clone()),
    )?;

    let _account_manager_proxy = app.connect_to_service::<AccountManagerMarker>()?;
    let (transfer_proxy, server) = create_proxy::<PrototypeAccountTransferControlMarker>()?;
    app.pass_to_named_service(&PROTOTYPE_INTERFACE_DIR, server.into_channel())?;

    let test_fut = async move {
        match transfer_proxy
            .transfer_account(TEST_ACCOUNT_ID, &mut TEST_NODE_ID.clone(), Lifetime::Persistent)
            .await
            .unwrap_err()
        {
            fidl::Error::ClientChannelClosed(zx::Status::PEER_CLOSED) => (),
            e => panic!("Expected ClientChannelClosed error but got {:?}", e),
        }

        std::mem::drop(nested_environment);
    };

    let (_test_res, _service_res) = join(test_fut, service_fs.collect::<Vec<()>>()).await;
    Ok(())
}

/// A trivial test that ensures that we can connect to the exposed prototype
/// interface.  This test will likely be replaced once some functionality is
/// implemented.
#[fasync::run_singlethreaded(test)]
async fn test_connect_to_prototype_interface() -> Result<(), Error> {
    let (transfer_proxy, service_fut) = create_account_manager_transfer()?;

    let test_fut = async move {
        // FIDL connection should succeed, but account manager should return unimplemented.
        assert_eq!(
            transfer_proxy
                .transfer_account(TEST_ACCOUNT_ID, &mut TEST_NODE_ID.clone(), Lifetime::Persistent)
                .await?,
            Err(ApiError::UnsupportedOperation)
        );
        Result::<(), Error>::Ok(())
    };

    let (test_res, _service_res) = join(test_fut, service_fut).await;
    assert!(test_res.is_ok());
    Ok(())
}
