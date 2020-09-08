// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    component_manager_lib::{
        builtin_environment::BuiltinEnvironmentBuilder,
        model::{
            binding::Binder, moniker::AbsoluteMoniker, realm::BindReason, testing::test_helpers,
        },
        startup,
    },
    fidl::endpoints::ServiceMarker,
    fidl_fidl_test_components as ftest,
    fidl_fuchsia_io::{OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE},
    fuchsia_component::server::{ServiceFs, ServiceObj},
    fuchsia_syslog as syslog,
    futures::prelude::*,
    log::*,
    std::{path::PathBuf, process},
};

/// Runs a component exposing the `fuchsia.component.test.Trigger` protocol, plumbing it through
/// `out/svc`.
#[fuchsia_async::run_singlethreaded]
async fn main() -> Result<(), Error> {
    syslog::init_with_tags(&["component_manager_for_rights_test"])?;
    let args = std::env::args().skip(1);
    let args = match startup::Arguments::new(args) {
        Ok(args) => args,
        Err(err) => {
            error!("Invalid arguments");
            return Err(err);
        }
    };

    // Set up environment.
    let builtin_environment = BuiltinEnvironmentBuilder::new()
        .set_args(args)
        .populate_config_from_args()
        .await?
        .add_elf_runner()?
        .add_available_resolvers_from_namespace()?
        .build()
        .await?;

    let hub_proxy = builtin_environment.bind_service_fs_for_hub().await?;

    let root_moniker = AbsoluteMoniker::root();
    match builtin_environment.model.bind(&root_moniker, &BindReason::Root).await {
        Ok(_) => {
            // TODO: Exit the component manager when the root component's binding is lost
            // (when it terminates).
        }
        Err(error) => {
            error!("Failed to bind to root component: {:?}", error);
            process::exit(1)
        }
    }

    // make sure root component exposes trigger protocol.
    let expose_dir_proxy = io_util::open_directory(
        &hub_proxy,
        &PathBuf::from("exec/expose"),
        OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
    )
    .expect("Failed to open directory");

    assert_eq!(
        vec![ftest::TriggerMarker::DEBUG_NAME],
        test_helpers::list_directory(&expose_dir_proxy).await
    );

    // bind expose/svc to out/svc of this v1 component.
    let mut fs = ServiceFs::<ServiceObj<'_, ()>>::new();
    fs.add_remote("svc", expose_dir_proxy);

    fs.take_and_serve_directory_handle()?;
    fs.collect::<()>().await;
    Ok(())
}
