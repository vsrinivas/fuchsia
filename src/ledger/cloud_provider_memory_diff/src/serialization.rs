// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::error::*;
use crate::types::*;
use fidl::encoding::Decodable;
use fidl_fuchsia_ledger_cloud as cloud;
use fuchsia_zircon::Vmo;
use std::convert::TryFrom;

/// Reads bytes from a fidl buffer into `result`.
pub fn read_buffer(
    buffer: &fidl_fuchsia_mem::Buffer,
    output: &mut Vec<u8>,
) -> Result<(), ClientError> {
    let size = buffer.size;
    let vmo = &buffer.vmo;
    output.resize(size as usize, 0);
    vmo.read(output.as_mut_slice(), 0).map_err(|err| {
        err.client_error(Status::ArgumentError)
            .with_explanation("unable to read from fuchsia.mem.Buffer")
    })
}

/// Writes bytes to a fidl buffer.
pub fn write_buffer(data: &[u8]) -> fidl_fuchsia_mem::Buffer {
    let size = data.len() as u64;
    let vmo = Vmo::create(size).expect("write_buffer: unable to allocate VMO");
    vmo.write(data, 0).expect("write_buffer: unable to write to VMO");
    fidl_fuchsia_mem::Buffer { vmo, size }
}

/// Converts from and to a cloud::CommitPack.
impl Commit {
    pub fn serialize_pack(commits: Vec<&Commit>) -> cloud::CommitPack {
        let mut serialized =
            cloud::Commits { commits: commits.into_iter().map(cloud::Commit::from).collect() };
        fidl::encoding::with_tls_encoded(&mut serialized, |data, _handles| {
            Ok::<_, fidl::Error>(cloud::CommitPack { buffer: write_buffer(data.as_slice()) })
        })
        .expect("Failed to write FIDL-encoded commit data to buffer")
    }

    pub fn serialize_pack_with_diffs(commits: Vec<(Commit, Option<Diff>)>) -> cloud::CommitPack {
        let mut serialized = cloud::Commits {
            commits: commits
                .into_iter()
                .map(|(commit, diff)| cloud::Commit {
                    diff: diff.map(cloud::Diff::from),
                    ..cloud::Commit::from(&commit)
                })
                .collect(),
        };
        fidl::encoding::with_tls_encoded(&mut serialized, |data, _handles| {
            Ok::<_, fidl::Error>(cloud::CommitPack { buffer: write_buffer(data.as_slice()) })
        })
        .expect("Failed to write FIDL-encoded commit data to buffer")
    }

    pub fn deserialize_pack(
        pack: &cloud::CommitPack,
    ) -> Result<Vec<(Commit, Option<Diff>)>, ClientError> {
        let buf = &pack.buffer;
        fidl::encoding::with_tls_coding_bufs(|data, _handles| {
            read_buffer(buf, data)?;
            let mut serialized_commits = cloud::Commits { commits: vec![] };

            // This is OK because ledger interfaces do not use static unions.
            let context = fidl::encoding::Context { unions_use_xunion_format: true };
            fidl::encoding::Decoder::decode_with_context(
                &context,
                &data,
                &mut [],
                &mut serialized_commits,
            )
            .map_err(|err| {
                err.client_error(Status::ArgumentError)
                    .with_explanation("couldn't decode commits from FIDL data")
            })?;
            serialized_commits
                .commits
                .into_iter()
                .map(|mut commit| {
                    let diff = commit.diff.take();
                    Ok((Self::try_from(commit)?, diff.map(Diff::try_from).transpose()?))
                })
                .collect()
        })
    }
}

impl Diff {
    pub fn serialize_pack(diff: Diff) -> cloud::DiffPack {
        let mut serialized: cloud::Diff = diff.into();
        fidl::encoding::with_tls_encoded(&mut serialized, |data, _handles| {
            Ok::<_, fidl::Error>(cloud::DiffPack { buffer: write_buffer(data.as_slice()) })
        })
        .expect("Failed to write FIDL-encoded diff data to buffer")
    }

    pub fn deserialize_pack(pack: &cloud::DiffPack) -> Result<Diff, ClientError> {
        let buf = &pack.buffer;
        fidl::encoding::with_tls_coding_bufs(|data, _handles| {
            read_buffer(buf, data)?;
            let mut serialized_diff = cloud::Diff::new_empty();

            // This is OK because ledger interfaces do not use static unions.
            let context = fidl::encoding::Context { unions_use_xunion_format: true };
            fidl::encoding::Decoder::decode_with_context(
                &context,
                &data,
                &mut [],
                &mut serialized_diff,
            )
            .map_err(|err| {
                err.client_error(Status::ArgumentError)
                    .with_explanation("couldn't decode commits from FIDL data")
            })?;
            Diff::try_from(serialized_diff)
        })
    }
}
