// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::table::*,
    anyhow::Error,
    fidl_fuchsia_inspect_deprecated::{self as fidl_inspect, InspectRequest, InspectRequestStream},
    fuchsia_async as fasync,
    futures::{TryFutureExt, TryStreamExt},
};

pub fn spawn_inspect_server(mut stream: InspectRequestStream, node: NodeObject) {
    fasync::spawn(
        async move {
            while let Some(req) = stream.try_next().await? {
                match req {
                    InspectRequest::ReadData { responder } => {
                        let metrics = get_metrics(&node);
                        let properties = get_properties(&node);
                        let mut object = fidl_inspect::Object {
                            name: node.get_node_name(),
                            properties,
                            metrics,
                        };
                        responder.send(&mut object)?;
                    }
                    InspectRequest::ListChildren { responder } => {
                        let mut names = get_children_names(&node);
                        names.sort();
                        responder.send(&mut names.into_iter())?;
                    }
                    InspectRequest::OpenChild { child_name, child_channel, responder } => {
                        let stream = child_channel.into_stream()?;
                        let ok = match &node {
                            NodeObject::Root(table) => {
                                spawn_inspect_server(stream, NodeObject::Table(table.clone()));
                                true
                            }
                            NodeObject::Row(row) => {
                                match row.cells.iter().find(|c| c.node_name == child_name) {
                                    Some(child) => {
                                        spawn_inspect_server(
                                            stream,
                                            NodeObject::Cell(child.clone()),
                                        );
                                        true
                                    }
                                    None => false,
                                }
                            }
                            NodeObject::Table(table) => {
                                match table.rows.iter().find(|c| c.node_name == child_name) {
                                    Some(child) => {
                                        spawn_inspect_server(
                                            stream,
                                            NodeObject::Row(child.clone()),
                                        );
                                        true
                                    }
                                    None => false,
                                }
                            }
                            _ => false,
                        };
                        responder.send(ok)?;
                    }
                }
            }
            Ok(())
        }
        .unwrap_or_else(|e: Error| eprintln!("error running inspect server: {:?}", e)),
    );
}

fn get_children_names(node: &NodeObject) -> Vec<&str> {
    match node {
        NodeObject::Table(table) => {
            table.rows.iter().map(|row| row.node_name.as_ref()).collect::<Vec<&str>>()
        }
        NodeObject::Row(row) => {
            row.cells.iter().map(|cell| cell.node_name.as_ref()).collect::<Vec<&str>>()
        }
        NodeObject::Root(table) => vec![table.node_name.as_ref()],
        _ => vec![],
    }
}

fn get_metrics(node: &NodeObject) -> Vec<fidl_inspect::Metric> {
    match node {
        NodeObject::Cell(cell) => vec![
            fidl_inspect::Metric {
                key: "double_value".to_string(),
                value: fidl_inspect::MetricValue::DoubleValue(cell.double_value),
            },
            fidl_inspect::Metric {
                key: "value".to_string(),
                value: fidl_inspect::MetricValue::IntValue(cell.value),
            },
        ],
        _ => vec![],
    }
}

fn get_properties(node: &NodeObject) -> Vec<fidl_inspect::Property> {
    match node {
        NodeObject::Cell(cell) => vec![fidl_inspect::Property {
            key: "name".to_string(),
            value: fidl_inspect::PropertyValue::Str(cell.name.clone()),
        }],
        NodeObject::Table(table) => vec![
            fidl_inspect::Property {
                key: "binary_data".to_string(),
                value: fidl_inspect::PropertyValue::Bytes(table.binary_data.clone()),
            },
            fidl_inspect::Property {
                key: "object_name".to_string(),
                value: fidl_inspect::PropertyValue::Str(table.object_name.clone()),
            },
        ],
        _ => vec![],
    }
}
