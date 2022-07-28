// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Utilities that prints information in a human-readable format.

use {
    crate::digest,
    crate::ProfileMemoryOutput,
    anyhow::Result,
    digest::processed,
    ffx_writer::Writer,
    humansize::{file_size_opts::BINARY, FileSize},
    std::io::Write,
};

/// Print to `w` a human-readable presentation of `processes`.
fn print_processes_digest(w: &mut Writer, processes: Vec<processed::Process>) -> Result<()> {
    for process in processes {
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
    Ok(())
}

/// Print to `w` a human-readable presentation of `digest`.
fn print_complete_digest(mut w: Writer, digest: processed::Digest) -> Result<()> {
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
    print_processes_digest(&mut w, digest.processes)?;
    writeln!(w)?;
    Ok(())
}

/// Print to `w` a human-readable presentation of `output`.
pub fn write_human_readable_output<'a>(mut w: Writer, output: ProfileMemoryOutput) -> Result<()> {
    match output {
        ProfileMemoryOutput::CompleteDigest(digest) => print_complete_digest(w, digest),
        ProfileMemoryOutput::ProcessDigest(processes_digest) => {
            print_processes_digest(&mut w, processes_digest)
        }
    }
}
