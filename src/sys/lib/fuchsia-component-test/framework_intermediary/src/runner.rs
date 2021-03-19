// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_component_runner as fcrunner, fidl_fuchsia_data as fdata, fidl_fuchsia_io as fio,
    fidl_fuchsia_realm_builder as ftrb, fuchsia_async as fasync,
    futures::lock::Mutex,
    futures::TryStreamExt,
    log::*,
    std::{collections::HashMap, sync::Arc},
    vfs::{
        directory::entry::DirectoryEntry, execution_scope::ExecutionScope,
        file::vmo::asynchronous::read_only_static, path::Path as VfsPath, pseudo_directory,
    },
};

pub const MOCK_ID_KEY: &'static str = "mock_id";

#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub struct MockId(String);

impl MockId {
    pub(crate) fn as_str(&self) -> &str {
        self.0.as_str()
    }
}

pub struct MocksRunner {
    next_mock_id: Mutex<u64>,
    mocks: Mutex<HashMap<String, ftrb::FrameworkIntermediaryControlHandle>>,
}

impl MocksRunner {
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
                    let program = start_info.program.ok_or(format_err!("missing program"))?;
                    let namespace = start_info.ns.ok_or(format_err!("missing namespace"))?;
                    let outgoing_dir =
                        start_info.outgoing_dir.ok_or(format_err!("missing outgoing_dir"))?;
                    let runtime_dir =
                        start_info.runtime_dir.ok_or(format_err!("missing runtime_dir"))?;

                    let mock_id = extract_mock_id(program)?;
                    let mock_control_handle = {
                        let mocks_guard = self.mocks.lock().await;
                        mocks_guard
                            .get(&mock_id)
                            .ok_or(format_err!("no such mock: {:?}", mock_id))?
                            .clone()
                    };

                    mock_control_handle.send_on_mock_run_request(
                        &mock_id,
                        ftrb::MockComponentStartInfo {
                            ns: Some(namespace),
                            outgoing_dir: Some(outgoing_dir),
                            ..ftrb::MockComponentStartInfo::EMPTY
                        },
                    )?;

                    fasync::Task::local(run_mock_controller(
                        controller.into_stream()?,
                        mock_id,
                        runtime_dir,
                        mock_control_handle.clone(),
                    ))
                    .detach();
                }
            }
        }
        Ok(())
    }
}

fn extract_mock_id<'a>(dict: fdata::Dictionary) -> Result<String, Error> {
    for entry in dict.entries.ok_or(format_err!("program section is empty"))? {
        if &entry.key == MOCK_ID_KEY {
            match entry.value.map(|box_| *box_) {
                Some(fdata::DictionaryValue::Str(s)) => return Ok(s.clone()),
                Some(v) => {
                    return Err(format_err!(
                        "program section has invalid type for mock_id field: {:?}",
                        v
                    ))
                }
                _ => return Err(format_err!("program section is missing value for mock_id field")),
            }
        } else {
            // Dictionaries must contain _only_ the MOCK_ID_KEY for them to be valid
            return Err(format_err!("unrecognized key in program section: {:?}", entry.key));
        }
    }
    Err(format_err!("program section is missing mock_id field"))
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
        VfsPath::empty(),
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
