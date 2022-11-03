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
    std::cmp::Reverse,
    std::io::Write,
};

/// Print to `w` a human-readable presentation of `processes`.
fn print_processes_digest(
    w: &mut Writer,
    processes: Vec<processed::Process>,
    size_formatter: fn(u64) -> String,
) -> Result<()> {
    for process in processes {
        writeln!(w, "Process name: {}", process.name)?;
        writeln!(w, "Process koid: {}", process.koid)?;
        writeln!(w, "Private:      {}", size_formatter(process.memory.private))?;
        writeln!(
            w,
            "PSS:          {} (Proportional Set Size)",
            size_formatter(process.memory.scaled)
        )?;
        writeln!(
            w,
            "Total:        {} (Private + Shared unscaled)",
            size_formatter(process.memory.total)
        )?;

        let names = {
            let mut names: Vec<&String> = process.name_to_memory.keys().collect();
            // Filter out names of VMOs that don't use any memory.
            names.retain(|&name| process.name_to_memory.get(name).unwrap().total > 0);
            // Sort the VMO names along the tuple (private, scaled, name of VMO).
            names.sort_by(|&a, &b| {
                let sa = process.name_to_memory.get(a).unwrap();
                let sb = process.name_to_memory.get(b).unwrap();
                // Sort along decreasing memory sizes and increasing lexical order for names.
                let tuple_1 = (sa.private, sa.scaled, &b);
                let tuple_2 = (sb.private, sb.scaled, &a);
                tuple_2.cmp(&tuple_1)
            });
            names
        };
        // Find the longest name. Use that length during formatting to align the column after
        // the name of VMOs.
        let process_name_trailing_padding = names.iter().map(|name| name.len()).max().unwrap_or(0);
        // The spacing between the columns containing sizes.
        // 12 was chosen to accommodate the following worst case: "1234567890 B"
        let padding_between_number_columns = 12;
        // Write the heading of the table of VMOs.
        writeln!(
            w,
            "    {:<p1$} {:>p2$} {:>p2$} {:>p2$}",
            "",
            "Private",
            "Scaled",
            "Total",
            p1 = process_name_trailing_padding,
            p2 = padding_between_number_columns
        )?;
        // Write the actual content of the table of VMOs.
        for name in names {
            if let Some(sizes) = process.name_to_memory.get(name) {
                let extra_info = if sizes.total == sizes.private { "" } else { "(shared)" };
                writeln!(
                    w,
                    "    {:<p1$} {:>p2$} {:>p2$} {:>p2$} {:>p2$}",
                    name,
                    size_formatter(sizes.private),
                    size_formatter(sizes.scaled),
                    size_formatter(sizes.total),
                    extra_info,
                    p1 = process_name_trailing_padding,
                    p2 = padding_between_number_columns
                )?;
            }
        }
        writeln!(w)?;
    }
    Ok(())
}

/// Print to `w` a human-readable presentation of `digest`.
fn print_complete_digest(
    w: &mut Writer,
    digest: processed::Digest,
    size_formatter: fn(u64) -> String,
) -> Result<()> {
    writeln!(w, "Time:  {} ns", digest.time)?;
    writeln!(w, "VMO:   {}", size_formatter(digest.total_committed_bytes_in_vmos))?;
    writeln!(w, "Free:  {}", size_formatter(digest.kernel.free))?;
    writeln!(w)?;
    writeln!(w, "Task:      kernel")?;
    writeln!(w, "PID:       1")?;
    let kernel_total = digest.kernel.wired
        + digest.kernel.vmo
        + digest.kernel.total_heap
        + digest.kernel.mmu
        + digest.kernel.ipc;
    writeln!(w, "Total:     {}", size_formatter(kernel_total))?;
    writeln!(w, "    wired: {}", size_formatter(digest.kernel.wired))?;
    writeln!(w, "    vmo:   {}", size_formatter(digest.kernel.vmo))?;
    writeln!(w, "    heap:  {}", size_formatter(digest.kernel.total_heap))?;
    writeln!(w, "    mmu:   {}", size_formatter(digest.kernel.mmu))?;
    writeln!(w, "    ipc:   {}", size_formatter(digest.kernel.ipc))?;
    writeln!(w)?;

    let sorted_buckets = {
        let mut sorted_buckets = digest.buckets;
        sorted_buckets.sort_by_key(|bucket| Reverse(bucket.size));
        sorted_buckets
    };

    for bucket in sorted_buckets {
        writeln!(w, "Bucket {}: {}", bucket.name, size_formatter(bucket.size))?;
    }
    print_processes_digest(w, digest.processes, size_formatter)?;
    writeln!(w)?;
    Ok(())
}

/// Print to `w` a human-readable presentation of `output`.
pub fn write_human_readable_output<'a>(
    w: &mut Writer,
    output: ProfileMemoryOutput,
    exact_sizes: bool,
) -> Result<()> {
    let size_to_string_formatter = if exact_sizes {
        |size: u64| size.to_string() + " B"
    } else {
        |size: u64| size.file_size(BINARY).unwrap()
    };

    match output {
        ProfileMemoryOutput::CompleteDigest(digest) => {
            print_complete_digest(w, digest, size_to_string_formatter)
        }
        ProfileMemoryOutput::ProcessDigest(processes_digest) => {
            print_processes_digest(w, processes_digest.process_data, size_to_string_formatter)
        }
    }
}

#[cfg(test)]
mod tests {

    use super::*;
    use crate::plugin_output::ProcessesMemoryUsage;
    use crate::processed::RetainedMemory;
    use crate::ProfileMemoryOutput::ProcessDigest;
    use std::collections::HashMap;
    use std::collections::HashSet;

    fn data_for_test() -> crate::ProfileMemoryOutput {
        ProcessDigest(ProcessesMemoryUsage {
            capture_time: 123,
            process_data: vec![processed::Process {
                koid: 4,
                name: "P".to_string(),
                memory: RetainedMemory { private: 11, scaled: 22, total: 33 },
                name_to_memory: {
                    let mut result = HashMap::new();
                    result.insert(
                        "vmoC".to_string(),
                        processed::RetainedMemory { private: 4444, scaled: 55555, total: 666666 },
                    );
                    result.insert(
                        "vmoB".to_string(),
                        processed::RetainedMemory { private: 4444, scaled: 55555, total: 666666 },
                    );
                    result.insert(
                        "vmoA".to_string(),
                        processed::RetainedMemory {
                            private: 44444,
                            scaled: 555555,
                            total: 6666666,
                        },
                    );
                    result
                },
                vmos: HashSet::new(),
            }],
        })
    }

    #[test]
    fn write_human_readable_output_exact_sizes_test() {
        let mut writer = Writer::new_test(None);
        let _ = write_human_readable_output(&mut writer, data_for_test(), true);
        let actual_output = writer.test_output().unwrap();
        let expected_output = r#"Process name: P
Process koid: 4
Private:      11 B
PSS:          22 B (Proportional Set Size)
Total:        33 B (Private + Shared unscaled)
              Private       Scaled        Total
    vmoA      44444 B     555555 B    6666666 B     (shared)
    vmoB       4444 B      55555 B     666666 B     (shared)
    vmoC       4444 B      55555 B     666666 B     (shared)

"#;
        pretty_assertions::assert_eq!(actual_output, *expected_output);
    }

    #[test]
    fn write_human_readable_output_human_friendly_sizes_test() {
        let mut writer = Writer::new_test(None);
        let _ = write_human_readable_output(&mut writer, data_for_test(), false);
        let actual_output = writer.test_output().unwrap();
        let expected_output = r#"Process name: P
Process koid: 4
Private:      11 B
PSS:          22 B (Proportional Set Size)
Total:        33 B (Private + Shared unscaled)
              Private       Scaled        Total
    vmoA    43.40 KiB   542.53 KiB     6.36 MiB     (shared)
    vmoB     4.34 KiB    54.25 KiB   651.04 KiB     (shared)
    vmoC     4.34 KiB    54.25 KiB   651.04 KiB     (shared)

"#;
        pretty_assertions::assert_eq!(actual_output, *expected_output);
    }
}
