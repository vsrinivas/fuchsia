// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    byteorder::{LittleEndian, WriteBytesExt},
    diagnostics_data::{
        Data, DiagnosticsHierarchy, InspectData, LifecycleData, LifecycleType, Property,
    },
    errors as _, ffx_inspect_common as _, ffx_writer as _,
    fidl::endpoints::ServerEnd,
    fidl::prelude::*,
    fidl_fuchsia_developer_remotecontrol::{
        ArchiveIteratorEntry, ArchiveIteratorError, ArchiveIteratorMarker, ArchiveIteratorRequest,
        BridgeStreamParameters, DiagnosticsData, InlineData, RemoteControlProxy,
        RemoteControlRequest, RemoteDiagnosticsBridgeProxy, RemoteDiagnosticsBridgeRequest,
    },
    fidl_fuchsia_diagnostics::{ClientSelectorConfiguration, DataType, StreamMode},
    fidl_fuchsia_io as fio,
    fuchsia_zircon_status::Status,
    futures::{StreamExt, TryStreamExt},
    iquery_test_support,
    std::{
        collections::HashMap,
        io::Write,
        path::Path,
        sync::{
            atomic::{AtomicBool, Ordering},
            Arc,
        },
    },
};

#[derive(Default)]
pub struct FakeArchiveIteratorResponse {
    value: String,
    iterator_error: Option<ArchiveIteratorError>,
    should_fidl_error: bool,
}

impl FakeArchiveIteratorResponse {
    pub fn new_with_value(value: String) -> Self {
        FakeArchiveIteratorResponse { value, ..Default::default() }
    }

    pub fn new_with_fidl_error() -> Self {
        FakeArchiveIteratorResponse { should_fidl_error: true, ..Default::default() }
    }

    pub fn new_with_iterator_error(iterator_error: ArchiveIteratorError) -> Self {
        FakeArchiveIteratorResponse { iterator_error: Some(iterator_error), ..Default::default() }
    }
}

pub fn setup_fake_bridge_provider(
    server_end: ServerEnd<ArchiveIteratorMarker>,
    responses: Arc<Vec<FakeArchiveIteratorResponse>>,
) -> Result<()> {
    let mut stream = server_end.into_stream()?;
    fuchsia_async::Task::local(async move {
        let mut iter = responses.iter();
        while let Some(req) = stream.next().await {
            let ArchiveIteratorRequest::GetNext { responder } = req.unwrap();
            let next = iter.next();
            match next {
                Some(FakeArchiveIteratorResponse { value, iterator_error, should_fidl_error }) => {
                    if let Some(err) = iterator_error {
                        responder.send(&mut Err(*err)).unwrap();
                    } else if *should_fidl_error {
                        responder.control_handle().shutdown();
                    } else {
                        responder
                            .send(&mut Ok(vec![ArchiveIteratorEntry {
                                diagnostics_data: Some(DiagnosticsData::Inline(InlineData {
                                    data: value.clone(),
                                    truncated_chars: 0,
                                })),
                                ..ArchiveIteratorEntry::EMPTY
                            }]))
                            .unwrap()
                    }
                }
                None => responder.send(&mut Ok(vec![])).unwrap(),
            }
        }
    })
    .detach();
    Ok(())
}

pub struct FakeBridgeData {
    parameters: BridgeStreamParameters,
    responses: Arc<Vec<FakeArchiveIteratorResponse>>,
}

impl FakeBridgeData {
    pub fn new(
        parameters: BridgeStreamParameters,
        responses: Arc<Vec<FakeArchiveIteratorResponse>>,
    ) -> Self {
        FakeBridgeData { parameters, responses }
    }
}

pub fn setup_fake_diagnostics_bridge(
    expected_data: Vec<FakeBridgeData>,
) -> RemoteDiagnosticsBridgeProxy {
    let (proxy, mut stream) = fidl::endpoints::create_proxy_and_stream::<<fidl_fuchsia_developer_remotecontrol::RemoteDiagnosticsBridgeProxy as fidl::endpoints::Proxy>::Protocol>().unwrap();
    fuchsia_async::Task::local(async move {
        'req: while let Ok(Some(req)) = stream.try_next().await {
            match req {
                RemoteDiagnosticsBridgeRequest::StreamDiagnostics {
                    parameters,
                    iterator,
                    responder,
                } => {
                    for data in expected_data.iter() {
                        if data.parameters == parameters {
                            setup_fake_bridge_provider(iterator, data.responses.clone()).unwrap();
                            responder.send(&mut Ok(())).expect("should send");
                            continue 'req;
                        }
                    }
                    assert!(false, "{:?} did not match any expected parameters", parameters);
                }
                _ => assert!(false),
            }
        }
    })
    .detach();
    proxy
}

pub fn make_short_lifecycle(moniker: String, timestamp: i64, typ: LifecycleType) -> LifecycleData {
    Data::for_lifecycle_event(
        moniker.clone(),
        typ,
        None,
        format!("fake-url://{}", moniker),
        timestamp,
        vec![],
    )
}

pub fn make_inspects_for_lifecycle() -> Vec<InspectData> {
    vec![
        make_inspect_with_length(String::from("test/moniker1"), 1, 20),
        make_inspect_with_length(String::from("test/moniker1"), 2, 30),
        make_inspect_with_length(String::from("test/moniker3"), 3, 3),
    ]
}

pub fn setup_fake_rcs() -> RemoteControlProxy {
    let mock_realm_explorer = iquery_test_support::MockRealmExplorer::default();
    let mock_realm_query = iquery_test_support::MockRealmQuery::default();
    let (proxy, mut stream) = fidl::endpoints::create_proxy_and_stream::<<fidl_fuchsia_developer_remotecontrol::RemoteControlProxy as fidl::endpoints::Proxy>::Protocol>().unwrap();
    fuchsia_async::Task::local(async move {
        let explorer = Arc::new(mock_realm_explorer);
        let querier = Arc::new(mock_realm_query);
        while let Ok(Some(req)) = stream.try_next().await {
            match req {
                RemoteControlRequest::RootRealmExplorer { server, responder } => {
                    let explorer = Arc::clone(&explorer);
                    fuchsia_async::Task::local(async move { explorer.serve(server).await })
                        .detach();
                    responder.send(&mut Ok(())).unwrap();
                }
                RemoteControlRequest::RootRealmQuery { server, responder } => {
                    let querier = Arc::clone(&querier);
                    fuchsia_async::Task::local(async move { querier.serve(server).await }).detach();
                    responder.send(&mut Ok(())).unwrap();
                }
                _ => assert!(false),
            }
        }
    })
    .detach();
    proxy
}

pub fn make_inspect_with_length(moniker: String, timestamp: i64, len: usize) -> InspectData {
    let long_string = std::iter::repeat("a").take(len).collect::<String>();
    let hierarchy = DiagnosticsHierarchy::new(
        String::from("name"),
        vec![Property::String(format!("hello_{}", timestamp), long_string)],
        vec![],
    );
    Data::for_inspect(
        moniker.clone(),
        Some(hierarchy),
        timestamp,
        format!("fake-url://{}", moniker),
        String::from("fake-filename"),
        vec![],
    )
}

pub fn make_inspects() -> Vec<InspectData> {
    vec![
        make_inspect_with_length(String::from("test/moniker1"), 1, 20),
        make_inspect_with_length(String::from("test/moniker2"), 2, 10),
        make_inspect_with_length(String::from("test/moniker3"), 3, 30),
        make_inspect_with_length(String::from("test/moniker1"), 20, 3),
    ]
}

pub fn inspect_bridge_data(
    client_selector_configuration: ClientSelectorConfiguration,
    inspects: Vec<InspectData>,
) -> FakeBridgeData {
    let params = BridgeStreamParameters {
        stream_mode: Some(StreamMode::Snapshot),
        data_type: Some(DataType::Inspect),
        client_selector_configuration: Some(client_selector_configuration),
        ..BridgeStreamParameters::EMPTY
    };
    let value = serde_json::to_string(&inspects).unwrap();
    let expected_responses = Arc::new(vec![FakeArchiveIteratorResponse::new_with_value(value)]);
    FakeBridgeData::new(params, expected_responses)
}

pub trait Entry {
    fn open(
        self: Arc<Self>,
        flags: fio::OpenFlags,
        mode: u32,
        path: &str,
        object: ServerEnd<fio::NodeMarker>,
    );
    fn encode(&self, buf: &mut Vec<u8>);
    fn name(&self) -> String;
}

pub struct MockDir {
    subdirs: HashMap<String, Arc<dyn Entry>>,
    name: String,
    at_end: AtomicBool,
}

impl MockDir {
    pub fn new(name: String) -> Self {
        MockDir { name, subdirs: HashMap::new(), at_end: AtomicBool::new(false) }
    }

    pub fn add_entry(mut self, entry: Arc<dyn Entry>) -> Self {
        self.subdirs.insert(entry.name(), entry);
        self
    }

    async fn serve(self: Arc<Self>, object: ServerEnd<fio::DirectoryMarker>) {
        let mut stream = object.into_stream().unwrap();
        let _ = stream.control_handle().send_on_open_(
            Status::OK.into_raw(),
            Some(&mut fio::NodeInfoDeprecated::Directory(fio::DirectoryObject {})),
        );
        while let Ok(Some(request)) = stream.try_next().await {
            match request {
                fio::DirectoryRequest::Open { flags, mode, path, object, .. } => {
                    self.clone().open(flags, mode, &path, object);
                }
                fio::DirectoryRequest::Clone { flags, object, .. } => {
                    self.clone().open(flags, fio::MODE_TYPE_DIRECTORY, ".", object);
                }
                fio::DirectoryRequest::Rewind { responder, .. } => {
                    self.at_end.store(false, Ordering::Relaxed);
                    responder.send(Status::OK.into_raw()).unwrap();
                }
                fio::DirectoryRequest::ReadDirents { max_bytes: _, responder, .. } => {
                    let entries = match self.at_end.compare_exchange(
                        false,
                        true,
                        Ordering::Relaxed,
                        Ordering::Relaxed,
                    ) {
                        Ok(false) => encode_entries(&self.subdirs),
                        Err(true) => Vec::new(),
                        _ => unreachable!(),
                    };
                    responder.send(Status::OK.into_raw(), &entries).unwrap();
                }
                x => panic!("unsupported request: {:?}", x),
            }
        }
    }
}

fn encode_entries(subdirs: &HashMap<String, Arc<dyn Entry>>) -> Vec<u8> {
    let mut buf = Vec::new();
    let mut data = subdirs.iter().collect::<Vec<(_, _)>>();
    data.sort_by(|a, b| a.0.cmp(b.0));
    for (_, entry) in data.iter() {
        entry.encode(&mut buf);
    }
    buf
}

impl Entry for MockDir {
    fn open(
        self: Arc<Self>,
        flags: fio::OpenFlags,
        mode: u32,
        path: &str,
        object: ServerEnd<fio::NodeMarker>,
    ) {
        let path = Path::new(path);
        let mut path_iter = path.iter();
        let segment = if let Some(segment) = path_iter.next() {
            if let Some(segment) = segment.to_str() {
                segment
            } else {
                send_error(object, Status::NOT_FOUND);
                return;
            }
        } else {
            "."
        };
        if segment == "." {
            fuchsia_async::Task::local(self.clone().serve(ServerEnd::new(object.into_channel())))
                .detach();
            return;
        }
        if let Some(entry) = self.subdirs.get(segment) {
            entry.clone().open(flags, mode, path_iter.as_path().to_str().unwrap(), object);
        } else {
            send_error(object, Status::NOT_FOUND);
        }
    }

    fn encode(&self, buf: &mut Vec<u8>) {
        buf.write_u64::<LittleEndian>(fio::INO_UNKNOWN).expect("writing mockdir ino to work");
        buf.write_u8(self.name.len() as u8).expect("writing mockdir size to work");
        buf.write_u8(fio::DirentType::Directory.into_primitive())
            .expect("writing mockdir type to work");
        buf.write_all(self.name.as_ref()).expect("writing mockdir name to work");
    }

    fn name(&self) -> String {
        self.name.clone()
    }
}

impl Entry for fio::DirectoryProxy {
    fn open(
        self: Arc<Self>,
        flags: fio::OpenFlags,
        mode: u32,
        path: &str,
        object: ServerEnd<fio::NodeMarker>,
    ) {
        let _ = fio::DirectoryProxy::open(&*self, flags, mode, path, object);
    }

    fn encode(&self, _buf: &mut Vec<u8>) {
        unimplemented!();
    }

    fn name(&self) -> String {
        unimplemented!();
    }
}

pub fn get_v1_json_dump() -> serde_json::Value {
    serde_json::json!(
        [
            {
                "data_source":"Inspect",
                "metadata":{
                    "filename":"fuchsia.inspect.Tree",
                    "component_url": "fuchsia-pkg://fuchsia.com/account#meta/account_manager.cmx",
                    "timestamp":0
                },
                "moniker":"realm1/realm2/session5/account_manager.cmx",
                "payload":{
                    "root": {
                        "accounts": {
                            "active": 0,
                            "total": 0
                        },
                        "auth_providers": {
                            "types": "google"
                        },
                        "listeners": {
                            "active": 1,
                            "events": 0,
                            "total_opened": 1
                        }
                    }
                },
                "version":1
            }
        ]
    )
}

pub fn get_v1_single_value_json() -> serde_json::Value {
    serde_json::json!(
        [
            {
                "data_source":"Inspect",
                "metadata":{
                    "filename":"fuchsia.inspect.Tree",
                    "component_url": "fuchsia-pkg://fuchsia.com/account#meta/account_manager.cmx",
                    "timestamp":0
                },
                "moniker":"realm1/realm2/session5/account_manager.cmx",
                "payload":{
                    "root": {
                        "accounts": {
                            "active": 0
                        }
                    }
                },
                "version":1
            }
        ]
    )
}

pub fn get_empty_value_json() -> serde_json::Value {
    serde_json::json!([])
}

fn send_error(object: ServerEnd<fio::NodeMarker>, status: Status) {
    let stream = object.into_stream().expect("failed to create stream");
    let control_handle = stream.control_handle();
    let _ = control_handle.send_on_open_(status.into_raw(), None);
    control_handle.shutdown_with_epitaph(status);
}
