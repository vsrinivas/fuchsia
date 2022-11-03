// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Library that obtains and prints memory digests of a running fuchsia device.

mod bucket;
mod digest;
mod plugin_output;
mod write_csv_output;
mod write_human_readable_output;

use {
    crate::plugin_output::filter_digest_by_process_koids,
    crate::write_csv_output::write_csv_output,
    crate::write_human_readable_output::write_human_readable_output,
    anyhow::Result,
    digest::{processed, raw},
    ffx_core::ffx_plugin,
    ffx_profile_memory_args::MemoryCommand,
    ffx_writer::Writer,
    fidl_fuchsia_memory::MonitorProxy,
    futures::AsyncReadExt,
    plugin_output::ProfileMemoryOutput,
    std::io::Write,
    std::time::Duration,
};

#[ffx_plugin("ffx_memory", MonitorProxy = "core/memory_monitor:expose:fuchsia.memory.Monitor")]
/// Prints a memory digest to stdout.
pub async fn plugin_entrypoint(
    monitor_proxy: MonitorProxy,
    cmd: MemoryCommand,
    #[ffx(machine = ProfileMemoryOutput)] mut writer: Writer,
) -> Result<()> {
    // Either call `print_output` once, or call `print_output` repeatedly every `interval` seconds
    // until the user presses ctrl-C.
    match cmd.interval {
        None => print_output(&monitor_proxy, &cmd, &mut writer).await?,
        Some(interval) => loop {
            print_output(&monitor_proxy, &cmd, &mut writer).await?;
            fuchsia_async::Timer::new(Duration::from_secs_f64(interval)).await;
        },
    }
    Ok(())
}

pub async fn print_output(
    monitor_proxy: &MonitorProxy,
    cmd: &MemoryCommand,
    writer: &mut Writer,
) -> Result<()> {
    if cmd.debug_json {
        let raw_data = get_raw_data(monitor_proxy).await?;
        writeln!(writer, "{}", String::from_utf8(raw_data)?)?;
        Ok(())
    } else {
        let memory_monitor_output = get_output(monitor_proxy).await?;
        let processed_digest =
            processed::digest_from_memory_monitor_output(memory_monitor_output, cmd.buckets);
        let output = match cmd.process_koids.len() {
            0 => ProfileMemoryOutput::CompleteDigest(processed_digest),
            _ => filter_digest_by_process_koids(processed_digest, &cmd.process_koids),
        };
        if cmd.csv {
            write_csv_output(writer, output)
        } else {
            if writer.is_machine() {
                writer.machine(&output)
            } else {
                write_human_readable_output(writer, output, cmd.exact_sizes)
            }
        }
    }
}

/// Returns a buffer containing the data that MemoryMonitor wrote.
async fn get_raw_data(monitor_proxy: &MonitorProxy) -> Result<Vec<u8>> {
    // Create a socket.
    let (rx, tx) = fidl::Socket::create(fidl::SocketOpts::STREAM)?;

    // Send one end of the socket to the remote device.
    monitor_proxy.write_json_capture_and_buckets(tx)?;

    // Read all the bytes sent from the other end of the socket.
    let mut rx_async = fidl::AsyncSocket::from_socket(rx)?;
    let mut buffer = Vec::new();
    rx_async.read_to_end(&mut buffer).await?;

    Ok(buffer)
}

/// Returns the `MemoryMonitorOutput` obtained via the `MonitorProxyInterface`. Performs basic schema validation.
async fn get_output(monitor_proxy: &MonitorProxy) -> anyhow::Result<raw::MemoryMonitorOutput> {
    let buffer = get_raw_data(monitor_proxy).await?;
    Ok(serde_json::from_slice(&buffer)?)
}

#[cfg(test)]
mod tests {
    use super::*;
    use futures::AsyncWriteExt;

    lazy_static::lazy_static! {
    static ref EXPECTED_CAPTURE: raw::Capture = raw::Capture{
        time: 0,
        kernel: raw::Kernel{
        total: 0,
        free: 0,
        wired: 0,
        total_heap: 0,
        free_heap: 0,
        vmo: 0,
        vmo_pager_total: 0,
        vmo_pager_newest: 0,
        vmo_pager_oldest: 0,
        vmo_discardable_locked: 0,
        vmo_discardable_unlocked: 0,
        mmu: 0,
        ipc: 0,
        other: 0,
        },
        processes: vec![
        raw::Process::Headers(raw::ProcessHeaders::default()),
        raw::Process::Data(raw::ProcessData{koid: 2, name: "process1".to_string(), vmos: vec![1, 2, 3]}),
        raw::Process::Data(raw::ProcessData{koid: 3, name: "process2".to_string(), vmos: vec![2, 3, 4]}),
        ],
        vmo_names: vec!["name1".to_string(), "name2".to_string()],
        vmos: vec![],
    };

    static ref EXPECTED_OUTPUT: raw::MemoryMonitorOutput = raw::MemoryMonitorOutput{
        capture: EXPECTED_CAPTURE.clone(),
        buckets_definitions: vec![]
    };

    static ref DATA_WRITTEN_BY_MEMORY_MONITOR: Vec<u8> = serde_json::to_vec(&*EXPECTED_OUTPUT).unwrap();

    }

    use fidl_fuchsia_memory::MonitorRequest;

    /// Returns a fake monitor service that writes `EXPECTED_OUTPUT` serialized to JSON to the socket
    /// when `WriteJsonCaptureAndBuckets` is called.
    fn setup_fake_monitor_svc() -> MonitorProxy {
        setup_fake_monitor_proxy(|request| match request {
            MonitorRequest::Watch { watcher: _, .. } => {}
            MonitorRequest::WriteJsonCaptureAndBuckets { socket, .. } => {
                let mut s = fidl::AsyncSocket::from_socket(socket).unwrap();
                fuchsia_async::Task::local(async move {
                    s.write_all(&DATA_WRITTEN_BY_MEMORY_MONITOR).await.unwrap();
                })
                .detach();
            }
            _ => unimplemented!(),
        })
    }

    /// Tests that `get_raw_data` properly reads data from the memory monitor service.
    #[fuchsia_async::run_singlethreaded(test)]
    async fn get_raw_data_test() {
        let monitor_proxy = setup_fake_monitor_svc();
        let raw_data = get_raw_data(&monitor_proxy).await.expect("failed to get raw data");
        assert_eq!(raw_data, *DATA_WRITTEN_BY_MEMORY_MONITOR);
    }

    /// Tests that `get_output` properly reads and parses data from the memory monitor service.
    #[fuchsia_async::run_singlethreaded(test)]
    async fn get_output_test() {
        let monitor_proxy = setup_fake_monitor_svc();
        let output = get_output(&monitor_proxy).await.expect("failed to get output");
        assert_eq!(output, *EXPECTED_OUTPUT);
    }
}
