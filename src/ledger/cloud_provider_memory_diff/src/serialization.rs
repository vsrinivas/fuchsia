// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::Error;
use fidl_fuchsia_ledger_cloud::{SerializedCommit, SerializedCommits, Status};
use fuchsia_zircon::Vmo;
use std::convert::{TryFrom, TryInto};

use crate::state::{CloudError, Commit, CommitId, Fingerprint, ObjectId, PageId, Token};

impl From<Vec<u8>> for Fingerprint {
    fn from(bytes: Vec<u8>) -> Fingerprint {
        Fingerprint(bytes)
    }
}

impl From<Vec<u8>> for ObjectId {
    fn from(obj_id: Vec<u8>) -> ObjectId {
        ObjectId(obj_id)
    }
}

impl PageId {
    pub fn from(app_id: Vec<u8>, page_id: Vec<u8>) -> PageId {
        PageId(app_id, page_id)
    }
}

/// Reads bytes from a fidl buffer into `result`.
pub fn read_buffer(buffer: fidl_fuchsia_mem::Buffer, output: &mut Vec<u8>) -> Result<(), Error> {
    let size = buffer.size;
    let vmo = buffer.vmo;
    output.resize(size as usize, 0);
    vmo.read(output.as_mut_slice(), 0)?;
    Ok(())
}

/// Writes bytes to a fidl buffer.
pub fn write_buffer(data: &[u8]) -> Result<fidl_fuchsia_mem::Buffer, Error> {
    let size = data.len() as u64;
    let vmo = Vmo::create(size)?;
    vmo.write(data, 0)?;
    Ok(fidl_fuchsia_mem::Buffer { vmo, size })
}

impl TryFrom<Option<Box<fidl_fuchsia_ledger_cloud::Token>>> for Token {
    type Error = ();
    fn try_from(val: Option<Box<fidl_fuchsia_ledger_cloud::Token>>) -> Result<Self, Self::Error> {
        match val {
            None => Ok(Token(0)),
            Some(t) => match t.opaque_id.as_slice().try_into().map(usize::from_le_bytes) {
                Ok(v) => Ok(Token(v)),
                Err(_) => Err(()),
            },
        }
    }
}

impl Into<fidl_fuchsia_ledger_cloud::Token> for Token {
    fn into(self: Token) -> fidl_fuchsia_ledger_cloud::Token {
        fidl_fuchsia_ledger_cloud::Token { opaque_id: Vec::from(&self.0.to_le_bytes() as &[u8]) }
    }
}

impl From<CloudError> for Status {
    fn from(e: CloudError) -> Self {
        match e {
            CloudError::ObjectNotFound(_) | CloudError::FingerprintNotFound(_) => Status::NotFound,
            CloudError::InvalidToken => Status::ArgumentError,
            CloudError::ParseError => Status::ParseError,
        }
    }
}

impl CloudError {
    /// Converts a Result<(), CloudError> to a Ledger status.
    pub fn status(res: Result<(), Self>) -> Status {
        match res {
            Ok(()) => Status::Ok,
            Err(s) => Status::from(s),
        }
    }
}

impl From<SerializedCommit> for Commit {
    fn from(commit: SerializedCommit) -> Commit {
        Commit { id: CommitId(commit.id), data: commit.data }
    }
}

impl From<&Commit> for SerializedCommit {
    fn from(commit: &Commit) -> SerializedCommit {
        SerializedCommit { id: commit.id.0.clone(), data: commit.data.clone() }
    }
}

/// Converts from and to SerializedCommits.
impl Commit {
    pub fn serialize_vec(commits: Vec<&Commit>) -> fidl_fuchsia_mem::Buffer {
        let mut serialized = SerializedCommits {
            commits: commits.into_iter().map(SerializedCommit::from).collect(),
        };
        fidl::encoding::with_tls_encoded(&mut serialized, |data, _handles| {
            write_buffer(data.as_slice())
        })
        .expect("Failed to write FIDL-encoded commit data to buffer")
    }

    pub fn deserialize_vec(buf: fidl_fuchsia_mem::Buffer) -> Result<Vec<Commit>, CloudError> {
        fidl::encoding::with_tls_coding_bufs(|data, _handles| {
            read_buffer(buf, data).map_err(|_| CloudError::ParseError)?;
            let mut serialized_commits = SerializedCommits { commits: vec![] };
            fidl::encoding::Decoder::decode_into(&data, &mut [], &mut serialized_commits)
                .map_err(|_| CloudError::ParseError)?;
            Ok(serialized_commits.commits.into_iter().map(Self::from).collect())
        })
    }
}
