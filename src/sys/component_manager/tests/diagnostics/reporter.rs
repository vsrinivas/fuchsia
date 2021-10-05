// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    diagnostics_reader::{assert_data_tree, AnyProperty, ArchiveReader, Inspect},
    fidl_fuchsia_io as fio, fuchsia_async as fasync, fuchsia_syslog as syslog, io_util,
    std::path::Path,
};

async fn read_file<'a>(root_proxy: &'a fio::DirectoryProxy, path: &'a str) -> String {
    let file_proxy =
        io_util::open_file(&root_proxy, &Path::new(path), io_util::OPEN_RIGHT_READABLE)
            .expect("Failed to open file.");
    let res = io_util::read_file(&file_proxy).await;
    res.expect("Unable to read file.")
}

#[fasync::run_singlethreaded]
async fn main() {
    syslog::init().unwrap();

    let data = ArchiveReader::new()
        .add_selector("<component_manager>:root")
        .snapshot::<Inspect>()
        .await
        .expect("got inspect data");

    let hub_proxy = io_util::open_directory_in_namespace("/hub", io_util::OPEN_RIGHT_READABLE)
        .expect("Unable to open directory in namespace");
    let archivist_job_koid = read_file(&hub_proxy, "children/archivist/exec/runtime/elf/job_id")
        .await
        .parse::<u64>()
        .unwrap();
    let reporter_job_koid = read_file(&hub_proxy, "children/reporter/exec/runtime/elf/job_id")
        .await
        .parse::<u64>()
        .unwrap();

    assert_eq!(data.len(), 1, "expected 1 match: {:?}", data);
    assert_data_tree!(data[0].payload.as_ref().unwrap(), root: {
        "fuchsia.inspect.Health": {
            start_timestamp_nanos: AnyProperty,
            status: "OK"
        },
        cpu_stats: contains {
            measurements: {
                task_count: 3u64,
                "fuchsia.inspect.Stats": {
                    current_size: AnyProperty,
                    maximum_size: AnyProperty,
                    total_dynamic_children: AnyProperty,
                    allocated_blocks: AnyProperty,
                    deallocated_blocks: AnyProperty,
                    failed_allocations: 0u64,
                },
                components: {
                    "<component_manager>": contains {},
                    "archivist": {
                        archivist_job_koid.to_string() => {
                            "@samples": {
                                "0": {
                                    timestamp: AnyProperty,
                                    cpu_time: AnyProperty,
                                    queue_time: AnyProperty,
                                }
                            }
                        }
                    },
                    "reporter": {
                        reporter_job_koid.to_string() => {
                            "@samples": {
                                "0": {
                                    timestamp: AnyProperty,
                                    cpu_time: AnyProperty,
                                    queue_time: AnyProperty,
                                }
                            }
                        }
                    },
                }
            },
        },
        "fuchsia.inspect.Stats": {
            current_size: AnyProperty,
            maximum_size: AnyProperty,
            total_dynamic_children: AnyProperty,
            allocated_blocks: AnyProperty,
            deallocated_blocks: AnyProperty,
            failed_allocations: 0u64,
        }
    });
}
