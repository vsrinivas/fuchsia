// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

//! # Inspect Runtime
//!
//! This library contains the necessary functions to serve inspect from a component.

use fidl::endpoints::DiscoverableProtocolMarker;
use fidl_fuchsia_inspect::TreeMarker;
use fidl_fuchsia_io as fio;
use fuchsia_component::server::{ServiceFs, ServiceObjTrait};
use fuchsia_inspect::{Error, Inspector};
use futures::prelude::*;
use std::sync::Arc;
use tracing::warn;
use vfs::{
    directory::entry::DirectoryEntry, execution_scope::ExecutionScope, path::Path, pseudo_directory,
};

pub mod service;

/// Directory within the outgoing directory of a component where the diagnostics service should be
/// added.
pub const DIAGNOSTICS_DIR: &str = "diagnostics";

/// Spawns a server with options for handling `fuchsia.inspect.Tree` requests in
/// the outgoing diagnostics directory.
pub fn serve_with_options<'a, ServiceObjTy: ServiceObjTrait>(
    inspector: &Inspector,
    options: service::TreeServerSettings,
    service_fs: &mut ServiceFs<ServiceObjTy>,
) -> Result<(), Error> {
    let (proxy, server) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>()
        .map_err(|e| Error::fidl(e.into()))?;
    let dir = create_diagnostics_dir_with_options(inspector.clone(), options);
    let server_end = server.into_channel().into();
    let scope = ExecutionScope::new();
    dir.open(
        scope,
        fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        0,
        Path::dot(),
        server_end,
    );
    service_fs.add_remote(DIAGNOSTICS_DIR, proxy);

    Ok(())
}

/// Spawns a server for handling `fuchsia.inspect.Tree` requests in the outgoing diagnostics
/// directory.
pub fn serve<'a, ServiceObjTy: ServiceObjTrait>(
    inspector: &Inspector,
    service_fs: &mut ServiceFs<ServiceObjTy>,
) -> Result<(), Error> {
    serve_with_options(inspector, service::TreeServerSettings::default(), service_fs)
}

/// Creates the outgoing diagnostics directory with options. Should be added to the component's
/// outgoing directory at `DIAGNOSTICS_DIR`. Use `serve_with_options` if the component's outgoing
/// directory is served by `ServiceFs`.
pub fn create_diagnostics_dir_with_options(
    inspector: fuchsia_inspect::Inspector,
    options: service::TreeServerSettings,
) -> Arc<dyn DirectoryEntry> {
    pseudo_directory! {
        TreeMarker::PROTOCOL_NAME =>
            vfs::service::host(move |stream| {
                service::handle_request_stream(
                    inspector.clone(),
                    options.clone(),
                    stream
                ).unwrap_or_else(|e| {
                    warn!(
                        "error handling fuchsia.inspect/Tree connection: {e:#}"
                    );
                })
            }),
    }
}

/// Creates the outgoing diagnostics directory. Should be added to the component's outgoing
/// directory at `DIAGNOSTICS_DIR`. Use `serve` if the component's outgoing directory is served by
/// `ServiceFs`.
pub fn create_diagnostics_dir(inspector: Inspector) -> Arc<dyn DirectoryEntry> {
    create_diagnostics_dir_with_options(inspector, service::TreeServerSettings::default())
}

#[cfg(test)]
mod tests {
    use super::*;
    use component_events::{
        events::{EventStream, Started},
        matcher::EventMatcher,
    };
    use fidl_fuchsia_sys2 as fsys;
    use fuchsia_component::{client, server::ServiceObj};
    use fuchsia_component_test::ScopedInstance;
    use fuchsia_inspect::{reader, testing::assert_data_tree, Inspector};

    const TEST_COMPONENT_URL: &str = "#meta/inspect_test_component.cm";

    #[fuchsia::test]
    async fn new_no_op() -> Result<(), Error> {
        let mut fs: ServiceFs<ServiceObj<'_, ()>> = ServiceFs::new();

        let inspector = Inspector::new_no_op();
        assert!(!inspector.is_valid());

        // Ensure serve doesn't crash on a No-Op inspector
        serve(&inspector, &mut fs)
    }

    #[fuchsia::test]
    async fn connect_to_service() -> Result<(), anyhow::Error> {
        let mut event_stream = EventStream::open().await.unwrap();

        let app = ScopedInstance::new("coll".to_string(), TEST_COMPONENT_URL.to_string())
            .await
            .expect("failed to create test component");

        let started_stream = EventMatcher::ok()
            .moniker_regex(app.child_name().to_owned())
            .wait::<Started>(&mut event_stream);

        app.connect_to_binder().expect("failed to connect to Binder protocol");

        started_stream.await.expect("failed to observe Started event");

        let realm_query = client::connect_to_protocol::<fsys::RealmQueryMarker>().unwrap();
        let (_, maybe_resolved) = realm_query
            .get_instance_info(&format!("./coll:{}", app.child_name()))
            .await
            .expect("fidl call succeeds")
            .expect("the component exists");
        let resolved = maybe_resolved.expect("the instance is resolved");
        let execution = resolved.execution.expect("the instance has an execution state");

        let out_dir = execution.out_dir.expect("out dir exists");
        let out_dir = out_dir.into_proxy().expect("dir into proxy");
        let diagnostics_dir = fuchsia_fs::directory::open_directory(
            &out_dir,
            "diagnostics",
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        )
        .await
        .expect("opened diagnostics");

        let tree = client::connect_to_protocol_at_dir_root::<TreeMarker>(&diagnostics_dir)
            .expect("connected to tree");

        let hierarchy = reader::read(&tree).await?;
        assert_data_tree!(hierarchy, root: {
            int: 3i64,
            "lazy-node": {
                a: "test",
                child: {
                    double: 3.25,
                },
            }
        });

        Ok(())
    }
}
