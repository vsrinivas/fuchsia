// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    fidl::endpoints::{create_endpoints, create_proxy, ProtocolMarker, ServerEnd},
    fidl_fuchsia_component_runner as fcrunner, fidl_fuchsia_data as fdata, fidl_fuchsia_io as fio,
    fidl_fuchsia_realm_builder as ftrb, fidl_fuchsia_sys as fsysv1, files_async,
    fuchsia_async as fasync,
    fuchsia_component::client::connect_to_protocol,
    futures::lock::Mutex,
    futures::TryStreamExt,
    log::*,
    rand::{self, Rng},
    std::{collections::HashMap, sync::Arc},
    vfs::{
        directory::entry::DirectoryEntry, directory::helper::DirectlyMutable,
        directory::immutable::simple as pfs, execution_scope::ExecutionScope,
        file::vmo::asynchronous::read_only_static, path::Path as VfsPath, pseudo_directory,
    },
};

pub const RUNNER_NAME: &'static str = "realm_builder";
pub const MOCK_ID_KEY: &'static str = "mock_id";
pub const LEGACY_URL_KEY: &'static str = "legacy_url";

#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub struct MockId(String);

impl From<MockId> for String {
    fn from(mock_id: MockId) -> Self {
        mock_id.0
    }
}

pub struct Runner {
    next_mock_id: Mutex<u64>,
    mocks: Mutex<HashMap<String, ftrb::FrameworkIntermediaryControlHandle>>,
}

impl Runner {
    pub fn new() -> Arc<Self> {
        Arc::new(Self { next_mock_id: Mutex::new(0), mocks: Mutex::new(HashMap::new()) })
    }

    pub async fn register_mock(
        self: &Arc<Self>,
        control_handle: ftrb::FrameworkIntermediaryControlHandle,
    ) -> MockId {
        let mut next_mock_id_guard = self.next_mock_id.lock().await;
        let mut mocks_guard = self.mocks.lock().await;

        let mock_id = format!("{}", *next_mock_id_guard);
        *next_mock_id_guard += 1;

        mocks_guard.insert(mock_id.clone(), control_handle);
        MockId(mock_id)
    }

    pub fn run_runner_service(self: &Arc<Self>, stream: fcrunner::ComponentRunnerRequestStream) {
        let self_ref = self.clone();
        fasync::Task::local(async move {
            if let Err(e) = self_ref.handle_runner_request_stream(stream).await {
                error!("error encountered while running runner service for mocks: {:?}", e);
            }
        })
        .detach();
    }

    async fn handle_runner_request_stream(
        self: &Arc<Self>,
        mut stream: fcrunner::ComponentRunnerRequestStream,
    ) -> Result<(), Error> {
        while let Some(req) = stream.try_next().await? {
            match req {
                fcrunner::ComponentRunnerRequest::Start { start_info, controller, .. } => {
                    let program =
                        start_info.program.clone().ok_or(format_err!("missing program"))?;
                    if start_info.ns.is_none() {
                        return Err(format_err!("missing namespace"));
                    }
                    if start_info.outgoing_dir.is_none() {
                        return Err(format_err!("missing outgoing_dir"));
                    }
                    if start_info.runtime_dir.is_none() {
                        return Err(format_err!("missing runtime_dir"));
                    }

                    match extract_mock_id_or_legacy_url(program)? {
                        MockIdOrLegacyUrl::MockId(mock_id) => {
                            self.launch_mock_component(mock_id, start_info, controller).await?;
                        }
                        MockIdOrLegacyUrl::LegacyUrl(legacy_url) => {
                            Self::launch_v1_component(legacy_url, start_info, controller).await?;
                        }
                    }
                }
            }
        }
        Ok(())
    }

    async fn launch_mock_component(
        self: &Arc<Self>,
        mock_id: String,
        start_info: fcrunner::ComponentStartInfo,
        controller: ServerEnd<fcrunner::ComponentControllerMarker>,
    ) -> Result<(), Error> {
        let mock_control_handle = {
            let mocks_guard = self.mocks.lock().await;
            mocks_guard.get(&mock_id).ok_or(format_err!("no such mock: {:?}", mock_id))?.clone()
        };

        mock_control_handle.send_on_mock_run_request(
            &mock_id,
            ftrb::MockComponentStartInfo {
                ns: start_info.ns,
                outgoing_dir: start_info.outgoing_dir,
                ..ftrb::MockComponentStartInfo::EMPTY
            },
        )?;

        fasync::Task::local(run_mock_controller(
            controller.into_stream()?,
            mock_id,
            start_info.runtime_dir.unwrap(),
            mock_control_handle.clone(),
        ))
        .detach();
        Ok(())
    }

    /// Launches a new v1 component. This is done by using fuchsia.sys.Environment to create a new
    /// nested environment and then launching a v1 component into that environment, using the "svc"
    /// entry from `start_info.ns` as the v1 component's additional services. The v1 component's
    /// outgoing directory is likewise connected to `start_info.outgoing_directory`, allowing
    /// protocol capabilities to flow in either direction.
    async fn launch_v1_component(
        legacy_url: String,
        start_info: fcrunner::ComponentStartInfo,
        controller: ServerEnd<fcrunner::ComponentControllerMarker>,
    ) -> Result<(), Error> {
        let execution_scope = ExecutionScope::new();

        // We are going to create a new v1 nested environment that holds the component's incoming
        // svc contents along with fuchsia.sys.Loader. This is because fuchsia.sys.Loader must be
        // in the environment for us to be able to launch components within it.
        //
        // In order to accomplish this, we need to host an svc directory that holds this protocol
        // along with everything else in the component's incoming svc directory. When the
        // fuchsia.sys.Loader node in the directory is connected to, we forward the connection to
        // our own namespace, and for any other connection we forward to the component's incoming
        // svc.
        let namespace = start_info.ns.unwrap();
        if namespace.len() > 2 {
            return Err(format_err!("v1 component namespace contains unexpected directories"));
        }
        let mut svc_names = vec![];
        let mut svc_dir_proxy = None;
        for namespace_entry in namespace {
            match namespace_entry.path.as_ref().map(|s| s.as_str()) {
                Some("/pkg") => (),
                Some("/svc") => {
                    let dir_proxy = namespace_entry
                        .directory
                        .expect("missing directory handle")
                        .into_proxy()?;
                    svc_names = files_async::readdir(&dir_proxy)
                        .await?
                        .into_iter()
                        .map(|direntry| direntry.name)
                        .collect();
                    svc_dir_proxy = Some(dir_proxy);
                    break;
                }
                Some(p) => return Err(format_err!("unexpected item in namespace: {:?}", p)),
                _ => return Err(format_err!("malformed namespace")),
            }
        }

        let runner_svc_dir_proxy = io_util::open_directory_in_namespace(
            "/svc",
            fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE,
        )?;
        let host_pseudo_dir = pfs::simple();
        host_pseudo_dir.clone().add_entry(
            fsysv1::LoaderMarker::NAME,
            vfs::remote::remote_boxed(Box::new(
                move |_scope: ExecutionScope,
                      flags: u32,
                      mode: u32,
                      _relative_path: VfsPath,
                      server_end: ServerEnd<fio::NodeMarker>| {
                    if let Err(e) = runner_svc_dir_proxy.open(
                        flags,
                        mode,
                        fsysv1::LoaderMarker::NAME,
                        server_end,
                    ) {
                        error!("failed to forward service open to realm builder server namespace: {:?}", e);
                    }
                },
            )),
        )?;

        if let Some(svc_dir_proxy) = svc_dir_proxy {
            for svc_name in &svc_names {
                let svc_dir_proxy = Clone::clone(&svc_dir_proxy);
                let svc_name = svc_name.clone();
                let svc_name_for_err = svc_name.clone();
                if let Err(status) = host_pseudo_dir.clone().add_entry(
                    svc_name.clone().as_str(),
                    vfs::remote::remote_boxed(Box::new(
                        move |_scope: ExecutionScope,
                              flags: u32,
                              mode: u32,
                              _relative_path: VfsPath,
                              server_end: ServerEnd<fio::NodeMarker>| {
                            if let Err(e) =
                                svc_dir_proxy.open(flags, mode, svc_name.as_str(), server_end)
                            {
                                error!("failed to forward service open to v2 namespace: {:?}", e);
                            }
                        },
                    )),
                ) {
                    if status == fuchsia_zircon::Status::ALREADY_EXISTS {
                        log::error!(
                            "Service {} added twice to namespace of component {}",
                            svc_name_for_err,
                            legacy_url
                        );
                    }

                    return Err(status.into());
                }
            }
        }

        let (host_dir_client_end, host_dir_server_end) =
            create_endpoints::<fio::NodeMarker>().expect("could not create node proxy endpoints");
        host_pseudo_dir.clone().open(
            execution_scope.clone(),
            fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE,
            fio::MODE_TYPE_DIRECTORY,
            VfsPath::dot(),
            host_dir_server_end.into_channel().into(),
        );

        svc_names.push(fsysv1::LoaderMarker::NAME.into());
        let mut additional_services = fsysv1::ServiceList {
            names: svc_names,
            provider: None,
            host_directory: Some(host_dir_client_end.into_channel()),
        };

        // Our service list for the new nested environment is all set up, so we can proceed with
        // creating the v1 nested environment.

        let mut options = fsysv1::EnvironmentOptions {
            inherit_parent_services: false,
            use_parent_runners: false,
            kill_on_oom: true,
            delete_storage_on_death: true,
        };

        let id: u64 = rand::thread_rng().gen();
        let realm_label = format!("v2-bridge-{}", id);

        let (sub_env_proxy, sub_env_server_end) = create_proxy::<fsysv1::EnvironmentMarker>()?;
        let (sub_env_controller_proxy, sub_env_controller_server_end) =
            create_proxy::<fsysv1::EnvironmentControllerMarker>()?;

        let env_proxy = connect_to_protocol::<fsysv1::EnvironmentMarker>()?;
        env_proxy.create_nested_environment(
            sub_env_server_end,
            sub_env_controller_server_end,
            &realm_label,
            Some(&mut additional_services),
            &mut options,
        )?;

        // We have created the nested environment that holds exactly and only fuchsia.sys.Loader.
        //
        // Now we can create the component.

        // The directory request given to the v1 component launcher connects the given directory
        // handle to the v1 component's `svc` subdir of its outgoing directory, whereas the
        // directory request we got from component manager should connect to the top-level of the
        // outgoing directory.
        //
        // We can make the two work together by hosting our own vfs on `start_info.outgoing_dir`,
        // which forwards connections to `svc` into the v1 component's `directory_request`.

        let (outgoing_svc_dir_proxy, outgoing_svc_dir_server_end) =
            create_proxy::<fio::DirectoryMarker>()?;
        let out_pseudo_dir = pseudo_directory!(
            "svc" => vfs::remote::remote_dir(outgoing_svc_dir_proxy),
        );
        out_pseudo_dir.open(
            execution_scope.clone(),
            fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE,
            fio::MODE_TYPE_DIRECTORY,
            VfsPath::dot(),
            start_info.outgoing_dir.unwrap().into_channel().into(),
        );

        // We've got a nested environment and everything set up to get protocols into and out of
        // the v1 component. Time to launch it.
        let (sub_env_launcher_proxy, sub_env_launcher_server_end) =
            create_proxy::<fsysv1::LauncherMarker>()?;
        sub_env_proxy.get_launcher(sub_env_launcher_server_end)?;

        let mut launch_info = fsysv1::LaunchInfo {
            url: legacy_url.clone(),
            arguments: None,
            out: None,
            err: None,
            directory_request: Some(outgoing_svc_dir_server_end.into_channel()),
            flat_namespace: None,
            additional_services: None,
        };
        sub_env_launcher_proxy.create_component(&mut launch_info, None)?;

        fasync::Task::local(run_v1_controller(
            controller.into_stream()?,
            legacy_url,
            realm_label,
            start_info.runtime_dir.unwrap(),
            sub_env_controller_proxy,
            execution_scope,
        ))
        .detach();
        Ok(())
    }
}

enum MockIdOrLegacyUrl {
    MockId(String),
    LegacyUrl(String),
}

/// Extracts either the value for the `mock_id` key or the `legacy_url` key from the provided
/// dictionary. It is an error for both keys to be present at once, or for anything else to be
/// present in the dictionary.
fn extract_mock_id_or_legacy_url<'a>(dict: fdata::Dictionary) -> Result<MockIdOrLegacyUrl, Error> {
    let mut entries = dict.entries.ok_or(format_err!("program section is empty"))?;
    if entries.len() != 1 {
        return Err(format_err!(
            "program section must contain only one field, this one has: {:?}",
            entries.into_iter().map(|e| e.key).collect::<Vec<_>>()
        ));
    }
    let entry = entries.pop().unwrap();
    let entry_value =
        entry.value.map(|box_| *box_).ok_or(format_err!("program section is missing value"))?;
    match (entry.key.as_str(), entry_value) {
        (MOCK_ID_KEY, fdata::DictionaryValue::Str(s)) => {
            return Ok(MockIdOrLegacyUrl::MockId(s.clone()))
        }
        (LEGACY_URL_KEY, fdata::DictionaryValue::Str(s)) => {
            return Ok(MockIdOrLegacyUrl::LegacyUrl(s.clone()))
        }
        _ => return Err(format_err!("malformed program section")),
    }
}

async fn run_mock_controller(
    mut stream: fcrunner::ComponentControllerRequestStream,
    mock_id: String,
    runtime_dir_server_end: ServerEnd<fio::DirectoryMarker>,
    control_handle: ftrb::FrameworkIntermediaryControlHandle,
) {
    let execution_scope = ExecutionScope::new();
    let runtime_dir = pseudo_directory!(
        "mock_id" => read_only_static(mock_id.clone().into_bytes()),
    );
    runtime_dir.open(
        execution_scope.clone(),
        fio::OPEN_RIGHT_READABLE,
        fio::MODE_TYPE_DIRECTORY,
        VfsPath::dot(),
        runtime_dir_server_end.into_channel().into(),
    );

    while let Some(req) =
        stream.try_next().await.expect("invalid controller request from component manager")
    {
        match req {
            fcrunner::ComponentControllerRequest::Stop { .. }
            | fcrunner::ComponentControllerRequest::Kill { .. } => {
                // We don't actually care much if this succeeds. If we can no longer successfully
                // talk to the topology builder library then the test probably crashed, and the
                // mock has thus stopped anyway.
                let _ = control_handle.send_on_mock_stop_request(&mock_id);

                break;
            }
        }
    }

    execution_scope.shutdown();
}

/// Services `runtime_dir_server_end`. When component manager instructs this component to stop
/// running with a message over `stream` we call `v1_controller.kill()` and shutdown
/// `execution_scope`.
async fn run_v1_controller(
    mut stream: fcrunner::ComponentControllerRequestStream,
    legacy_url: String,
    realm_label: String,
    runtime_dir_server_end: ServerEnd<fio::DirectoryMarker>,
    v1_controller: fsysv1::EnvironmentControllerProxy,
    execution_scope: ExecutionScope,
) {
    // Run the runtime dir for component manager
    let runtime_dir = pseudo_directory!(
        "legacy_url" => read_only_static(legacy_url.into_bytes()),
        "realm_label" => read_only_static(realm_label.into_bytes()),
    );
    runtime_dir.open(
        execution_scope.clone(),
        fio::OPEN_RIGHT_READABLE,
        fio::MODE_TYPE_DIRECTORY,
        VfsPath::dot(),
        runtime_dir_server_end.into_channel().into(),
    );

    while let Some(req) =
        stream.try_next().await.expect("invalid controller request from component manager")
    {
        match req {
            fcrunner::ComponentControllerRequest::Stop { .. }
            | fcrunner::ComponentControllerRequest::Kill { .. } => {
                // We don't actually care much if this succeeds. If we can no longer successfully
                // talk to the topology builder library then the test probably crashed, and the
                // mock has thus stopped anyway.
                v1_controller.kill().await.expect("failed to kill v1 component");

                break;
            }
        }
    }

    execution_scope.shutdown();
}
