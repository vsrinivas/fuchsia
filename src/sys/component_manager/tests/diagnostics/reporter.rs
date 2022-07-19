// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    diagnostics_reader::{assert_data_tree, AnyProperty, ArchiveReader, Inspect},
    fidl_fuchsia_sys2 as fsys, fuchsia_fs,
    std::path::Path,
};

async fn get_job_koid(moniker: &str, realm_query: &fsys::RealmQueryProxy) -> u64 {
    let (_, resolved) = realm_query.get_instance_info(moniker).await.unwrap().unwrap();
    let resolved = resolved.unwrap();
    let started = resolved.started.unwrap();
    let runtime_dir = started.runtime_dir.unwrap();
    let runtime_dir = runtime_dir.into_proxy().unwrap();
    let file_proxy = fuchsia_fs::open_file(
        &runtime_dir,
        &Path::new("elf/job_id"),
        fuchsia_fs::OpenFlags::RIGHT_READABLE,
    )
    .expect("Failed to open file.");
    let res = fuchsia_fs::read_file(&file_proxy).await;
    let contents = res.expect("Unable to read file.");
    contents.parse::<u64>().unwrap()
}

#[fuchsia::main]
async fn main() {
    let data = ArchiveReader::new()
        .add_selector("<component_manager>:root")
        .snapshot::<Inspect>()
        .await
        .expect("got inspect data");

    let realm_query =
        fuchsia_component::client::connect_to_protocol::<fsys::RealmQueryMarker>().unwrap();

    let archivist_job_koid = get_job_koid("./archivist", &realm_query).await;
    let reporter_job_koid = get_job_koid("./reporter", &realm_query).await;

    assert_eq!(data.len(), 1, "expected 1 match: {:?}", data);
    assert_data_tree!(data[0].payload.as_ref().unwrap(), root: {
        "fuchsia.inspect.Health": {
            start_timestamp_nanos: AnyProperty,
            status: "OK"
        },
        early_start_times: {
            "0": {
                moniker: "/",
                time: AnyProperty,
            },
            "1": {
                moniker: "/root",
                time: AnyProperty,
            },
            "2": {
                moniker: "/root/reporter",
                time: AnyProperty,
            },
            "3": {
                moniker: "/root/archivist",
                time: AnyProperty,
            },
        },
        cpu_stats: contains {
            measurements: {
                component_count: 3u64,
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
                    "root/archivist": {
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
                    "root/reporter": {
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
