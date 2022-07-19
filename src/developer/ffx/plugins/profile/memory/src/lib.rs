// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Library that obtains and prints memory digests of a running fuchsia device.

mod digest;

use {
    anyhow::Result,
    digest::{processed, raw},
    ffx_core::ffx_plugin,
    ffx_profile_memory_args::MemoryCommand,
    ffx_writer::Writer,
    fidl_fuchsia_memory::MonitorProxy,
    futures::AsyncReadExt,
    humansize::{file_size_opts::BINARY, FileSize},
    std::io::Write,
};

#[ffx_plugin("ffx_memory", MonitorProxy = "core/appmgr:out:fuchsia.memory.Monitor")]
/// Prints a memory digest to stdout.
pub async fn print_memory_digest(
    monitor_proxy: MonitorProxy,
    cmd: MemoryCommand,
    #[ffx(machine = processed::Digest)] mut writer: Writer,
) -> Result<()> {
    if cmd.print_json_from_memory_monitor {
        let raw_data = get_raw_data(monitor_proxy).await?;
        writeln!(writer, "{}", String::from_utf8(raw_data)?)?;
        Ok(())
    } else {
        if writer.is_machine() {
            let digest = get_digest(monitor_proxy).await?;
            writer.machine(&processed::Digest::from(digest))?;
            Ok(())
        } else {
            let digest = get_digest(monitor_proxy).await?;
            pretty_print_full_digest(writer, processed::Digest::from(digest))
        }
    }
}

/// Returns a buffer containing the data that MemoryMonitor wrote.
async fn get_raw_data(
    monitor_proxy: impl fidl_fuchsia_memory::MonitorProxyInterface,
) -> Result<Vec<u8>> {
    // Create a socket.
    let (rx, tx) = fidl::Socket::create(fidl::SocketOpts::STREAM)?;

    // Send one end of the socket to the remote device.
    monitor_proxy.write_json_capture(tx)?;

    // Read all the bytes sent from the other end of the socket.
    let mut rx_async = fidl::AsyncSocket::from_socket(rx)?;
    let mut buffer = Vec::new();
    rx_async.read_to_end(&mut buffer).await?;

    Ok(buffer)
}

/// Returns a `Digest` obtained via the `MonitorProxyInterface`. Performs basic schema validation.
async fn get_digest(
    monitor_proxy: impl fidl_fuchsia_memory::MonitorProxyInterface,
) -> anyhow::Result<raw::Digest> {
    let buffer = get_raw_data(monitor_proxy).await?;
    Ok(serde_json::from_slice(&buffer)?)
}

/// Print to `w` a human-readable presentation of `digest`.
fn pretty_print_full_digest<'a>(mut w: Writer, digest: processed::Digest) -> Result<()> {
    writeln!(w, "Time:  {}", digest.time)?;
    writeln!(w, "VMO:   {}", digest.kernel.vmo.file_size(BINARY).unwrap())?;
    writeln!(w, "Free:  {}", digest.kernel.free.file_size(BINARY).unwrap())?;
    writeln!(w)?;
    writeln!(w, "Task:      kernel")?;
    writeln!(w, "PID:       1")?;
    let kernel_total = digest.kernel.wired
        + digest.kernel.vmo
        + digest.kernel.total_heap
        + digest.kernel.mmu
        + digest.kernel.ipc;
    writeln!(w, "Total:     {}", kernel_total.file_size(BINARY).unwrap())?;
    writeln!(w, "    wired: {}", digest.kernel.wired.file_size(BINARY).unwrap())?;
    writeln!(w, "    vmo:   {}", digest.kernel.vmo.file_size(BINARY).unwrap())?;
    writeln!(w, "    heap:  {}", digest.kernel.total_heap.file_size(BINARY).unwrap())?;
    writeln!(w, "    mmu:   {}", digest.kernel.mmu.file_size(BINARY).unwrap())?;
    writeln!(w, "    ipc:   {}", digest.kernel.ipc.file_size(BINARY).unwrap())?;
    writeln!(w)?;
    for process in digest.processes {
        writeln!(w, "Task:          {}", process.name)?;
        writeln!(w, "PID:           {}", process.koid)?;
        writeln!(w, "Private Bytes: {}", process.memory.private.file_size(BINARY).unwrap())?;
        writeln!(w, "Total(Shared): {}", process.memory.scaled.file_size(BINARY).unwrap())?;
        writeln!(w, "Total:         {}", process.memory.total.file_size(BINARY).unwrap())?;
        let names = {
            let mut names: Vec<&String> = process.name_to_memory.keys().collect();
            names.sort_unstable_by(|&a, &b| {
                let sa = process.name_to_memory.get(a).unwrap();
                let sb = process.name_to_memory.get(b).unwrap();
                (sb.private, sb.scaled).cmp(&(sa.private, sa.scaled))
            });
            names
        };
        for name in names {
            if let Some(sizes) = process.name_to_memory.get(name) {
                if sizes.total == 0 {
                    continue;
                }
                // If the VMO is not shared between multiple
                // processes, all three metrics are equivalent, and
                // there is no point in printing all of them.
                if sizes.total == sizes.private {
                    writeln!(w, "    {}: {}", name, sizes.total.file_size(BINARY).unwrap())?;
                } else {
                    writeln!(
                        w,
                        "    {}: {} {} {}",
                        name,
                        sizes.private.file_size(BINARY).unwrap(),
                        sizes.scaled.file_size(BINARY).unwrap(),
                        sizes.total.file_size(BINARY).unwrap()
                    )?;
                }
            }
        }
        writeln!(w)?;
    }
    writeln!(w)?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use futures::AsyncWriteExt;

    lazy_static::lazy_static! {
    static ref EXPECTED_DIGEST: raw::Digest = raw::Digest{
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

    static ref DATA_WRITTEN_BY_MEMORY_MONITOR: Vec<u8> = serde_json::to_vec(&*EXPECTED_DIGEST).unwrap();

    }

    use fidl_fuchsia_memory::MonitorRequest;

    /// Returns a fake monitor service that writes `EXPECTED_DIGEST` serialized to JSON to the socket
    /// when `WriteJsonCapture` is called.
    fn setup_fake_monitor_svc() -> MonitorProxy {
        setup_fake_monitor_proxy(|request| match request {
            MonitorRequest::Watch { watcher: _, .. } => {}
            MonitorRequest::WriteJsonCapture { socket, .. } => {
                let mut s = fidl::AsyncSocket::from_socket(socket).unwrap();
                fuchsia_async::Task::local(async move {
                    s.write_all(&DATA_WRITTEN_BY_MEMORY_MONITOR).await.unwrap();
                })
                .detach();
            }
        })
    }

    /// Tests that `get_raw_data` properly reads data from the memory monitor service.
    #[fuchsia_async::run_singlethreaded(test)]
    async fn get_raw_data_test() {
        let monitor_proxy = setup_fake_monitor_svc();
        let raw_data = get_raw_data(monitor_proxy).await.expect("failed to get raw data");
        assert_eq!(raw_data, *DATA_WRITTEN_BY_MEMORY_MONITOR);
    }

    /// Tests that `get_digest` properly reads and parses data from the memory monitor service.
    #[fuchsia_async::run_singlethreaded(test)]
    async fn get_digest_test() {
        let monitor_proxy = setup_fake_monitor_svc();
        let digest = get_digest(monitor_proxy).await.expect("failed to get digest");
        assert_eq!(digest, *EXPECTED_DIGEST);
    }
}
