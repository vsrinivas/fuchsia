// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod mock_cr50_agent;
mod pinweaver;
mod scrypt;

use {
    anyhow::{anyhow, Error},
    fdio as _,
    fidl_fuchsia_hardware_block_partition::Guid,
    fidl_fuchsia_identity_account::{AccountManagerMarker, AccountManagerProxy, AccountProxy},
    fidl_fuchsia_identity_credential::{ManagerMarker, ManagerProxy},
    fidl_fuchsia_tpm_cr50::PinWeaverMarker,
    fuchsia_async::{DurationExt as _, TimeoutExt as _},
    fuchsia_component_test::LocalComponentHandles,
    fuchsia_component_test::{Capability, ChildOptions, RealmBuilder, RealmInstance, Ref, Route},
    fuchsia_driver_test::{DriverTestRealmBuilder, DriverTestRealmInstance},
    fuchsia_zircon::{self as zx, sys::zx_status_t},
    futures::{FutureExt as _, StreamExt as _},
    std::collections::VecDeque,
    std::os::raw::c_int,
};

use crate::mock_cr50_agent::{mock, MockResponse};

// Canonically defined in //zircon/system/public/zircon/hw/gpt.h
const FUCHSIA_DATA_GUID_VALUE: [u8; 16] = [
    // 08185F0C-892D-428A-A789-DBEEC8F55E6A
    0x0c, 0x5f, 0x18, 0x08, 0x2d, 0x89, 0x8a, 0x42, 0xa7, 0x89, 0xdb, 0xee, 0xc8, 0xf5, 0x5e, 0x6a,
];
const FUCHSIA_DATA_GUID: Guid = Guid { value: FUCHSIA_DATA_GUID_VALUE };
const ACCOUNT_LABEL: &str = "account";

// The maximum time to wait for an account channel to close after the account is locked.
const ACCOUNT_CLOSE_TIMEOUT: zx::Duration = zx::Duration::from_seconds(5);

const GLOBAL_ACCOUNT_ID: u64 = 1;
const EMPTY_PASSWORD: &'static str = "";
const REAL_PASSWORD: &'static str = "a real passphrase!";
const BAD_PASSWORD: &'static str = "not the real passphrase :(";

#[link(name = "fs-management")]
extern "C" {
    pub fn fvm_init(fd: c_int, slice_size: usize) -> zx_status_t;
}

enum Config {
    PinweaverOrScrypt,
    ScryptOnly,
}

struct TestEnv {
    pub realm_instance: RealmInstance,
}

impl TestEnv {
    async fn build(config: Config) -> TestEnv {
        TestEnv::build_with_cr50_mock(config, None).await
    }

    async fn build_with_cr50_mock(
        config: Config,
        maybe_mock_cr50: Option<VecDeque<MockResponse>>,
    ) -> TestEnv {
        let builder = RealmBuilder::new().await.unwrap();
        builder.driver_test_realm_setup().await.unwrap();
        let password_authenticator = builder
            .add_child(
                "password_authenticator",
                "#meta/password-authenticator.cm",
                ChildOptions::new(),
            )
            .await
            .unwrap();
        let (allow_scrypt, allow_pinweaver) = match config {
            Config::PinweaverOrScrypt => (true, true),
            Config::ScryptOnly => (true, false),
        };
        builder.init_mutable_config_to_empty(&password_authenticator).await.unwrap();
        builder
            .set_config_value_bool(&password_authenticator, "allow_scrypt", allow_scrypt)
            .await
            .unwrap();
        builder
            .set_config_value_bool(&password_authenticator, "allow_pinweaver", allow_pinweaver)
            .await
            .unwrap();
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                    .capability(Capability::protocol_by_name("fuchsia.process.Launcher"))
                    .capability(Capability::storage("data"))
                    .from(Ref::parent())
                    .to(&password_authenticator),
            )
            .await
            .unwrap();

        let credential_manager = builder
            .add_child("credential_manager", "fuchsia-pkg://fuchsia.com/password-authenticator-integration-tests#meta/credential-manager.cm", ChildOptions::new()).await.unwrap();
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                    .capability(Capability::protocol_by_name("fuchsia.process.Launcher"))
                    .capability(Capability::storage("data"))
                    .from(Ref::parent())
                    .to(&credential_manager),
            )
            .await
            .unwrap();

        // Expose CredentialManager to PasswordAuthenticator.
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol::<ManagerMarker>())
                    .from(&credential_manager)
                    .to(&password_authenticator),
            )
            .await
            .unwrap();

        // Expose CredentialManager so we can manually modify hash tree state for tests.
        // See [`test_pinweaver_unknown_label`]
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol::<ManagerMarker>())
                    .from(&credential_manager)
                    .to(Ref::parent()),
            )
            .await
            .unwrap();

        // Set up mock Cr50Agent.
        let mocks = maybe_mock_cr50.unwrap_or(VecDeque::new());
        let cr50 = builder
            .add_local_child(
                "mock_cr50",
                move |handles: LocalComponentHandles| Box::pin(mock(mocks.clone(), handles)),
                ChildOptions::new(),
            )
            .await
            .unwrap();

        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol::<PinWeaverMarker>())
                    .from(&cr50)
                    .to(&credential_manager),
            )
            .await
            .unwrap();

        // Expose AccountManager so we can test it
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol::<AccountManagerMarker>())
                    .from(&password_authenticator)
                    .to(Ref::parent()),
            )
            .await
            .unwrap();

        // Offer /dev from DriverTestrealm to password_authenticator, which makes use of it.
        builder
            .add_route(
                Route::new()
                    .capability(Capability::directory("dev-topological"))
                    .from(Ref::child("driver_test_realm"))
                    .to(&password_authenticator),
            )
            .await
            .unwrap();

        let realm_instance = builder.build().await.unwrap();
        let args = fidl_fuchsia_driver_test::RealmArgs {
            root_driver: Some("fuchsia-boot:///#driver/platform-bus.so".to_string()),
            ..fidl_fuchsia_driver_test::RealmArgs::EMPTY
        };
        realm_instance.driver_test_realm_start(args).await.unwrap();

        TestEnv { realm_instance }
    }

    pub fn account_manager(&self) -> AccountManagerProxy {
        self.realm_instance
            .root
            .connect_to_protocol_at_exposed_dir::<AccountManagerMarker>()
            .expect("connect to account manager")
    }

    pub fn credential_manager(&self) -> ManagerProxy {
        self.realm_instance
            .root
            .connect_to_protocol_at_exposed_dir::<ManagerMarker>()
            .expect("connect to credential manager")
    }
}

/// Waits up to ACCOUNT_CLOSE_TIMEOUT for the supplied account to close.
async fn wait_for_account_close(account: &AccountProxy) -> Result<(), Error> {
    account
        .take_event_stream()
        .for_each(|_| async move {}) // Drain all remaining events
        .map(|_| Ok(())) // Completing the drain results in ok
        .on_timeout(ACCOUNT_CLOSE_TIMEOUT.after_now(), || {
            Err(anyhow!("Account close timeout exceeded"))
        })
        .await
}
