// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        hash_tree::{
            HashTree, HashTreeError, HashTreeStorage, BITS_PER_LEVEL, CHILDREN_PER_NODE,
            LABEL_LENGTH, TREE_HEIGHT,
        },
        label_generator::Label,
        lookup_table::LookupTable,
        pinweaver::PinWeaverProtocol,
    },
    fidl_fuchsia_identity_credential::CredentialError,
    fidl_fuchsia_tpm_cr50 as fcr50,
    tracing::{error, info, warn},
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
    ///
    /// 3. Stale CR50 State: The CR50 state was from a prior installation.
    /// In this case we can attempt to reset the CR50 state if the HashTree on
    /// disk is empty.
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
        Ok(hash_tree) => {
            synchronize_state(hash_tree, hash_tree_storage, lookup_table, pinweaver).await
        }
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
    hash_tree_storage.store(&hash_tree)?;
    Ok(hash_tree)
}

/// Returns the current sync state between the on-disk Pinweaver hash tree and
/// the on chip Pinweaver hash tree.
async fn get_sync_state<PW: PinWeaverProtocol>(
    hash_tree: HashTree,
    pinweaver: &PW,
) -> Result<HashTreeSyncState, CredentialError> {
    let stored_root_hash = hash_tree.get_root_hash()?;
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
async fn synchronize_state<HS: HashTreeStorage, LT: LookupTable, PW: PinWeaverProtocol>(
    hash_tree: HashTree,
    hash_tree_storage: &HS,
    lookup_table: &mut LT,
    pinweaver: &PW,
) -> Result<HashTree, CredentialError> {
    let hash_tree_populated_size = hash_tree.populated_size();
    let sync_state = get_sync_state(hash_tree, pinweaver).await?;
    match sync_state {
        HashTreeSyncState::Current(current_tree) => Ok(current_tree),
        HashTreeSyncState::OutOfSync(out_of_sync_tree, log_to_replay) => {
            let updated_hash_tree =
                replay_state(out_of_sync_tree, lookup_table, pinweaver, log_to_replay).await?;
            hash_tree_storage.store(&updated_hash_tree)?;
            Ok(updated_hash_tree)
        }
        HashTreeSyncState::Unrecoverable => {
            // In the case where the hash tree and the Cr50 state are inconsistent but
            // the hash tree believes there should be no credentials, reset the Cr50 state
            // to regain consistency. This may occur if credentials were present in Cr50
            // before Fuchsia was installed.
            if hash_tree_populated_size == 0 {
                warn!("State synchronization failed. But the hash tree was empty. Resetting state");
                // Reset the local & on-chip state back to an empty state.
                reset_state(hash_tree_storage, lookup_table, pinweaver).await
            // In this case we have a populated hash tree that has fallen more than two steps
            // out of sync with the CR50 chip.
            } else {
                error!(
                    "State synchronization failed. Hash tree on disk contained {} items",
                    hash_tree_populated_size
                );
                Err(CredentialError::CorruptedMetadata)
            }
        }
    }
}

/// Replay state is responsible for moving the disk persisted |hash_tree| one operation
/// forward in time from the information provided in the |log_to_replay|. A given CR50
/// LogEntry can define one of four missed operations which the replay_state function
/// is responsible for reproducing locally so that the tree is in sync.
async fn replay_state<LT: LookupTable, PW: PinWeaverProtocol>(
    mut hash_tree: HashTree,
    lookup_table: &mut LT,
    pinweaver: &PW,
    log_to_replay: fcr50::LogEntry,
) -> Result<HashTree, CredentialError> {
    let message_type = log_to_replay.message_type.ok_or(CredentialError::InternalError)?;
    let root_hash = log_to_replay.root_hash.ok_or(CredentialError::InternalError)?;
    // TODO(benwright) Add inspect reporting.
    match &message_type {
        fcr50::MessageType::InsertLeaf => {
            let label = Label::leaf_label(
                log_to_replay.label.ok_or(CredentialError::InternalError)?,
                LABEL_LENGTH,
            );
            info!(?label, "Replaying InsertLeaf");
            let leaf_hmac = log_to_replay
                .entry_data
                .ok_or(CredentialError::InternalError)?
                .leaf_hmac
                .ok_or(CredentialError::InternalError)?;
            hash_tree.update_leaf_hash(&label, leaf_hmac)?;
        }
        fcr50::MessageType::RemoveLeaf => {
            let label = Label::leaf_label(
                log_to_replay.label.ok_or(CredentialError::InternalError)?,
                LABEL_LENGTH,
            );
            info!(?label, "Replaying RemoveLeaf");
            hash_tree.delete_leaf(&label)?;
        }
        fcr50::MessageType::ResetTree => {
            info!("Replaying ResetTree");
            hash_tree.reset()?;
        }
        fcr50::MessageType::TryAuth => {
            let label = Label::leaf_label(
                log_to_replay.label.ok_or(CredentialError::InternalError)?,
                LABEL_LENGTH,
            );
            info!(?label, "Replaying TryAuth");
            let h_aux = hash_tree.get_auxiliary_hashes_flattened(&label)?;
            let metadata = lookup_table.read(&label).await?.bytes;
            let response = pinweaver.log_replay(root_hash, h_aux, metadata).await?;
            hash_tree.update_leaf_hash(
                &label,
                response.leaf_hash.ok_or(CredentialError::InternalError)?,
            )?;
            lookup_table
                .write(&label, response.cred_metadata.ok_or(CredentialError::InternalError)?)
                .await?;
        }
    };
    let root_hash = log_to_replay.root_hash.ok_or(CredentialError::InternalError)?;
    let local_root_hash = hash_tree.get_root_hash()?;
    if *local_root_hash == root_hash {
        Ok(hash_tree)
    } else {
        error!(
            "Failed to resync hash tree local tree: {:?} does not match cr50: {:?}",
            root_hash, local_root_hash
        );
        Err(CredentialError::CorruptedMetadata)
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::{
        hash_tree::MockHashTreeStorage, lookup_table::MockLookupTable, lookup_table::ReadResult,
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
            let mut hash_tree =
                HashTree::new(TREE_HEIGHT, CHILDREN_PER_NODE).expect("unable to create hash tree");
            let label = Label::leaf_label(9, LABEL_LENGTH);
            let mac = [9; 32];
            hash_tree.update_leaf_hash(&label, mac).expect("Failed to update hash tree");
            Ok(hash_tree)
        });
        let result = provision(&storage, &mut lookup_table, &pinweaver).await;
        assert_matches!(result, Err(CredentialError::CorruptedMetadata));
    }

    #[fuchsia::test]
    async fn test_provision_with_empty_hash_tree_resets() {
        let mut pinweaver = MockPinWeaverProtocol::new();
        let mut lookup_table = MockLookupTable::new();
        let mut storage = MockHashTreeStorage::new();
        pinweaver.expect_get_log().times(1).returning(|&_| Ok(vec![]));
        lookup_table.expect_reset().times(1).returning(|| Ok(()));
        storage.expect_load().times(1).returning(|| {
            Ok(HashTree::new(TREE_HEIGHT, CHILDREN_PER_NODE).expect("unable to create hash tree"))
        });
        storage.expect_store().times(1).returning(|_| Ok(()));
        pinweaver.expect_reset_tree().times(1).returning(|_, _| Ok([0; 32]));
        let result = provision(&storage, &mut lookup_table, &pinweaver).await;
        assert_matches!(result, Ok(_));
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
            let mut hash_tree =
                HashTree::new(TREE_HEIGHT, CHILDREN_PER_NODE).expect("unable to create hash tree");
            let label = Label::leaf_label(9, LABEL_LENGTH);
            let mac = [9; 32];
            hash_tree.update_leaf_hash(&label, mac).expect("Failed to update hash tree");
            Ok(hash_tree)
        });
        let result = provision(&storage, &mut lookup_table, &pinweaver).await;
        assert_matches!(result, Err(CredentialError::CorruptedMetadata));
    }

    #[fuchsia::test]
    async fn test_replay_reset_tree() {
        let mut pinweaver = MockPinWeaverProtocol::new();
        let mut lookup_table = MockLookupTable::new();
        let mut storage = MockHashTreeStorage::new();
        let hash_tree =
            HashTree::new(TREE_HEIGHT, CHILDREN_PER_NODE).expect("unable to create hash tree");
        let root_hash = hash_tree.get_root_hash().unwrap().clone();
        pinweaver.expect_get_log().times(1).returning(move |&_| {
            Ok(vec![
                fcr50::LogEntry { root_hash: Some(root_hash.clone()), ..fcr50::LogEntry::EMPTY },
                fcr50::LogEntry {
                    root_hash: Some(root_hash.clone()),
                    message_type: Some(fcr50::MessageType::ResetTree),
                    ..fcr50::LogEntry::EMPTY
                },
            ])
        });
        storage.expect_load().times(1).return_once(move || Ok(hash_tree));
        storage.expect_store().times(1).return_once(|_| Ok(()));
        let result = provision(&storage, &mut lookup_table, &pinweaver).await;
        assert_matches!(result, Ok(_));
    }

    #[fuchsia::test]
    async fn test_replay_insert_leaf() {
        let mut pinweaver = MockPinWeaverProtocol::new();
        let mut lookup_table = MockLookupTable::new();
        let mut storage = MockHashTreeStorage::new();
        let hash_tree =
            HashTree::new(TREE_HEIGHT, CHILDREN_PER_NODE).expect("unable to create hash tree");
        let root_hash = hash_tree.get_root_hash().unwrap().clone();

        // Replay the expected disk operations that were missed in this recovery flow.
        let mut expected_hash_tree =
            HashTree::new(TREE_HEIGHT, CHILDREN_PER_NODE).expect("unable to create hash tree");
        let expected_label = Label::leaf_label(2, LABEL_LENGTH);
        let expected_leaf_hmac = [1; 32];
        expected_hash_tree
            .update_leaf_hash(&expected_label, expected_leaf_hmac)
            .expect("failed to insert leaf node");
        let expected_root_hash = expected_hash_tree.get_root_hash().unwrap().clone();

        pinweaver.expect_get_log().times(1).returning(move |&_| {
            Ok(vec![
                fcr50::LogEntry { root_hash: Some(root_hash), ..fcr50::LogEntry::EMPTY },
                fcr50::LogEntry {
                    root_hash: Some(expected_root_hash),
                    message_type: Some(fcr50::MessageType::InsertLeaf),
                    label: Some(expected_label.value()),
                    entry_data: Some(fcr50::EntryData {
                        leaf_hmac: Some(expected_leaf_hmac),
                        ..fcr50::EntryData::EMPTY
                    }),
                    ..fcr50::LogEntry::EMPTY
                },
            ])
        });
        storage.expect_load().times(1).return_once(move || Ok(hash_tree));
        storage.expect_store().times(1).return_once(|_| Ok(()));
        let result = provision(&storage, &mut lookup_table, &pinweaver).await;
        assert_matches!(result, Ok(_));
    }

    #[fuchsia::test]
    async fn test_replay_remove_leaf() {
        let mut pinweaver = MockPinWeaverProtocol::new();
        let mut lookup_table = MockLookupTable::new();
        let mut storage = MockHashTreeStorage::new();

        let expected_label = Label::leaf_label(2, LABEL_LENGTH);
        let expected_leaf_hmac = [1; 32];

        // Our hash tree state should be synced with inserting thee new leaf hash.
        let mut hash_tree =
            HashTree::new(TREE_HEIGHT, CHILDREN_PER_NODE).expect("unable to create hash tree");
        hash_tree
            .update_leaf_hash(&expected_label, expected_leaf_hmac)
            .expect("failed to insert leaf node");
        let root_hash = hash_tree.get_root_hash().unwrap().clone();

        // The expected hash tree state should have deleted the leaf.
        let mut expected_hash_tree =
            HashTree::new(TREE_HEIGHT, CHILDREN_PER_NODE).expect("unable to create hash tree");
        expected_hash_tree
            .update_leaf_hash(&expected_label, expected_leaf_hmac)
            .expect("failed to insert leaf node");
        expected_hash_tree.delete_leaf(&expected_label).expect("failed to delete leaf node");
        let expected_root_hash = expected_hash_tree.get_root_hash().unwrap().clone();

        pinweaver.expect_get_log().times(1).returning(move |&_| {
            Ok(vec![
                fcr50::LogEntry { root_hash: Some(root_hash), ..fcr50::LogEntry::EMPTY },
                fcr50::LogEntry {
                    root_hash: Some(expected_root_hash),
                    message_type: Some(fcr50::MessageType::RemoveLeaf),
                    label: Some(expected_label.value()),
                    ..fcr50::LogEntry::EMPTY
                },
            ])
        });
        storage.expect_load().times(1).return_once(move || Ok(hash_tree));
        storage.expect_store().times(1).return_once(|_| Ok(()));
        let result = provision(&storage, &mut lookup_table, &pinweaver).await;
        assert_matches!(result, Ok(_));
    }

    #[fuchsia::test]
    async fn test_replay_try_auth() {
        let mut pinweaver = MockPinWeaverProtocol::new();
        let mut lookup_table = MockLookupTable::new();
        let mut storage = MockHashTreeStorage::new();

        let expected_label = Label::leaf_label(2, LABEL_LENGTH);
        let expected_leaf_hmac = [1; 32];
        let expected_leaf_hmac_after_auth = [2; 32];
        let fake_metadata: Vec<u8> = vec![3; 128];
        let fake_metadata_clone = fake_metadata.clone();

        // Our hash tree state should contain one credential.
        let mut hash_tree =
            HashTree::new(TREE_HEIGHT, CHILDREN_PER_NODE).expect("unable to create hash tree");
        hash_tree
            .update_leaf_hash(&expected_label, expected_leaf_hmac)
            .expect("failed to insert leaf node");
        let root_hash = hash_tree.get_root_hash().unwrap().clone();

        // Updating the expected hash tree twice simulates updating the credential after a TryAuth
        // operation.
        let mut expected_hash_tree =
            HashTree::new(TREE_HEIGHT, CHILDREN_PER_NODE).expect("unable to create hash tree");
        expected_hash_tree
            .update_leaf_hash(&expected_label, expected_leaf_hmac)
            .expect("failed to insert leaf node");
        expected_hash_tree
            .update_leaf_hash(&expected_label, expected_leaf_hmac_after_auth)
            .expect("failed to insert leaf node");
        let expected_root_hash = expected_hash_tree.get_root_hash().unwrap().clone();

        lookup_table
            .expect_read()
            .times(1)
            .return_once(|_| Ok(ReadResult { version: 1, bytes: fake_metadata }));
        lookup_table.expect_write().times(1).return_once(|_, _| Ok(()));
        pinweaver.expect_log_replay().times(1).return_once(move |root_hash, _, metadata| {
            assert_eq!(root_hash, expected_root_hash);
            assert_eq!(metadata, fake_metadata_clone);
            Ok(fcr50::LogReplayResponse {
                cred_metadata: Some(vec![1; 32]),
                leaf_hash: Some(expected_leaf_hmac_after_auth.clone()),
                ..fcr50::LogReplayResponse::EMPTY
            })
        });
        pinweaver.expect_get_log().times(1).returning(move |&_| {
            Ok(vec![
                fcr50::LogEntry { root_hash: Some(root_hash), ..fcr50::LogEntry::EMPTY },
                fcr50::LogEntry {
                    root_hash: Some(expected_root_hash.clone()),
                    message_type: Some(fcr50::MessageType::TryAuth),
                    label: Some(expected_label.value()),
                    ..fcr50::LogEntry::EMPTY
                },
            ])
        });
        storage.expect_load().times(1).return_once(move || Ok(hash_tree));
        storage.expect_store().times(1).return_once(|_| Ok(()));
        let result = provision(&storage, &mut lookup_table, &pinweaver).await;
        assert_matches!(result, Ok(_));
    }

    // Tests that a failed replay does not write to disk.
    #[fuchsia::test]
    async fn test_replay_failed_does_not_write() {
        let mut pinweaver = MockPinWeaverProtocol::new();
        let mut lookup_table = MockLookupTable::new();
        let mut storage = MockHashTreeStorage::new();
        let hash_tree =
            HashTree::new(TREE_HEIGHT, CHILDREN_PER_NODE).expect("unable to create hash tree");
        let root_hash = hash_tree.get_root_hash().unwrap().clone();
        pinweaver.expect_get_log().times(1).returning(move |&_| {
            Ok(vec![
                fcr50::LogEntry { root_hash: Some(root_hash.clone()), ..fcr50::LogEntry::EMPTY },
                fcr50::LogEntry {
                    root_hash: Some([1; 32]),
                    message_type: Some(fcr50::MessageType::ResetTree),
                    ..fcr50::LogEntry::EMPTY
                },
            ])
        });
        storage.expect_load().times(1).return_once(move || Ok(hash_tree));
        let result = provision(&storage, &mut lookup_table, &pinweaver).await;
        assert_matches!(result, Err(CredentialError::CorruptedMetadata));
    }
}
