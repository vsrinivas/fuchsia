// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context, Error},
    fidl_fuchsia_component_resolution as fresolution, fidl_fuchsia_io as fio,
    fidl_fuchsia_test_internal as ftest_internal, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_protocol,
    fuchsia_component::server::ServiceFs,
    futures::StreamExt,
    std::{
        convert::{TryFrom, TryInto},
        sync::Arc,
    },
    test_manager_lib::AboveRootCapabilitiesForTest,
    tracing::{info, warn},
};

const DEFAULT_MANIFEST_NAME: &str = "test_manager.cm";
const DATA_FOR_TEST_DIR_NAME: &str = "data_for_test";
const DATA_FOR_TESTS_PATH: &str = "/data/data_for_test";

/// Arguments passed to test manager.
struct TestManagerArgs {
    /// optional positional argument that specifies an override for the name of the manifest.
    manifest_name: Option<String>,
}

impl TryFrom<std::env::Args> for TestManagerArgs {
    type Error = Error;
    fn try_from(args: std::env::Args) -> Result<Self, Self::Error> {
        let mut args_vec: Vec<_> = args.collect();
        match args_vec.len() {
            1 => Ok(Self { manifest_name: None }),
            2 => Ok(Self { manifest_name: args_vec.pop() }),
            _ => anyhow::bail!("Unexpected number of arguments: {:?}", args_vec),
        }
    }
}

impl TestManagerArgs {
    pub fn manifest_name(&self) -> &str {
        self.manifest_name.as_ref().map(String::as_str).unwrap_or(DEFAULT_MANIFEST_NAME)
    }
}

#[fuchsia::main]
async fn main() -> Result<(), Error> {
    info!("started");
    let args: TestManagerArgs = std::env::args().try_into()?;
    let mut fs = ServiceFs::new();

    if !std::path::Path::new("/tmp").exists() {
        return Err(anyhow!("Expected /tmp to be available"));
    }

    if !std::path::Path::new("/data").exists() {
        return Err(anyhow!("Expected /data to be available"));
    }

    inspect_runtime::serve(fuchsia_inspect::component::inspector(), &mut fs)?;

    info!("Reading capabilities from {}", args.manifest_name());
    let routing_info = Arc::new(AboveRootCapabilitiesForTest::new(args.manifest_name()).await?);
    let routing_info_clone = routing_info.clone();
    let resolver = Arc::new(
        connect_to_protocol::<fresolution::ResolverMarker>()
            .expect("Cannot connect to component resolver"),
    );
    let debug_data_controller = Arc::new(
        connect_to_protocol::<ftest_internal::DebugDataControllerMarker>()
            .expect("Cannnot connect to debug data control"),
    );
    let resolver_clone = resolver.clone();
    let root_inspect = Arc::new(test_manager_lib::RootInspectNode::new(
        fuchsia_inspect::component::inspector().root(),
    ));

    // Delete data from previous run if leftover due to deletion failure.
    if std::path::Path::new(DATA_FOR_TESTS_PATH).exists() {
        std::fs::remove_dir_all(DATA_FOR_TESTS_PATH).context("Removing data for test directory")?;
    }

    std::fs::create_dir(DATA_FOR_TESTS_PATH).context("Create data for test directory")?;
    let data_directory = fuchsia_fs::directory::open_in_namespace(
        DATA_FOR_TESTS_PATH,
        fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
    )?;

    fs.add_remote(DATA_FOR_TEST_DIR_NAME, data_directory);

    fs.dir("svc")
        .add_fidl_service(move |stream| {
            let routing_info_for_task = routing_info_clone.clone();
            let resolver = resolver.clone();
            let debug_data_controller = debug_data_controller.clone();
            let root_inspect_clone = root_inspect.clone();
            fasync::Task::local(async move {
                test_manager_lib::run_test_manager(
                    stream,
                    resolver,
                    debug_data_controller,
                    routing_info_for_task,
                    &*root_inspect_clone,
                )
                .await
                .unwrap_or_else(|error| warn!(?error, "test manager returned error"))
            })
            .detach();
        })
        .add_fidl_service(move |stream| {
            let routing_info_for_task = routing_info.clone();
            let resolver = resolver_clone.clone();

            fasync::Task::spawn(async move {
                test_manager_lib::run_test_manager_query_server(
                    stream,
                    resolver,
                    routing_info_for_task,
                )
                .await
                .unwrap_or_else(|error| warn!(?error, "test manager returned error"))
            })
            .detach();
        });
    fs.take_and_serve_directory_handle()?;
    fs.collect::<()>().await;
    Ok(())
}
