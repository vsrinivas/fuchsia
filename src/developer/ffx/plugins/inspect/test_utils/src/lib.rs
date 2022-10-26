// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    diagnostics_data::{Data, DiagnosticsHierarchy, InspectData, Property},
    errors as _, ffx_inspect_common as _, ffx_writer as _,
    fidl::endpoints::ServerEnd,
    fidl::prelude::*,
    fidl_fuchsia_developer_remotecontrol::{
        ArchiveIteratorEntry, ArchiveIteratorError, ArchiveIteratorMarker, ArchiveIteratorRequest,
        BridgeStreamParameters, DiagnosticsData, InlineData, RemoteControlProxy,
        RemoteControlRequest, RemoteDiagnosticsBridgeProxy, RemoteDiagnosticsBridgeRequest,
    },
    fidl_fuchsia_diagnostics::{ClientSelectorConfiguration, DataType, StreamMode},
    futures::{StreamExt, TryStreamExt},
    iquery_test_support,
    std::sync::Arc,
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

pub fn get_empty_value_json() -> serde_json::Value {
    serde_json::json!([])
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
