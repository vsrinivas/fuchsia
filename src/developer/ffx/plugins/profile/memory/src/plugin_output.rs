// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {crate::digest, digest::processed, serde::Serialize};

/// Contains the memory usage of processes, and the time at which the
/// data was captured.
#[derive(Serialize)]
pub struct ProcessesMemoryUsage {
    /// The list of process data.
    pub process_data: Vec<processed::Process>,
    /// The time at which the data was captured.
    pub capture_time: u64,
}

/// The plugin can output one of these based on the options:
/// * A complete digest of the memory usage.
/// * A digest of the memory usage of a subset of the processes running on the targetted device.
#[derive(Serialize)]
pub enum ProfileMemoryOutput {
    CompleteDigest(processed::Digest),
    ProcessDigest(ProcessesMemoryUsage),
}

/// Returns a ProfileMemoryOutput that only contains information related to the process identified by `koid`.
pub fn filter_digest_by_process_koids(
    digest: processed::Digest,
    koids: &Vec<u64>,
) -> ProfileMemoryOutput {
    let mut vec = Vec::new();
    for process in digest.processes {
        if koids.contains(&process.koid) {
            vec.push(process);
        }
    }
    return ProfileMemoryOutput::ProcessDigest(ProcessesMemoryUsage {
        process_data: vec,
        capture_time: digest.time,
    });
}
