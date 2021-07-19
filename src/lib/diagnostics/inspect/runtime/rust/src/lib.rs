// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

//! # Inspect Runtime
//!
//! This library contains the necessary functions to serve inspect from a component.

use fidl::endpoints::DiscoverableService;
use fidl_fuchsia_inspect::TreeMarker;
use fidl_fuchsia_io::{DirectoryMarker, OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE};
use fuchsia_component::server::{ServiceFs, ServiceObjTrait};
use fuchsia_inspect::{Error, Inspector};
use futures::prelude::*;
use tracing::error;
use vfs::{
    directory::entry::DirectoryEntry, execution_scope::ExecutionScope, path::Path,
    pseudo_directory, service as pseudo_fs_service,
};

pub mod service;

/// Directiory within the outgoing directory of a component where the diagnostics service should be
/// added.
pub const DIAGNOSTICS_DIR: &str = "diagnostics";

/// Spawns a server for handling `fuchsia.inspect.Tree` requests in the outgoing diagnostics
/// directory.
pub fn serve<'a, ServiceObjTy: ServiceObjTrait>(
    inspector: &Inspector,
    service_fs: &mut ServiceFs<ServiceObjTy>,
) -> Result<(), Error> {
    let (proxy, server) =
        fidl::endpoints::create_proxy::<DirectoryMarker>().map_err(|e| Error::fidl(e.into()))?;
    let inspector_for_fs = inspector.clone();
    let dir = pseudo_directory! {
        TreeMarker::SERVICE_NAME => pseudo_fs_service::host(move |stream| {
            let inspector = inspector_for_fs.clone();
            async move {
                service::handle_request_stream(
                    inspector, service::TreeServerSettings::default(), stream
                    )
                    .await
                    .unwrap_or_else(|e| error!("failed to run server: {:?}", e));
            }
            .boxed()
        }),
    };

    let server_end = server.into_channel().into();
    let scope = ExecutionScope::new();
    dir.open(scope, OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE, 0, Path::dot(), server_end);
    service_fs.add_remote(DIAGNOSTICS_DIR, proxy);

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use fdio;
    use fuchsia_component::server::ServiceObj;
    use fuchsia_component_test::ScopedInstance;
    use fuchsia_inspect::{assert_inspect_tree, reader, Inspector};
    use glob::glob;

    const TEST_COMPONENT_URL: &str =
        "fuchsia-pkg://fuchsia.com/inspect-runtime-tests#meta/inspect_test_component.cm";

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
        let app = ScopedInstance::new("coll".to_string(), TEST_COMPONENT_URL.to_string())
            .await
            .expect("failed to create test component");

        let pattern = format!(
            "/hub/children/coll:{}/exec/out/diagnostics/{}",
            app.child_name(),
            TreeMarker::SERVICE_NAME
        );
        let path = glob(&pattern)?.next().unwrap().expect("failed to parse glob");
        let (tree, server_end) =
            fidl::endpoints::create_proxy::<TreeMarker>().expect("failed to create proxy");
        fdio::service_connect(&path.to_string_lossy().to_string(), server_end.into_channel())
            .expect("failed to connect to service");

        let hierarchy = reader::read(&tree).await?;
        assert_inspect_tree!(hierarchy, root: {
            int: 3i64,
            "lazy-node": {
                a: "test",
                child: {
                    double: 3.14,
                },
            }
        });

        Ok(())
    }
}
