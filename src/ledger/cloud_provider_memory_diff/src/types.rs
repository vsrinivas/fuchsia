// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::error::*,
    fidl::encoding::Decodable,
    fidl_fuchsia_ledger_cloud as cloud,
    std::convert::{TryFrom, TryInto},
};

pub use cloud::Operation;

trait RequireExt {
    type Ok;
    /// Returns a ClientError if missing. `what` describes what should be there.
    fn require(self, what: &str) -> Result<Self::Ok, ClientError>;
}

impl<T> RequireExt for Option<T> {
    type Ok = T;
    fn require(self, what: &str) -> Result<T, ClientError> {
        self.ok_or_else(|| {
            client_error(Status::ArgumentError).with_explanation(format!("missing {}", what))
        })
    }
}

/// A wrapper for a position in the commit log.
#[derive(Clone, Copy, Debug, Eq, Ord, PartialEq, PartialOrd)]
pub struct Token(pub usize);

impl TryFrom<Option<Box<cloud::PositionToken>>> for Token {
    type Error = ClientError;
    fn try_from(val: Option<Box<cloud::PositionToken>>) -> Result<Self, Self::Error> {
        match val {
            None => Ok(Token(0)),
            Some(t) => match t.opaque_id.as_slice().try_into().map(usize::from_le_bytes) {
                Ok(v) => Ok(Token(v)),
                Err(e) => {
                    Err(e.client_error(Status::ArgumentError).with_explanation("invalid token"))
                }
            },
        }
    }
}

impl From<Token> for cloud::PositionToken {
    fn from(t: Token) -> Self {
        Self { opaque_id: Vec::from(&t.0.to_le_bytes() as &[u8]) }
    }
}

/// A wrapper for the id of a commit.
#[derive(PartialEq, Eq, Hash, Debug, Clone)]
pub struct CommitId(pub Vec<u8>);

impl From<Vec<u8>> for CommitId {
    fn from(x: Vec<u8>) -> Self {
        CommitId(x)
    }
}

impl From<CommitId> for Vec<u8> {
    fn from(x: CommitId) -> Self {
        x.0
    }
}

/// A wrapper for the id of an object.
#[derive(PartialEq, Eq, Hash, Debug, Clone)]
pub struct ObjectId(pub Vec<u8>);

impl From<Vec<u8>> for ObjectId {
    fn from(obj_id: Vec<u8>) -> ObjectId {
        ObjectId(obj_id)
    }
}

/// A wrapper for fingerprints.
#[derive(PartialEq, Eq, Hash, Debug, Clone)]
pub struct Fingerprint(pub Vec<u8>);

impl From<Vec<u8>> for Fingerprint {
    fn from(bytes: Vec<u8>) -> Fingerprint {
        Fingerprint(bytes)
    }
}

/// A wrapper for application and page ids.
#[derive(PartialEq, Eq, Hash, Debug, Clone)]
pub struct PageId(pub Vec<u8>, pub Vec<u8>);

impl PageId {
    pub fn from(app_id: Vec<u8>, page_id: Vec<u8>) -> PageId {
        PageId(app_id, page_id)
    }
}

/// A commit stored in the cloud.
#[derive(Clone, Debug, PartialEq)]
pub struct Commit {
    /// The id of this commit.
    pub id: CommitId,
    /// Opaque data associated to the commit.
    pub data: Vec<u8>,
}

impl TryFrom<cloud::Commit> for Commit {
    type Error = ClientError;
    fn try_from(c: cloud::Commit) -> Result<Self, Self::Error> {
        Ok(Commit { id: c.id.require("commit id")?.into(), data: c.data.require("commit data")? })
    }
}

impl From<&Commit> for cloud::Commit {
    fn from(commit: &Commit) -> cloud::Commit {
        cloud::Commit {
            id: Some(commit.id.0.clone()),
            data: Some(commit.data.clone()),
            ..cloud::Commit::new_empty()
        }
    }
}

/// Diffs exchanged with Ledger and stored in the cloud.
#[derive(Clone, Debug, PartialEq)]
pub struct Diff {
    /// Base commit for the diff.
    pub base_state: PageState,
    /// An ordered list of actions to go from `base_state` to the
    /// target version.
    pub changes: Vec<DiffEntry>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum PageState {
    EmptyPage,
    AtCommit(CommitId),
}

impl From<cloud::PageState> for PageState {
    fn from(base: cloud::PageState) -> Self {
        match base {
            cloud::PageState::EmptyPage(_) => PageState::EmptyPage,
            cloud::PageState::AtCommit(commit_id) => PageState::AtCommit(CommitId(commit_id)),
        }
    }
}

impl From<PageState> for cloud::PageState {
    fn from(base: PageState) -> Self {
        match base {
            PageState::EmptyPage => cloud::PageState::EmptyPage(cloud::EmptyPage {}),
            PageState::AtCommit(CommitId(commit_id)) => cloud::PageState::AtCommit(commit_id),
        }
    }
}

impl TryFrom<cloud::Diff> for Diff {
    type Error = ClientError;
    fn try_from(x: cloud::Diff) -> Result<Diff, Self::Error> {
        Ok(Diff {
            base_state: x.base_state.require("diff base state")?.into(),
            changes: x
                .changes
                .require("diff changes")?
                .into_iter()
                .map(DiffEntry::try_from)
                .collect::<Result<Vec<DiffEntry>, ClientError>>()?,
        })
    }
}

impl From<Diff> for cloud::Diff {
    fn from(d: Diff) -> Self {
        Self {
            base_state: Some(d.base_state.into()),
            changes: Some(d.changes.into_iter().map(DiffEntry::into).collect()),
        }
    }
}

#[derive(Clone, Debug, Eq, Hash, PartialEq)]
pub struct DiffEntry {
    pub entry_id: Vec<u8>,
    pub data: Vec<u8>,
    pub operation: Operation,
}

impl TryFrom<cloud::DiffEntry> for DiffEntry {
    type Error = ClientError;
    fn try_from(e: cloud::DiffEntry) -> Result<Self, Self::Error> {
        Ok(DiffEntry {
            entry_id: e.entry_id.require("entry id")?,
            data: e.data.require("entry data")?,
            operation: e.operation.require("entry operation")?,
        })
    }
}

impl From<DiffEntry> for cloud::DiffEntry {
    fn from(e: DiffEntry) -> Self {
        cloud::DiffEntry {
            entry_id: Some(e.entry_id),
            data: Some(e.data),
            operation: Some(e.operation),
            reference: None,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn require() {
        assert!(None::<()>.require("test").is_err());
        assert_eq!(Some(1).require("test").unwrap(), 1);
    }

    #[test]
    fn convert_token_to_fidl_and_back() {
        for token in [Token(0), Token(12), Token(usize::max_value())].iter() {
            let fidl_token = cloud::PositionToken::from(*token);
            let roundtrip_token = Token::try_from(Some(Box::new(fidl_token))).unwrap();
            assert_eq!(roundtrip_token, *token);
        }
    }

    #[test]
    fn convert_invalid_token_fails() {
        // This token does not fit into a u64.
        let bytes = vec![1, 1, 1, 1, 1, 1, 1, 1, 1];
        let fidl_token = Some(Box::new(cloud::PositionToken { opaque_id: bytes }));
        assert!(Token::try_from(fidl_token).is_err());
    }

    #[test]
    fn convert_commit_to_fidl_and_back() {
        let commit = Commit { id: CommitId(vec![1, 2, 3]), data: vec![4, 5, 6] };
        let fidl_commit1 = cloud::Commit::from(&commit);
        assert_eq!(Commit::try_from(fidl_commit1).unwrap(), commit);
        // The diff is ignored by the conversion from FIDL to the internal representation.
        let fidl_commit2 = cloud::Commit {
            diff: Some(cloud::Diff { base_state: None, changes: None }),
            ..cloud::Commit::from(&commit)
        };
        assert_eq!(Commit::try_from(fidl_commit2).unwrap(), commit);
    }

    #[test]
    fn convert_diff_to_fidl_and_back() {
        let diff = Diff {
            base_state: PageState::AtCommit(CommitId(vec![0])),
            changes: vec![
                DiffEntry { entry_id: vec![1], data: vec![2], operation: Operation::Insertion },
                DiffEntry {
                    entry_id: vec![0, 0],
                    data: vec![0, 0, 0],
                    operation: Operation::Deletion,
                },
            ],
        };
        let fidl_diff = cloud::Diff::from(diff.clone());
        assert_eq!(Diff::try_from(fidl_diff).unwrap(), diff);
    }
}
