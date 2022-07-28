// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {crate::digest, digest::processed, serde::Serialize};

// The plugin can output one of these based on the options.
#[derive(Serialize)]
pub enum ProfileMemoryOutput {
    CompleteDigest(processed::Digest),
    ProcessDigest(Vec<processed::Process>),
}

/// Returns a ProfileMemoryOutput that only contains information related to the process identified by `koid`.
pub fn filter_digest_by_process_koids(
    digest: processed::Digest,
    koids: Vec<u64>,
) -> ProfileMemoryOutput {
    let mut vec = Vec::new();
    for process in digest.processes {
        if koids.contains(&process.koid) {
            vec.push(process);
        }
    }
    return ProfileMemoryOutput::ProcessDigest(vec);
}
