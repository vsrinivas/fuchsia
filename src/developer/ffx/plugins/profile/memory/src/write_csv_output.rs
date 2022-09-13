// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::digest, crate::plugin_output::ProcessesMemoryUsage, crate::ProfileMemoryOutput,
    anyhow::Result, digest::processed, ffx_writer::Writer, std::io::Write,
};

/// Print to `w` a csv presentation of `processes`.
fn print_processes_digest(w: &mut Writer, processes: ProcessesMemoryUsage) -> Result<()> {
    for process in processes.process_data {
        writeln!(
            w,
            "{},{},{},{},{},{}",
            processes.capture_time,
            process.koid,
            process.name,
            process.memory.private,
            process.memory.scaled,
            process.memory.total
        )?;
    }
    Ok(())
}

/// Print to `w` a human-readable presentation of `digest`.
fn print_complete_digest(w: &mut Writer, digest: processed::Digest) -> Result<()> {
    print_processes_digest(
        w,
        ProcessesMemoryUsage { process_data: digest.processes, capture_time: digest.time },
    )?;
    writeln!(w)?;
    Ok(())
}

/// Print to `w` a csv presentation of `output`.
pub fn write_csv_output<'a>(w: &mut Writer, output: ProfileMemoryOutput) -> Result<()> {
    match output {
        ProfileMemoryOutput::CompleteDigest(digest) => print_complete_digest(w, digest),
        ProfileMemoryOutput::ProcessDigest(processes_digest) => {
            print_processes_digest(w, processes_digest)
        }
    }
}
