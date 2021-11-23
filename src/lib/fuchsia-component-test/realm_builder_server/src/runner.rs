// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context, Error},
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_component_runner as fcrunner, fidl_fuchsia_component_test as ftest,
    fidl_fuchsia_data as fdata, fidl_fuchsia_io as fio, fidl_fuchsia_sys as fsysv1,
    fuchsia_async as fasync,
    fuchsia_component::client::connect_to_protocol,
    futures::lock::Mutex,
    futures::TryStreamExt,
    rand::{self, Rng},
    std::{collections::HashMap, sync::Arc},
    tracing::*,
    vfs::{
        directory::entry::DirectoryEntry, execution_scope::ExecutionScope,
        file::vmo::asynchronous::read_only_static, path::Path as VfsPath, pseudo_directory,
    },
};

pub const RUNNER_NAME: &'static str = "realm_builder";
pub const MOCK_ID_KEY: &'static str = "mock_id";
pub const LEGACY_URL_KEY: &'static str = "legacy_url";
pub const LOCAL_COMPONENT_NAME_KEY: &'static str = "LOCAL_COMPONENT_NAME";

#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub struct MockId(String);

impl From<MockId> for String {
    fn from(mock_id: MockId) -> Self {
        mock_id.0
    }
}

#[derive(Debug, Clone)]
pub enum ControlHandleOrRunnerProxy {
    ControlHandle(ftest::RealmBuilderControlHandle),
    #[allow(unused)]
    RunnerProxy(Arc<Mutex<Option<fcrunner::ComponentRunnerProxy>>>),
}

pub struct Runner {
    next_mock_id: Mutex<u64>,
    mocks: Mutex<HashMap<String, ControlHandleOrRunnerProxy>>,
}

impl Runner {
    pub fn new() -> Arc<Self> {
        Arc::new(Self { next_mock_id: Mutex::new(0), mocks: Mutex::new(HashMap::new()) })
    }

    #[cfg(test)]
    pub async fn mocks(self: &Arc<Self>) -> HashMap<String, ControlHandleOrRunnerProxy> {
        self.mocks.lock().await.clone()
    }

    pub async fn register_mock(
        self: &Arc<Self>,
        control_handle: ftest::RealmBuilderControlHandle,
    ) -> MockId {
        let mut next_mock_id_guard = self.next_mock_id.lock().await;
        let mut mocks_guard = self.mocks.lock().await;

        let mock_id = format!("{}", *next_mock_id_guard);
        *next_mock_id_guard += 1;

        mocks_guard
            .insert(mock_id.clone(), ControlHandleOrRunnerProxy::ControlHandle(control_handle));
        MockId(mock_id)
    }

    pub async fn register_local_component(
        self: &Arc<Self>,
        runner_proxy: Arc<Mutex<Option<fcrunner::ComponentRunnerProxy>>>,
    ) -> MockId {
        let mut next_mock_id_guard = self.next_mock_id.lock().await;
        let mut mocks_guard = self.mocks.lock().await;

        let mock_id = format!("{}", *next_mock_id_guard);
        *next_mock_id_guard += 1;

        mocks_guard.insert(mock_id.clone(), ControlHandleOrRunnerProxy::RunnerProxy(runner_proxy));
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
        mut start_info: fcrunner::ComponentStartInfo,
        controller: ServerEnd<fcrunner::ComponentControllerMarker>,
    ) -> Result<(), Error> {
        let mocks_guard = self.mocks.lock().await;
        let mock_control_handle_or_runner_proxy =
            mocks_guard.get(&mock_id).ok_or(format_err!("no such mock: {:?}", mock_id))?.clone();

        match mock_control_handle_or_runner_proxy {
            ControlHandleOrRunnerProxy::ControlHandle(mock_control_handle) => {
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
            }
            ControlHandleOrRunnerProxy::RunnerProxy(runner_proxy_placeholder) => {
                let runner_proxy_placeholder_guard = runner_proxy_placeholder.lock().await;
                if runner_proxy_placeholder_guard.is_none() {
                    return Err(format_err!("runner request received for a local component before Builder.Build was called, this should be impossible"));
                }
                let runner_proxy = runner_proxy_placeholder_guard.as_ref().unwrap();
                if let Some(mut program) = start_info.program.as_mut() {
                    remove_mock_id(&mut program);
                }
                runner_proxy
                    .start(start_info, controller)
                    .context("failed to send start request for local component to client")?;
            }
        };
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
    let entries = dict.entries.ok_or(format_err!("program section is empty"))?;
    for entry in entries.into_iter() {
        let entry_value =
            entry.value.map(|box_| *box_).ok_or(format_err!("program section is missing value"))?;
        match (entry.key.as_str(), entry_value) {
            (MOCK_ID_KEY, fdata::DictionaryValue::Str(s)) => {
                return Ok(MockIdOrLegacyUrl::MockId(s.clone()))
            }
            (LEGACY_URL_KEY, fdata::DictionaryValue::Str(s)) => {
                return Ok(MockIdOrLegacyUrl::LegacyUrl(s.clone()))
            }
            _ => continue,
        }
    }
    return Err(format_err!("malformed program section"));
}

fn remove_mock_id(dict: &mut fdata::Dictionary) {
    if let Some(entries) = &mut dict.entries {
        *entries = entries.drain(..).filter(|entry| entry.key.as_str() != MOCK_ID_KEY).collect();
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

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl::endpoints::{create_endpoints, create_proxy_and_stream},
        matches::assert_matches,
    };

    // There are two separate `fuchsia.component.runner/ComponentRunner` channels for every local
    // component that's launched: one connecting component manager to the realm builder runner, and
    // one connecting the realm builder runner to a client. This test feeds a launch request into
    // the client end of the first channel pair (pretending to be component manager), and observes
    // the request be sent out by the realm builder runner on the server end of the second pair
    // (pretending to be the realm builder client).
    #[fuchsia::test]
    async fn launch_local_component() {
        let runner = Runner::new();

        let (client_runner_proxy, mut client_runner_request_stream) =
            create_proxy_and_stream::<fcrunner::ComponentRunnerMarker>().unwrap();
        let MockId(mock_id) =
            runner.register_local_component(Arc::new(Mutex::new(Some(client_runner_proxy)))).await;

        let (server_runner_proxy, server_runner_request_stream) =
            create_proxy_and_stream::<fcrunner::ComponentRunnerMarker>().unwrap();

        let _runner_request_stream_task = fasync::Task::local(async move {
            if let Err(e) = runner.handle_runner_request_stream(server_runner_request_stream).await
            {
                panic!("error returned by request stream: {:?}", e);
            }
        });

        let example_program = fdata::Dictionary {
            entries: Some(vec![
                fdata::DictionaryEntry {
                    key: "hippos".to_string(),
                    value: Some(Box::new(fdata::DictionaryValue::Str("rule!".to_string()))),
                },
                fdata::DictionaryEntry {
                    key: MOCK_ID_KEY.to_string(),
                    value: Some(Box::new(fdata::DictionaryValue::Str(mock_id))),
                },
            ]),
            ..fdata::Dictionary::EMPTY
        };

        let (_controller_client_end, controller_server_end) =
            create_endpoints::<fcrunner::ComponentControllerMarker>().unwrap();
        let (_outgoing_dir_client_end, outgoing_dir_server_end) =
            create_endpoints::<fio::DirectoryMarker>().unwrap();
        let (_runtime_dir_client_end, runtime_dir_server_end) =
            create_endpoints::<fio::DirectoryMarker>().unwrap();

        server_runner_proxy
            .start(
                fcrunner::ComponentStartInfo {
                    program: Some(example_program),
                    ns: Some(vec![]),
                    outgoing_dir: Some(outgoing_dir_server_end),
                    runtime_dir: Some(runtime_dir_server_end),
                    ..fcrunner::ComponentStartInfo::EMPTY
                },
                controller_server_end,
            )
            .expect("failed to write start message");

        assert_matches!(
            client_runner_request_stream
                .try_next()
                .await
                .expect("failed to read from client_runner_request_stream"),
            Some(fcrunner::ComponentRunnerRequest::Start { start_info, .. })
                if start_info.program == Some(fdata::Dictionary {
                    // The `MOCK_ID_KEY` entry gets removed from the program section before sending
                    // it off to the client, as this value is only used for bookkeeping internal to
                    // the realm builder runner.
                    entries: Some(vec![
                        fdata::DictionaryEntry {
                            key: "hippos".to_string(),
                            value: Some(Box::new(fdata::DictionaryValue::Str("rule!".to_string()))),
                        },
                    ]),
                    ..fdata::Dictionary::EMPTY
                })
        );
    }
}
