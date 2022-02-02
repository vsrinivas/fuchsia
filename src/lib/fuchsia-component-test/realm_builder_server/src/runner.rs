// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context, Error},
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_component_runner as fcrunner, fidl_fuchsia_data as fdata, fidl_fuchsia_io as fio,
    fidl_fuchsia_sys as fsysv1, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_protocol,
    futures::lock::Mutex,
    futures::{future::BoxFuture, TryStreamExt},
    rand::{self, Rng},
    std::{collections::HashMap, sync::Arc},
    tracing::*,
    vfs::execution_scope::ExecutionScope,
};

pub const RUNNER_NAME: &'static str = "realm_builder";
pub const LOCAL_COMPONENT_ID_KEY: &'static str = "local_component_id";
pub const LEGACY_URL_KEY: &'static str = "legacy_url";

#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub struct LocalComponentId(String);

impl From<LocalComponentId> for String {
    fn from(local_id: LocalComponentId) -> Self {
        local_id.0
    }
}

#[derive(Clone)]
pub enum ComponentImplementer {
    RunnerProxy(Arc<Mutex<Option<fcrunner::ComponentRunnerProxy>>>),
    Builtin(
        Arc<
            dyn Fn(ServerEnd<fio::DirectoryMarker>) -> BoxFuture<'static, ()>
                + Sync
                + Send
                + 'static,
        >,
    ),
}

pub struct Runner {
    next_local_component_id: Mutex<u64>,
    local_component_proxies: Mutex<HashMap<String, ComponentImplementer>>,
    execution_scope: ExecutionScope,
}

impl Runner {
    pub fn new() -> Arc<Self> {
        Arc::new(Self {
            next_local_component_id: Mutex::new(0),
            local_component_proxies: Mutex::new(HashMap::new()),
            execution_scope: ExecutionScope::new(),
        })
    }

    #[cfg(test)]
    pub async fn local_component_proxies(
        self: &Arc<Self>,
    ) -> HashMap<String, ComponentImplementer> {
        self.local_component_proxies.lock().await.clone()
    }

    pub async fn register_local_component(
        self: &Arc<Self>,
        runner_proxy: Arc<Mutex<Option<fcrunner::ComponentRunnerProxy>>>,
    ) -> LocalComponentId {
        let mut next_local_component_id_guard = self.next_local_component_id.lock().await;
        let mut local_component_proxies_guard = self.local_component_proxies.lock().await;

        let local_component_id = format!("{}", *next_local_component_id_guard);
        *next_local_component_id_guard += 1;

        local_component_proxies_guard
            .insert(local_component_id.clone(), ComponentImplementer::RunnerProxy(runner_proxy));
        LocalComponentId(local_component_id)
    }

    pub async fn register_builtin_component<M>(
        self: &Arc<Self>,
        implementation: M,
    ) -> LocalComponentId
    where
        M: Fn(ServerEnd<fio::DirectoryMarker>) -> BoxFuture<'static, ()> + Sync + Send + 'static,
    {
        let mut next_local_component_id_guard = self.next_local_component_id.lock().await;
        let mut local_component_proxies_guard = self.local_component_proxies.lock().await;

        let local_component_id = format!("{}", *next_local_component_id_guard);
        *next_local_component_id_guard += 1;

        local_component_proxies_guard.insert(
            local_component_id.clone(),
            ComponentImplementer::Builtin(Arc::new(implementation)),
        );
        LocalComponentId(local_component_id)
    }

    pub fn run_runner_service(self: &Arc<Self>, stream: fcrunner::ComponentRunnerRequestStream) {
        let self_ref = self.clone();
        fasync::Task::local(async move {
            if let Err(e) = self_ref.handle_runner_request_stream(stream).await {
                warn!("error encountered while running realm builder runner service: {:?}", e);
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

                    match extract_local_component_id_or_legacy_url(program)? {
                        LocalComponentIdOrLegacyUrl::LocalComponentId(local_component_id) => {
                            self.launch_local_component(local_component_id, start_info, controller)
                                .await?;
                        }
                        LocalComponentIdOrLegacyUrl::LegacyUrl(legacy_url) => {
                            self.launch_v1_component(legacy_url, start_info, controller).await?;
                        }
                    }
                }
            }
        }
        Ok(())
    }

    async fn launch_local_component(
        self: &Arc<Self>,
        local_component_id: String,
        mut start_info: fcrunner::ComponentStartInfo,
        controller: ServerEnd<fcrunner::ComponentControllerMarker>,
    ) -> Result<(), Error> {
        let local_component_proxies_guard = self.local_component_proxies.lock().await;
        let local_component_control_handle_or_runner_proxy = local_component_proxies_guard
            .get(&local_component_id)
            .ok_or(format_err!("no such local component: {:?}", local_component_id))?
            .clone();

        match local_component_control_handle_or_runner_proxy {
            ComponentImplementer::RunnerProxy(runner_proxy_placeholder) => {
                let runner_proxy_placeholder_guard = runner_proxy_placeholder.lock().await;
                if runner_proxy_placeholder_guard.is_none() {
                    return Err(format_err!("runner request received for a local component before Builder.Build was called, this should be impossible"));
                }
                let runner_proxy = runner_proxy_placeholder_guard.as_ref().unwrap();
                if let Some(mut program) = start_info.program.as_mut() {
                    remove_local_component_id(&mut program);
                }
                runner_proxy
                    .start(start_info, controller)
                    .context("failed to send start request for local component to client")?;
            }
            ComponentImplementer::Builtin(implementation) => {
                self.execution_scope.spawn(run_builtin_controller(
                    controller.into_stream()?,
                    fasync::Task::local((*implementation)(start_info.outgoing_dir.unwrap())),
                ));
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
        self: &Arc<Self>,
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

enum LocalComponentIdOrLegacyUrl {
    LocalComponentId(String),
    LegacyUrl(String),
}

/// Extracts either the value for the `local_component_id` key or the `legacy_url` key from the provided
/// dictionary. It is an error for both keys to be present at once, or for anything else to be
/// present in the dictionary.
fn extract_local_component_id_or_legacy_url<'a>(
    dict: fdata::Dictionary,
) -> Result<LocalComponentIdOrLegacyUrl, Error> {
    let entries = dict.entries.ok_or(format_err!("program section is empty"))?;
    for entry in entries.into_iter() {
        let entry_value =
            entry.value.map(|box_| *box_).ok_or(format_err!("program section is missing value"))?;
        match (entry.key.as_str(), entry_value) {
            (LOCAL_COMPONENT_ID_KEY, fdata::DictionaryValue::Str(s)) => {
                return Ok(LocalComponentIdOrLegacyUrl::LocalComponentId(s.clone()))
            }
            (LEGACY_URL_KEY, fdata::DictionaryValue::Str(s)) => {
                return Ok(LocalComponentIdOrLegacyUrl::LegacyUrl(s.clone()))
            }
            _ => continue,
        }
    }
    return Err(format_err!("malformed program section"));
}

fn remove_local_component_id(dict: &mut fdata::Dictionary) {
    if let Some(entries) = &mut dict.entries {
        *entries = entries
            .drain(..)
            .filter(|entry| entry.key.as_str() != LOCAL_COMPONENT_ID_KEY)
            .collect();
    }
}

async fn run_builtin_controller(
    mut stream: fcrunner::ComponentControllerRequestStream,
    builtin_task: fasync::Task<()>,
) {
    while let Some(req) =
        stream.try_next().await.expect("invalid controller request from component manager")
    {
        match req {
            fcrunner::ComponentControllerRequest::Stop { .. }
            | fcrunner::ComponentControllerRequest::Kill { .. } => {
                // The `return` would have dropped this anyway, but let's do it explicitly to help
                // convey to the reader that the whole point here is that the task stops running
                // when a stop or kill command is received.
                drop(builtin_task);
                return;
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        assert_matches::assert_matches,
        fidl::endpoints::{create_endpoints, create_proxy_and_stream},
        futures::{channel::mpsc, FutureExt, SinkExt, StreamExt},
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
        let LocalComponentId(local_component_id) =
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
                    key: LOCAL_COMPONENT_ID_KEY.to_string(),
                    value: Some(Box::new(fdata::DictionaryValue::Str(local_component_id))),
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
                    // The `LOCAL_COMPONENT_ID_KEY` entry gets removed from the program section
                    // before sending it off to the client, as this value is only used for
                    // bookkeeping internal to the realm builder runner.
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

    #[fuchsia::test]
    async fn launch_builtin_component() {
        let runner = Runner::new();

        let (sender, mut receiver) = mpsc::channel(1);

        let LocalComponentId(local_component_id) = runner
            .register_builtin_component(move |_outgoing_dir| {
                let mut sender = sender.clone();
                async move {
                    sender.send(()).await.expect("failed to send that builtin was invoked");
                }
                .boxed()
            })
            .await;

        let (server_runner_proxy, server_runner_request_stream) =
            create_proxy_and_stream::<fcrunner::ComponentRunnerMarker>().unwrap();

        let _runner_request_stream_task = fasync::Task::local(async move {
            if let Err(e) = runner.handle_runner_request_stream(server_runner_request_stream).await
            {
                panic!("error returned by request stream: {:?}", e);
            }
        });

        let example_program = fdata::Dictionary {
            entries: Some(vec![fdata::DictionaryEntry {
                key: LOCAL_COMPONENT_ID_KEY.to_string(),
                value: Some(Box::new(fdata::DictionaryValue::Str(local_component_id))),
            }]),
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

        receiver.next().await.expect("failed to receive that builtin was invoked");
    }
}
