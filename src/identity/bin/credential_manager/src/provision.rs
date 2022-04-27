// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        hash_tree::{
            HashTree, HashTreeError, HashTreeStorage, BITS_PER_LEVEL, CHILDREN_PER_NODE,
            TREE_HEIGHT,
        },
        lookup_table::LookupTable,
        pinweaver::PinWeaverProtocol,
    },
    fidl_fuchsia_identity_credential::CredentialError,
    fidl_fuchsia_tpm_cr50 as fcr50,
    log::{error, info},
};

/// Represents the state of the HashTree persisted on disk compared with the HashTree
/// state returned by the CR50.
enum HashTreeSyncState {
    /// The HashTree on disk and on the CR50 are in sync and there is nothing to do.
    Current(HashTree),
    /// The HashTree on disk is out of sync by one operation with the CR50 and we
    /// need to enter the recovery flow.
    OutOfSync(HashTree, fcr50::LogEntry),
    /// The HashTree on disk is out of sync by more than one operation with the CR50
    /// and we cannot proceed. This should only really occur in two cases:
    ///
    /// 1. Disk corruption: State wasn't flushed to disk for multiple operations etc.
    /// 2. NvRAM corruption: The CR50 state was somehow corrupted.
    ///
    /// In both cases there is no way to proceed from the credential manager point of
    /// view and we should trigger a provisioning error. This is extremely unlikely.
    Unrecoverable,
}

/// Detects if there is an existing |hash_tree| in which case it will
/// load it from disk. If no |hash_tree| exists the |CredentialManager| will
/// reset the CR50 via |pinweaver| and create and store a new |hash_tree|.
pub async fn provision<HS: HashTreeStorage, PW: PinWeaverProtocol, LT: LookupTable>(
    hash_tree_storage: &HS,
    lookup_table: &mut LT,
    pinweaver: &PW,
) -> Result<HashTree, CredentialError> {
    match hash_tree_storage.load() {
        Ok(hash_tree) => synchronize_state(hash_tree, pinweaver).await,
        Err(HashTreeError::DataStoreNotFound) => {
            info!("Could not read hash tree file, resetting");
            reset_state(hash_tree_storage, lookup_table, pinweaver).await
        }
        Err(err) => {
            // If the existing hash tree fails to deserialize return a fatal error rather than
            // resetting so we don't destroy data that would be helpful to isolate the problem.
            // TODO(benwright,jsankey): Reconsider this decision once the system is more mature.
            error!("Error loading hash tree: {:?}", err);
            Err(CredentialError::CorruptedMetadata)
        }
    }
}

/// Provisions a new |hash_tree|. This clears the lookup table and then
/// calls |PinWeaverProtocol::reset_tree| to reset the CR50. It then
/// constructs a new |hash_tree| and persists it to disk.
async fn reset_state<HS: HashTreeStorage, PW: PinWeaverProtocol, LT: LookupTable>(
    hash_tree_storage: &HS,
    lookup_table: &mut LT,
    pinweaver: &PW,
) -> Result<HashTree, CredentialError> {
    let hash_tree =
        HashTree::new(TREE_HEIGHT, CHILDREN_PER_NODE).expect("Unable to create hash tree");
    lookup_table.reset().await.map_err(|_| CredentialError::InternalError)?;
    pinweaver.reset_tree(BITS_PER_LEVEL, TREE_HEIGHT).await?;
    hash_tree_storage.store(&hash_tree).map_err(|_| CredentialError::InternalError)?;
    Ok(hash_tree)
}

/// Returns the current sync state between the on-disk Pinweaver hash tree and
/// the on chip Pinweaver hash tree.
async fn get_sync_state<PW: PinWeaverProtocol>(
    hash_tree: HashTree,
    pinweaver: &PW,
) -> Result<HashTreeSyncState, CredentialError> {
    let stored_root_hash = hash_tree.get_root_hash().map_err(|_| CredentialError::InternalError)?;
    let pinweaver_log = pinweaver.get_log(&stored_root_hash).await?;
    Ok(match &pinweaver_log[..] {
        // We are caught up to pinweaver so we only get one entry.
        [current_log] => {
            if Some(*stored_root_hash) == current_log.root_hash {
                HashTreeSyncState::Current(hash_tree)
            } else {
                HashTreeSyncState::Unrecoverable
            }
        }
        // We are one step behind pinweaver.
        [prev_log, current_log] => {
            if Some(*stored_root_hash) == prev_log.root_hash {
                HashTreeSyncState::OutOfSync(hash_tree, current_log.clone())
            } else {
                HashTreeSyncState::Unrecoverable
            }
        }
        // This state should never occur and is not recoverable.
        _ => HashTreeSyncState::Unrecoverable,
    })
}

/// Detects if there is a difference between the stored root hash in the |hash_tree|
/// and the root_hash returned by |pinweaver|. If the root hashes do not match then
/// a recovery flow is entered to attempt to resync the chip with the stored state.
async fn synchronize_state<PW: PinWeaverProtocol>(
    hash_tree: HashTree,
    pinweaver: &PW,
) -> Result<HashTree, CredentialError> {
    let sync_state = get_sync_state(hash_tree, pinweaver).await?;
    match sync_state {
        HashTreeSyncState::Current(current_tree) => Ok(current_tree),
        HashTreeSyncState::OutOfSync(_out_of_sync_tree, _log_to_replay) => {
            // TODO(benwright): Support the recovery scenario.
            Err(CredentialError::UnsupportedOperation)
        }
        HashTreeSyncState::Unrecoverable => Err(CredentialError::CorruptedMetadata),
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::{
        hash_tree::MockHashTreeStorage, lookup_table::MockLookupTable,
        pinweaver::MockPinWeaverProtocol,
    };
    use assert_matches::assert_matches;
    use fidl_fuchsia_tpm_cr50 as fcr50;

    #[fuchsia::test]
    async fn test_provision_with_existing_tree() {
        let mut pinweaver = MockPinWeaverProtocol::new();
        let mut lookup_table = MockLookupTable::new();
        let mut storage = MockHashTreeStorage::new();
        pinweaver.expect_get_log().times(1).returning(|&hash| {
            Ok(vec![fcr50::LogEntry { root_hash: Some(hash.clone()), ..fcr50::LogEntry::EMPTY }])
        });
        storage.expect_load().times(1).returning(|| {
            Ok(HashTree::new(TREE_HEIGHT, CHILDREN_PER_NODE).expect("unable to create hash tree"))
        });
        provision(&storage, &mut lookup_table, &pinweaver).await.expect("Unable to load storage");
    }

    #[fuchsia::test]
    async fn test_provision_with_no_tree_found() {
        let mut pinweaver = MockPinWeaverProtocol::new();
        let mut lookup_table = MockLookupTable::new();
        let mut storage = MockHashTreeStorage::new();
        storage.expect_load().times(1).returning(|| Err(HashTreeError::DataStoreNotFound));
        storage.expect_store().times(1).returning(|_| Ok(()));
        pinweaver.expect_reset_tree().times(1).returning(|_, _| Ok([0; 32]));
        lookup_table.expect_reset().times(1).returning(|| Ok(()));
        provision(&storage, &mut lookup_table, &pinweaver)
            .await
            .expect("Unable to create new tree");
    }

    #[fuchsia::test]
    async fn test_provision_with_unrecoverable_tree_fails() {
        let mut pinweaver = MockPinWeaverProtocol::new();
        let mut lookup_table = MockLookupTable::new();
        let mut storage = MockHashTreeStorage::new();
        pinweaver.expect_get_log().times(1).returning(|&_| {
            Ok(vec![
                fcr50::LogEntry { root_hash: Some([1; 32].clone()), ..fcr50::LogEntry::EMPTY },
                fcr50::LogEntry { root_hash: Some([2; 32].clone()), ..fcr50::LogEntry::EMPTY },
            ])
        });
        storage.expect_load().times(1).returning(|| {
            Ok(HashTree::new(TREE_HEIGHT, CHILDREN_PER_NODE).expect("unable to create hash tree"))
        });
        let result = provision(&storage, &mut lookup_table, &pinweaver).await;
        assert_matches!(result, Err(CredentialError::CorruptedMetadata));
    }

    #[fuchsia::test]
    async fn test_provision_with_no_log_entries_fails() {
        let mut pinweaver = MockPinWeaverProtocol::new();
        let mut lookup_table = MockLookupTable::new();
        let mut storage = MockHashTreeStorage::new();
        pinweaver.expect_get_log().times(1).returning(|&_| Ok(vec![]));
        storage.expect_load().times(1).returning(|| {
            Ok(HashTree::new(TREE_HEIGHT, CHILDREN_PER_NODE).expect("unable to create hash tree"))
        });
        let result = provision(&storage, &mut lookup_table, &pinweaver).await;
        assert_matches!(result, Err(CredentialError::CorruptedMetadata));
    }

    #[fuchsia::test]
    async fn test_provision_with_one_log_entry_no_hash_map_fails() {
        let mut pinweaver = MockPinWeaverProtocol::new();
        let mut lookup_table = MockLookupTable::new();
        let mut storage = MockHashTreeStorage::new();
        pinweaver.expect_get_log().times(1).returning(|&_| {
            Ok(vec![fcr50::LogEntry { root_hash: Some([1; 32].clone()), ..fcr50::LogEntry::EMPTY }])
        });
        storage.expect_load().times(1).returning(|| {
            Ok(HashTree::new(TREE_HEIGHT, CHILDREN_PER_NODE).expect("unable to create hash tree"))
        });
        let result = provision(&storage, &mut lookup_table, &pinweaver).await;
        assert_matches!(result, Err(CredentialError::CorruptedMetadata));
    }
}
