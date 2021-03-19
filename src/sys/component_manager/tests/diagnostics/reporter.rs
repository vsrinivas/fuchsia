// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    diagnostics_reader::{assert_data_tree, AnyProperty, ArchiveReader, Inspect},
    fidl_fidl_examples_routing_echo as fecho, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
    fuchsia_syslog as syslog,
};

#[fasync::run_singlethreaded]
async fn main() {
    syslog::init().unwrap();

    let data = ArchiveReader::new()
        .add_selector("<component_manager>:root")
        .snapshot::<Inspect>()
        .await
        .expect("got inspect data");

    assert_eq!(data.len(), 1, "expected 1 match: {:?}", data);
    assert_data_tree!(data[0].payload.as_ref().unwrap(), root: {
        "fuchsia.inspect.Health": {
            start_timestamp_nanos: AnyProperty,
            status: "OK"
        },
        cpu_stats: {
            measurements: {
                "/archivist:0": {
                    "@samples": {
                        "0": {
                            timestamp: AnyProperty,
                            cpu_time: AnyProperty,
                            queue_time: AnyProperty,
                        }
                    }
                },
                "/reporter:0": {
                    "@samples": {
                        "0": {
                            timestamp: AnyProperty,
                            cpu_time: AnyProperty,
                            queue_time: AnyProperty,
                        }
                    }
                },
                "<component_manager>": {
                    "@samples": {
                        "0": {
                            timestamp: AnyProperty,
                            cpu_time: AnyProperty,
                            queue_time: AnyProperty,
                        }
                    }
                },
                inspect_stats: {
                    current_size: 4096u64,
                    maximum_size: 262144u64,
                    total_dynamic_children: 0u64,
                }
            },
            processing_times_ns: AnyProperty,
        },
        inspect_stats: {
            current_size: 4096u64,
            maximum_size: 262144u64,
            total_dynamic_children: 0u64,
        }
    });

    let echo = connect_to_service::<fecho::EchoMarker>().unwrap();
    let _ = echo.echo_string(Some("OK")).await;
}
