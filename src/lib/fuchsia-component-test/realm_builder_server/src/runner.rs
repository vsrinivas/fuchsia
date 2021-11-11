// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_component_runner as fcrunner, fidl_fuchsia_component_test as ftest,
    fidl_fuchsia_data as fdata, fidl_fuchsia_io as fio, fidl_fuchsia_sys as fsysv1,
    fuchsia_async as fasync,
    fuchsia_component::client::connect_to_protocol,
    futures::lock::Mutex,
    futures::TryStreamExt,
    log::*,
    rand::{self, Rng},
    std::{collections::HashMap, sync::Arc},
    vfs::{
        directory::entry::DirectoryEntry, execution_scope::ExecutionScope,
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
    mocks: Mutex<HashMap<String, ftest::RealmBuilderControlHandle>>,
}

impl Runner {
    pub fn new() -> Arc<Self> {
        Arc::new(Self { next_mock_id: Mutex::new(0), mocks: Mutex::new(HashMap::new()) })
    }

    pub async fn register_mock(
        self: &Arc<Self>,
        control_handle: ftest::RealmBuilderControlHandle,
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
                warn!("error encountered while running runner service for mocks: {:?}", e);
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
            ftest::MockComponentStartInfo {
                ns: start_info.ns,
                outgoing_dir: start_info.outgoing_dir,
                ..ftest::MockComponentStartInfo::EMPTY
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

        let id: u64 = rand::thread_rng().gen();
        let realm_label = format!("v2-bridge-{}", id);
        let parent_env = connect_to_protocol::<fsysv1::EnvironmentMarker>()?;

        let legacy_component = legacy_component_lib::LegacyComponent::run(
            legacy_url,
            start_info,
            parent_env.into(),
            realm_label,
            execution_scope.clone(),
        )
        .await?;
        let controller = controller.into_stream()?;
        fasync::Task::local(async move {
            legacy_component.serve_controller(controller, execution_scope).await.unwrap()
        })
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
    control_handle: ftest::RealmBuilderControlHandle,
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
