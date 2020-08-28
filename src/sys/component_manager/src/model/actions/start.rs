// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{
        error::ModelError,
        exposed_dir::ExposedDir,
        hooks::{Event, EventError, EventErrorPayload, EventPayload, RuntimeInfo},
        moniker::AbsoluteMoniker,
        namespace::IncomingNamespace,
        realm::{BindReason, ExecutionState, Realm, Runtime, WeakRealm},
        runner::Runner,
    },
    cm_rust::data,
    fidl::endpoints::{self, Proxy, ServerEnd},
    fidl_fuchsia_component_runner as fcrunner,
    fidl_fuchsia_io::DirectoryProxy,
    fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync, fuchsia_zircon as zx,
    log::*,
    std::sync::Arc,
    vfs::execution_scope::ExecutionScope,
};

pub(super) async fn do_start(
    realm: &Arc<Realm>,
    bind_reason: &BindReason,
) -> Result<(), ModelError> {
    // Pre-flight check: if the component is already started, return now. Note that `bind_at` also
    // performs this check before scheduling the action; here, we do it again while the action is
    // registered so we avoid the risk of invoking the BeforeStart hook twice.
    {
        let execution = realm.lock_execution().await;
        if let Some(res) = should_return_early(&execution, &realm.abs_moniker) {
            return res;
        }
    }

    struct StartContext {
        component_decl: cm_rust::ComponentDecl,
        runner: Arc<dyn Runner>,
        pending_runtime: Runtime,
        start_info: fcrunner::ComponentStartInfo,
        controller_server_end: ServerEnd<fcrunner::ComponentControllerMarker>,
    }

    let result = async move {
        // Resolve the component.
        let component = realm.resolve().await?;

        // Find the runner to use.
        let runner = realm.resolve_runner().await.map_err(|e| {
            error!("Failed to resolve runner for `{}`: {}", realm.abs_moniker, e);
            e
        })?;

        // Generate the Runtime which will be set in the Execution.
        let (pending_runtime, start_info, controller_server_end) = make_execution_runtime(
            realm.as_weak(),
            component.resolved_url.clone(),
            component.package,
            &component.decl,
        )
        .await?;

        Ok(StartContext {
            component_decl: component.decl,
            runner,
            pending_runtime,
            start_info,
            controller_server_end,
        })
    }
    .await;

    let mut start_context = match result {
        Ok(start_context) => {
            let event = Event::new_with_timestamp(
                realm,
                Ok(EventPayload::Started {
                    runtime: RuntimeInfo::from_runtime(&start_context.pending_runtime),
                    component_decl: start_context.component_decl.clone(),
                    bind_reason: bind_reason.clone(),
                }),
                start_context.pending_runtime.timestamp,
            );

            realm.hooks.dispatch(&event).await?;
            start_context
        }
        Err(e) => {
            let event = Event::new(realm, Err(EventError::new(&e, EventErrorPayload::Started)));
            realm.hooks.dispatch(&event).await?;
            return Err(e);
        }
    };

    // Set the Runtime in the Execution. From component manager's perspective, this indicates
    // that the component has started. This may return early if the component is shut down.
    {
        let mut execution = realm.lock_execution().await;
        if let Some(res) = should_return_early(&execution, &realm.abs_moniker) {
            return res;
        }
        start_context.pending_runtime.watch_for_exit(realm.as_weak());
        execution.runtime = Some(start_context.pending_runtime);
    }

    // It's possible that the component is stopped before getting here. If so, that's fine: the
    // runner will start the component, but its stop or kill signal will be immediately set on the
    // component controller.
    start_context.runner.start(start_context.start_info, start_context.controller_server_end).await;

    Ok(())
}

/// Returns `Some(Result)` if `bind` should return early based on either of the following:
/// - The component instance is shut down.
/// - The component instance is already started.
pub fn should_return_early(
    execution: &ExecutionState,
    abs_moniker: &AbsoluteMoniker,
) -> Option<Result<(), ModelError>> {
    if execution.is_shut_down() {
        Some(Err(ModelError::instance_shut_down(abs_moniker.clone())))
    } else if execution.runtime.is_some() {
        Some(Ok(()))
    } else {
        None
    }
}

/// Returns a configured Runtime for a component and the start info (without actually starting
/// the component).
async fn make_execution_runtime(
    realm: WeakRealm,
    url: String,
    package: Option<fsys::Package>,
    decl: &cm_rust::ComponentDecl,
) -> Result<
    (Runtime, fcrunner::ComponentStartInfo, ServerEnd<fcrunner::ComponentControllerMarker>),
    ModelError,
> {
    // Create incoming/outgoing directories, and populate them.
    let exposed_dir = ExposedDir::new(ExecutionScope::new(), realm.clone(), decl.clone())?;
    let (outgoing_dir_client, outgoing_dir_server) =
        zx::Channel::create().map_err(|e| ModelError::namespace_creation_failed(e))?;
    let (runtime_dir_client, runtime_dir_server) =
        zx::Channel::create().map_err(|e| ModelError::namespace_creation_failed(e))?;
    let mut namespace = IncomingNamespace::new(package)?;
    let ns = namespace.populate(realm, decl).await?;

    let (controller_client, controller_server) =
        endpoints::create_endpoints::<fcrunner::ComponentControllerMarker>()
            .expect("could not create component controller endpoints");
    let controller =
        controller_client.into_proxy().expect("failed to create ComponentControllerProxy");
    // Set up channels into/out of the new component. These are absent from non-executable
    // components.
    let outgoing_dir_client = decl.get_used_runner().map(|_| {
        DirectoryProxy::from_channel(fasync::Channel::from_channel(outgoing_dir_client).unwrap())
    });
    let runtime_dir_client = decl.get_used_runner().map(|_| {
        DirectoryProxy::from_channel(fasync::Channel::from_channel(runtime_dir_client).unwrap())
    });
    let runtime = Runtime::start_from(
        url.clone(),
        Some(namespace),
        outgoing_dir_client,
        runtime_dir_client,
        exposed_dir,
        Some(controller),
    )?;
    let start_info = fcrunner::ComponentStartInfo {
        resolved_url: Some(url),
        program: data::clone_option_dictionary(&decl.program),
        ns: Some(ns),
        outgoing_dir: Some(ServerEnd::new(outgoing_dir_server)),
        runtime_dir: Some(ServerEnd::new(runtime_dir_server)),
    };

    Ok((runtime, start_info, controller_server))
}
