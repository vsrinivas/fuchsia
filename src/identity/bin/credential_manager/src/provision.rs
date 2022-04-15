// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    hash_tree::{
        HashTree, HashTreeError, HashTreeStorage, BITS_PER_LEVEL, CHILDREN_PER_NODE, TREE_HEIGHT,
    },
    lookup_table::LookupTable,
    pinweaver::PinWeaverProtocol,
};
use fidl_fuchsia_identity_credential::CredentialError;
use log::{error, info};

/// Detects if there is an existing |hash_tree| in which case it will
/// load it from disk. If no |hash_tree| exists the |CredentialManager| will
/// reset the CR50 via |pinweaver| and create and store a new |hash_tree|.
pub async fn provision<HS: HashTreeStorage, PW: PinWeaverProtocol, LT: LookupTable>(
    hash_tree_storage: &HS,
    lookup_table: &mut LT,
    pinweaver: &PW,
) -> Result<HashTree, CredentialError> {
    match hash_tree_storage.load() {
        Ok(hash_tree) => Ok(hash_tree),
        Err(HashTreeError::DataStoreNotFound) => {
            info!("Could not read hash tree file, resetting");
            reset_state(hash_tree_storage, lookup_table, pinweaver).await
        }
        Err(err) => {
            // If the existing hash tree fails to deserialize return a fatal error rather than
            // resetting so we don't destroy data that would be helpful to isolate the problem.
            // TODO(benwright,jsankey): Reconsider this decision once the system is more mature.
            error!("Error loading hash tree: {:?}", err);
            Err(CredentialError::InternalError)
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
