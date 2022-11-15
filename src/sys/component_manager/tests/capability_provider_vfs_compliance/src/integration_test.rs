// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use assert_matches::assert_matches;
use fidl_fuchsia_io as fio;
use fuchsia_async as fasync;
use fuchsia_async::TimeoutExt;
use fuchsia_fs::node::OpenError;
use fuchsia_zircon_status as zx_status;
use futures::StreamExt;

#[fasync::run_singlethreaded(test)]
async fn component_manager_namespace() {
    let non_hanging_nodes = [
        "/svc/fuchsia.sys2.LifecycleController",
        "/svc/fuchsia.sys2.RouteValidator",
        "/svc/fuchsia.sys2.RealmExplorer",
        "/svc/fuchsia.sys2.StorageAdmin",
    ];

    let nonhanging_failed_opens = non_hanging_nodes.iter().map(|node_path| async move {
        assert_matches!(
            validate_open_with_node_reference_and_describe(node_path).await,
            Err(OpenError::OnOpenEventStreamClosed),
            "Opening capability: {} with DESCRIBE|NODE_REFERENCE did not produce closed stream.",
            node_path
        );
    });

    let () = futures::future::join_all(nonhanging_failed_opens).await.into_iter().collect();

    let hanging_nodes = [
        "/svc/fuchsia.boot.Arguments",
        "/svc/fuchsia.component.Binder",
        "/svc/fuchsia.component.Realm",
        "/svc/fuchsia.boot.FactoryItems",
        "/svc/fuchsia.boot.Items",
        "/svc/fuchsia.boot.ReadOnlyLog",
        "/svc/fuchsia.boot.WriteOnlyLog",
        "/svc/fuchsia.kernel.Stats",
        "/svc/fuchsia.logger.LogSink",
        "/svc/fuchsia.process.Launcher",
        "/svc/fuchsia.sys2.CrashIntrospect",
        "/svc/fuchsia.sys2.CrashIntrospect",
    ];

    let hanging_failed_opens = hanging_nodes.iter().map(|node_path| async move {
        let hanging_err = validate_open_with_node_reference_and_describe(node_path)
            .on_timeout(std::time::Duration::from_secs(2), || {
                Err(OpenError::OpenError(zx_status::Status::TIMED_OUT))
            })
            .await;

        assert_matches!(
            hanging_err,
            Err(OpenError::OpenError(zx_status::Status::TIMED_OUT)),
            "Opening capability: {} with DESCRIBE|NODE_REFERENCE did not timeout.",
            node_path
        );
    });

    let () = futures::future::join_all(hanging_failed_opens).await.into_iter().collect();

    // Security checks prevent access to the below protocols, although they are still
    // hosted in component manager.
    // TODO(https://fxbug.dev/104365): Give this test permission to access these
    // resources as we expand io compliance enforcement in CM.
    // let _hanging_resources = [
    //     "/svc/fuchsia.kernel.RootJob",
    //     "/svc/fuchsia.kernel.RootJobForInspect",
    //     "/svc/fuchsia.boot.RootResource",
    //     "/svc/fuchsia.kernel.CpuResource",
    //     "/svc/fuchsia.kernel.DebugResource",
    //     "/svc/fuchsia.kernel.HypervisorResource",
    //     "/svc/fuchsia.kernel.InfoResource",
    //     "/svc/fuchsia.kernel.IoportResource",
    //     "/svc/fuchsia.kernel.IrqResource",
    //     "/svc/fuchsia.kernel.MmioResource",
    //     "/svc/fuchsia.kernel.PowerResource",
    //     "/svc/fuchsia.kernel.SmcResource",
    //     "/svc/fuchsia.kernel.VmexResource",
    // ];
}

async fn validate_open_with_node_reference_and_describe(path: &str) -> Result<(), OpenError> {
    // The Rust VFS defines the only valid call for DESCRIBE on a service node to be one
    // that includes the NODE_REFERENCE flag. Component framework aims to adhere to the rust
    // VFS implementation of the io protocol.
    // TODO(https://fxbug.dev/104406): If the rust VFS interpretation of the DESCRIBE
    // flag behavior on service nodes is incorrect, update this call.
    let node = fuchsia_fs::node::open_in_namespace(
        path,
        fio::OpenFlags::DESCRIBE | fio::OpenFlags::NODE_REFERENCE,
    )?;

    let mut events = node.take_event_stream();

    match events
        .next()
        .await
        .ok_or(OpenError::OnOpenEventStreamClosed)?
        .map_err(OpenError::OnOpenDecode)?
    {
        fio::NodeEvent::OnOpen_ { s: status, info } => {
            let () = zx_status::Status::ok(status).map_err(OpenError::OpenError)?;
            info.ok_or(OpenError::MissingOnOpenInfo)?;
        }
        event @ fio::NodeEvent::OnRepresentation { payload: _ } => {
            panic!("Compliance test got unexpected event: {:?}", event)
        }
    }

    Ok(())
}
