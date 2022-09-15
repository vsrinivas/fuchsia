// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Library that obtains and prints information about all processes of a running fuchsia device.

mod processes_data;

use {
    anyhow::Result,
    ffx_core::ffx_plugin,
    ffx_process_explorer_args::QueryCommand,
    fidl_fuchsia_process_explorer::QueryProxy,
    fuchsia_zircon_types as zx_types,
    futures::AsyncReadExt,
    processes_data::{processed, raw},
    std::io::Write,
};

// TODO(fxbug.dev/107973): The plugin must remain experimental until the FIDL API is strongly typed.
#[ffx_plugin("ffx_process_explorer", QueryProxy = "core/appmgr:out:fuchsia.process.explorer.Query")]
/// Prints processes data.
pub async fn print_processes_data(query_proxy: QueryProxy, _cmd: QueryCommand) -> Result<()> {
    let s = get_processes_data(query_proxy).await?;
    // TODO(fxbug.dev/#107974): Print the raw JSON only when "--machine=json" is passed.
    pretty_print_processes_data(processed::ProcessesData::from(s))
}

/// Returns a JSON string containing all processes data obtained via the QueryProxyInterface.
async fn get_processes_data(
    query_proxy: impl fidl_fuchsia_process_explorer::QueryProxyInterface,
) -> Result<raw::ProcessesData> {
    // Create a socket.
    let (rx, tx) = fidl::Socket::create(fidl::SocketOpts::STREAM)?;

    // Send one end of the socket to the remote device.
    query_proxy.write_json_processes_data(tx)?;

    // Read all the bytes sent from the other end of the socket.
    let mut rx_async = fidl::AsyncSocket::from_socket(rx)?;
    let mut buffer = Vec::new();
    rx_async.read_to_end(&mut buffer).await?;
    Ok(serde_json::from_slice(&buffer)?)
}

/// Print to stdout a human-readable presentation of `processes_data`.
fn pretty_print_processes_data(processes_data: processed::ProcessesData) -> Result<()> {
    let stdout = std::io::stdout().lock();
    let mut w = std::io::BufWriter::new(stdout);

    writeln!(w, "Total processes found:    {}", processes_data.processes_count)?;
    writeln!(w)?;
    for process in processes_data.processes {
        writeln!(w, "Process name:             {}", process.name)?;
        writeln!(w, "Process koid:             {}", process.koid)?;
        writeln!(w, "Total objects:            {}", process.objects_count)?;
        for objects_by_type in process.objects {
            writeln!(
                w,
                "   {}: {}",
                get_object_type_name(objects_by_type.objects_type),
                objects_by_type.objects_count
            )?;
            for object in objects_by_type.objects {
                writeln!(
                    w,
                    "         Koid: {:6}    Related Koid: {:6}    Peer Owner Koid: {:6}",
                    object.koid, object.related_koid, object.peer_owner_koid
                )?;
            }
        }
        writeln!(w, "===========================================================================")?;
    }
    writeln!(w)?;
    Ok(())
}

/// Convert objects type from zx_obj_type_t to a string
/// to make the information more readable.
fn get_object_type_name(object_type: zx_types::zx_obj_type_t) -> String {
    match object_type {
        zx_types::ZX_OBJ_TYPE_NONE => "None",
        zx_types::ZX_OBJ_TYPE_PROCESS => "Processes",
        zx_types::ZX_OBJ_TYPE_THREAD => "Threads",
        zx_types::ZX_OBJ_TYPE_VMO => "VMOs",
        zx_types::ZX_OBJ_TYPE_CHANNEL => "Channels",
        zx_types::ZX_OBJ_TYPE_EVENT => "Events",
        zx_types::ZX_OBJ_TYPE_PORT => "Ports",
        zx_types::ZX_OBJ_TYPE_INTERRUPT => "Interrupts",
        zx_types::ZX_OBJ_TYPE_PCI_DEVICE => "PCI Devices",
        zx_types::ZX_OBJ_TYPE_DEBUGLOG => "Debuglogs",
        zx_types::ZX_OBJ_TYPE_SOCKET => "Sockets",
        zx_types::ZX_OBJ_TYPE_RESOURCE => "Resources",
        zx_types::ZX_OBJ_TYPE_EVENTPAIR => "Event pairs",
        zx_types::ZX_OBJ_TYPE_JOB => "Jobs",
        zx_types::ZX_OBJ_TYPE_VMAR => "VMARs",
        zx_types::ZX_OBJ_TYPE_FIFO => "FIFOs",
        zx_types::ZX_OBJ_TYPE_GUEST => "Guests",
        zx_types::ZX_OBJ_TYPE_VCPU => "VCPUs",
        zx_types::ZX_OBJ_TYPE_TIMER => "Timers",
        zx_types::ZX_OBJ_TYPE_IOMMU => "IOMMUs",
        zx_types::ZX_OBJ_TYPE_BTI => "BTIs",
        zx_types::ZX_OBJ_TYPE_PROFILE => "Profiles",
        zx_types::ZX_OBJ_TYPE_PMT => "PMTs",
        zx_types::ZX_OBJ_TYPE_SUSPEND_TOKEN => "Suspend tokens",
        zx_types::ZX_OBJ_TYPE_PAGER => "Pagers",
        zx_types::ZX_OBJ_TYPE_EXCEPTION => "Exceptions",
        zx_types::ZX_OBJ_TYPE_CLOCK => "Clocks",
        zx_types::ZX_OBJ_TYPE_STREAM => "Streams",
        _ => "Error",
    }
    .to_string()
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
    }

    use fidl_fuchsia_process_explorer::QueryRequest;

    /// Returns a fake query service that writes `EXPECTED_PROCESSES_DATA` to the socket
    /// when `WriteJsonProcessesData` is called.
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

    /// Tests that `get_processes_data` properly reads data from the query service.
    #[fuchsia_async::run_singlethreaded(test)]
    async fn get_processes_data_test() {
        let query_proxy = setup_fake_query_svc();
        let processes_data =
            get_processes_data(query_proxy).await.expect("failed to get processes_data");
        assert_eq!(processes_data, *EXPECTED_PROCESSES_DATA);
    }
}
