// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Library that obtains and prints information about all processes of a running fuchsia device.

mod fuchsia_map;
mod processes_data;
mod write_human_readable_output;

use {
    anyhow::Result,
    ffx_core::ffx_plugin,
    ffx_process_explorer_args::{Args, QueryCommand},
    ffx_writer::Writer,
    fidl_fuchsia_process_explorer::QueryProxy,
    fuchsia_map::json,
    fuchsia_zircon_types::zx_koid_t,
    futures::AsyncReadExt,
    processes_data::{processed, raw},
    std::collections::HashSet,
    std::io::Write,
    write_human_readable_output::{
        pretty_print_invalid_koids, pretty_print_processes_data,
        pretty_print_processes_name_and_koid,
    },
};

// TODO(fxbug.dev/107973): The plugin must remain experimental until the FIDL API is strongly typed.
#[ffx_plugin("ffx_process_explorer", QueryProxy = "core/appmgr:out:fuchsia.process.explorer.Query")]
/// Prints processes data.
pub async fn print_processes_data(
    query_proxy: QueryProxy,
    cmd: QueryCommand,
    #[ffx(machine = processed::ProcessesData)] writer: Writer,
) -> Result<()> {
    let processes_data = get_processes_data(query_proxy).await?;
    let output = processed::ProcessesData::from(processes_data);

    match cmd.arg {
        Args::List(arg) => list_subcommand(writer, output, arg.verbose),
        Args::Filter(arg) => filter_subcommand(writer, output, arg.process_koids),
        Args::GenerateFuchsiaMap(_) => generate_fuchsia_map_subcommand(writer, output),
    }
}

/// Returns a buffer containing the data obtained via the QueryProxyInterface.
async fn get_raw_data(
    query_proxy: impl fidl_fuchsia_process_explorer::QueryProxyInterface,
) -> Result<Vec<u8>> {
    // Create a socket.
    let (rx, tx) = fidl::Socket::create(fidl::SocketOpts::STREAM)?;

    // Send one end of the socket to the remote device.
    query_proxy.write_json_processes_data(tx)?;

    // Read all the bytes sent from the other end of the socket.
    let mut rx_async = fidl::AsyncSocket::from_socket(rx)?;
    let mut buffer = Vec::new();
    rx_async.read_to_end(&mut buffer).await?;

    Ok(buffer)
}

/// Returns data structured according to ProcessesData obtained via the `QueryProxyInterface`. Performs basic schema validation.
async fn get_processes_data(
    query_proxy: impl fidl_fuchsia_process_explorer::QueryProxyInterface,
) -> Result<raw::ProcessesData> {
    let buffer = get_raw_data(query_proxy).await?;
    Ok(serde_json::from_slice(&buffer)?)
}

/// Returns data that contains information related to the processes contained in `koids`, and a vector containing any invalid koids (if any).
fn filter_by_process_koids(
    processes_data: processed::ProcessesData,
    koids: Vec<zx_koid_t>,
) -> (processed::ProcessesData, Vec<zx_koid_t>) {
    let koids_set: HashSet<zx_koid_t> = HashSet::from_iter(koids);
    let mut filtered_processes = Vec::new();
    let mut filtered_processes_koids: HashSet<zx_koid_t> = HashSet::new();

    for process in processes_data.processes {
        if koids_set.contains(&process.koid) {
            filtered_processes_koids.insert(process.koid);
            filtered_processes.push(process);
        }
    }

    let mut invalid_koids: Vec<zx_koid_t> = Vec::new();
    for koid in koids_set {
        if !filtered_processes_koids.contains(&koid) {
            invalid_koids.push(koid);
        }
    }

    (
        processed::ProcessesData {
            processes_count: filtered_processes.len(),
            processes: filtered_processes,
        },
        invalid_koids,
    )
}

fn list_subcommand(
    w: Writer,
    processes_data: processed::ProcessesData,
    verbose: bool,
) -> Result<()> {
    if verbose {
        if w.is_machine() {
            w.machine(&processes_data)?;
            Ok(())
        } else {
            pretty_print_processes_data(w, processes_data)
        }
    } else {
        pretty_print_processes_name_and_koid(w, processes_data)
    }
}

fn filter_subcommand(
    w: Writer,
    processes_data: processed::ProcessesData,
    koids: Vec<zx_koid_t>,
) -> Result<()> {
    let (filtered_output, invalid_koids) = filter_by_process_koids(processes_data, koids);
    if invalid_koids.len() > 0 {
        pretty_print_invalid_koids(w, invalid_koids)
    } else {
        pretty_print_processes_data(w, filtered_output)
    }
}

fn generate_fuchsia_map_subcommand(
    mut w: Writer,
    processes_data: processed::ProcessesData,
) -> Result<()> {
    let json = json::Json::from(processes_data);
    let serialized = serde_json::to_string(&json).unwrap();
    writeln!(w, "{}", serialized)?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use futures::AsyncWriteExt;

    lazy_static::lazy_static! {
    static ref EXPECTED_PROCESSES_DATA: raw::ProcessesData = raw::ProcessesData{
        processes: vec![
            raw::Process {
                koid: 1,
                name: "process1".to_string(),
                objects: vec![
                    raw::KernelObject {
                        object_type: 4,
                        koid: 78,
                        related_koid: 79,
                        peer_owner_koid: 2,
                    },
                    raw::KernelObject {
                        object_type: 4,
                        koid: 52,
                        related_koid: 53,
                        peer_owner_koid: 0,
                    },
                    raw::KernelObject {
                        object_type: 17,
                        koid: 36,
                        related_koid: 0,
                        peer_owner_koid: 0,
                    },
                ],
            },
            raw::Process {
                koid: 2,
                name: "process2".to_string(),
                objects: vec![
                    raw::KernelObject {
                        object_type: 19,
                        koid: 28,
                        related_koid: 0,
                        peer_owner_koid: 0,
                    },
                    raw::KernelObject {
                        object_type: 14,
                        koid: 95,
                        related_koid: 96,
                        peer_owner_koid: 0,
                    },
                    raw::KernelObject {
                        object_type: 4,
                        koid: 79,
                        related_koid: 78,
                        peer_owner_koid: 1,
                    },
                ],
            },
        ],
    };

    static ref DATA_WRITTEN_BY_PROCESS_EXPLORER: Vec<u8> = serde_json::to_vec(&*EXPECTED_PROCESSES_DATA).unwrap();

    }

    use fidl_fuchsia_process_explorer::QueryRequest;

    /// Returns a fake query service that writes `EXPECTED_PROCESSES_DATA` serialized to JSON to the socket when `WriteJsonProcessesData` is called.
    fn setup_fake_query_svc() -> QueryProxy {
        setup_fake_query_proxy(|request| match request {
            QueryRequest::WriteJsonProcessesData { socket, .. } => {
                let mut s = fidl::AsyncSocket::from_socket(socket).unwrap();
                fuchsia_async::Task::local(async move {
                    s.write_all(&serde_json::to_vec(&*EXPECTED_PROCESSES_DATA).unwrap())
                        .await
                        .unwrap();
                })
                .detach();
            }
        })
    }

    /// Tests that `get_raw_data` properly reads data from the process explorer query service.
    #[fuchsia_async::run_singlethreaded(test)]
    async fn get_raw_data_test() {
        let query_proxy = setup_fake_query_svc();
        let raw_data = get_raw_data(query_proxy).await.expect("failed to get raw data");
        assert_eq!(raw_data, *DATA_WRITTEN_BY_PROCESS_EXPLORER);
    }

    /// Tests that `get_processes_data` properly reads and parses data from the query service.
    #[fuchsia_async::run_singlethreaded(test)]
    async fn get_processes_data_test() {
        let query_proxy = setup_fake_query_svc();
        let processes_data =
            get_processes_data(query_proxy).await.expect("failed to get processes_data");
        assert_eq!(processes_data, *EXPECTED_PROCESSES_DATA);
    }
}
