// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    cs::v2::{ElfRuntime, Execution, V2Component},
    test_utils_lib::{events::*, matcher::EventMatcher, opaque_test::*},
};

#[fuchsia_async::run_singlethreaded(test)]
async fn empty_component() {
    let test =
        OpaqueTest::default("fuchsia-pkg://fuchsia.com/cs-tests#meta/empty.cm").await.unwrap();
    let event_source = test.connect_to_event_source().await.unwrap();
    let mut event_stream = event_source.subscribe(vec![Started::NAME]).await.unwrap();

    event_source.start_component_tree().await;

    // Root must be created first
    let event = EventMatcher::ok().moniker(".").expect_match::<Started>(&mut event_stream).await;
    event.resume().await.unwrap();

    let hub_v2_path = test.get_hub_v2_path();
    let actual_root_component = V2Component::explore(hub_v2_path).await;

    let expected_root_component = V2Component {
        name: "<root>".to_string(),
        url: "fuchsia-pkg://fuchsia.com/cs-tests#meta/empty.cm".to_string(),
        id: 0,
        component_type: "static".to_string(),
        appmgr_root_v1_realm: None,
        execution: Some(Execution {
            elf_runtime: None,
            incoming_capabilities: vec!["pkg".to_string()],
            outgoing_capabilities: None,
            exposed_capabilities: vec![],
        }),
        children: vec![],
    };

    assert_eq!(actual_root_component, expected_root_component);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn tree() {
    let test =
        OpaqueTest::default("fuchsia-pkg://fuchsia.com/cs-tests#meta/root.cm").await.unwrap();
    let event_source = test.connect_to_event_source().await.unwrap();
    let mut event_stream = event_source.subscribe(vec![Started::NAME]).await.unwrap();

    event_source.start_component_tree().await;

    // Root must be created first
    let event = EventMatcher::ok().moniker(".").expect_match::<Started>(&mut event_stream).await;
    event.resume().await.unwrap();

    // 6 descendants are created eagerly. Order is irrelevant.
    for _ in 1..=6 {
        let event = EventMatcher::ok().expect_match::<Started>(&mut event_stream).await;
        event.resume().await.unwrap();
    }

    let hub_v2_path = test.get_hub_v2_path();
    let actual_root_component = V2Component::explore(hub_v2_path).await;

    let expected_root_component = V2Component {
        name: "<root>".to_string(),
        url: "fuchsia-pkg://fuchsia.com/cs-tests#meta/root.cm".to_string(),
        id: 0,
        component_type: "static".to_string(),
        appmgr_root_v1_realm: None,
        execution: Some(Execution {
            elf_runtime: None,
            incoming_capabilities: vec!["pkg".to_string()],
            outgoing_capabilities: None,
            exposed_capabilities: vec![],
        }),
        children: vec![
            V2Component {
                name: "bar".to_string(),
                url: "fuchsia-pkg://fuchsia.com/cs-tests#meta/bar.cm".to_string(),
                id: 0,
                component_type: "static".to_string(),
                appmgr_root_v1_realm: None,
                execution: Some(Execution {
                    elf_runtime: None,
                    incoming_capabilities: vec!["pkg".to_string()],
                    outgoing_capabilities: None,
                    exposed_capabilities: vec![],
                }),
                children: vec![V2Component {
                    name: "baz".to_string(),
                    url: "fuchsia-pkg://fuchsia.com/cs-tests#meta/baz.cm".to_string(),
                    id: 0,
                    component_type: "static".to_string(),
                    appmgr_root_v1_realm: None,
                    execution: Some(Execution {
                        elf_runtime: None,
                        incoming_capabilities: vec!["pkg".to_string()],
                        outgoing_capabilities: None,
                        exposed_capabilities: vec![],
                    }),
                    children: vec![],
                }],
            },
            V2Component {
                name: "foo".to_string(),
                url: "fuchsia-pkg://fuchsia.com/cs-tests#meta/foo.cm".to_string(),
                id: 0,
                component_type: "static".to_string(),
                appmgr_root_v1_realm: None,
                execution: Some(Execution {
                    elf_runtime: None,
                    incoming_capabilities: vec!["pkg".to_string()],
                    outgoing_capabilities: None,
                    exposed_capabilities: vec![],
                }),
                children: vec![
                    V2Component {
                        name: "bar".to_string(),
                        url: "fuchsia-pkg://fuchsia.com/cs-tests#meta/bar.cm".to_string(),
                        id: 0,
                        component_type: "static".to_string(),
                        appmgr_root_v1_realm: None,
                        execution: Some(Execution {
                            elf_runtime: None,
                            incoming_capabilities: vec!["pkg".to_string()],
                            outgoing_capabilities: None,
                            exposed_capabilities: vec![],
                        }),
                        children: vec![V2Component {
                            name: "baz".to_string(),
                            url: "fuchsia-pkg://fuchsia.com/cs-tests#meta/baz.cm".to_string(),
                            id: 0,
                            component_type: "static".to_string(),
                            appmgr_root_v1_realm: None,
                            execution: Some(Execution {
                                elf_runtime: None,
                                incoming_capabilities: vec!["pkg".to_string()],
                                outgoing_capabilities: None,
                                exposed_capabilities: vec![],
                            }),
                            children: vec![],
                        }],
                    },
                    V2Component {
                        name: "baz".to_string(),
                        url: "fuchsia-pkg://fuchsia.com/cs-tests#meta/baz.cm".to_string(),
                        id: 0,
                        component_type: "static".to_string(),
                        appmgr_root_v1_realm: None,
                        execution: Some(Execution {
                            elf_runtime: None,
                            incoming_capabilities: vec!["pkg".to_string()],
                            outgoing_capabilities: None,
                            exposed_capabilities: vec![],
                        }),
                        children: vec![],
                    },
                ],
            },
        ],
    };

    assert_eq!(actual_root_component, expected_root_component);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn echo_realm() {
    let test =
        OpaqueTest::default("fuchsia-pkg://fuchsia.com/cs-tests#meta/echo_realm.cm").await.unwrap();
    let event_source = test.connect_to_event_source().await.unwrap();

    {
        let mut event_stream = event_source.subscribe(vec![Started::NAME]).await.unwrap();
        event_source.start_component_tree().await;

        // 3 components are started. Order is irrelevant.
        // root and echo_client are started eagerly.
        // echo_server is started after echo_client connects to the Echo service.
        for _ in 1..=3 {
            let event = EventMatcher::ok().expect_match::<Started>(&mut event_stream).await;
            event.resume().await.unwrap();
        }
    }

    let hub_v2_path = test.get_hub_v2_path();
    let actual_root_component = V2Component::explore(hub_v2_path).await;

    let expected_root_component = V2Component {
        name: "<root>".to_string(),
        url: "fuchsia-pkg://fuchsia.com/cs-tests#meta/echo_realm.cm".to_string(),
        id: 0,
        component_type: "static".to_string(),
        appmgr_root_v1_realm: None,
        execution: Some(Execution {
            elf_runtime: None,
            incoming_capabilities: vec!["pkg".to_string()],
            outgoing_capabilities: None,
            exposed_capabilities: vec![],
        }),
        children: vec![
            V2Component {
                name: "echo_server".to_string(),
                url: "fuchsia-pkg://fuchsia.com/cs-tests#meta/echo_server.cm".to_string(),
                id: 0,
                component_type: "static".to_string(),
                appmgr_root_v1_realm: None,
                execution: Some(Execution {
                    elf_runtime: Some(ElfRuntime {
                        job_id: 42,     // This number is irrelevant
                        process_id: 42, // This number is irrelevant
                    }),
                    incoming_capabilities: vec!["pkg".to_string()],
                    outgoing_capabilities: Some(
                        vec!["fidl.examples.routing.echo.Echo".to_string()],
                    ),
                    exposed_capabilities: vec![
                        "fidl.examples.routing.echo.Echo".to_string(),
                        "hub".to_string(),
                    ],
                }),
                children: vec![],
            },
            V2Component {
                name: "indef_echo_client".to_string(),
                url: "fuchsia-pkg://fuchsia.com/cs-tests#meta/indef_echo_client.cm".to_string(),
                id: 0,
                component_type: "static".to_string(),
                appmgr_root_v1_realm: None,
                execution: Some(Execution {
                    elf_runtime: Some(ElfRuntime {
                        job_id: 42,     // This number is irrelevant
                        process_id: 42, // This number is irrelevant
                    }),
                    incoming_capabilities: vec![
                        "fidl.examples.routing.echo.Echo".to_string(),
                        "fuchsia.logger.LogSink".to_string(),
                        "pkg".to_string(),
                    ],
                    outgoing_capabilities: None,
                    exposed_capabilities: vec![],
                }),
                children: vec![],
            },
        ],
    };

    assert_eq!(actual_root_component, expected_root_component);
}
