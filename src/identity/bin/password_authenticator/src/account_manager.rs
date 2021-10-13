// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, Context, Error};
use fidl_fuchsia_identity_account::{
    self as faccount,
    AccountManagerRequest::{
        self, DeprecatedGetAccount, DeprecatedProvisionNewAccount, GetAccount,
        GetAccountAuthStates, GetAccountIds, GetAccountMetadata, GetAuthenticationMechanisms,
        ProvisionNewAccount, RegisterAccountListener, RemoveAccount,
    },
    AccountManagerRequestStream,
};
use futures::prelude::*;
use futures::TryStreamExt;
use log::{error, warn};
use std::marker::PhantomData;

use crate::constants::{ACCOUNT_LABEL, FUCHSIA_DATA_GUID};
use crate::disk_management::{has_zxcrypt_header, BlockDevice, Partition, PartitionManager};

// TODO(zarvox): retain binding of open channels to accounts, once account handles are implemented
pub struct AccountManager<PMT, BDT>
where
    PMT: PartitionManager<BDT>,
    BDT: BlockDevice + Partition,
{
    partition_manager: PMT,
    block_dev_type: PhantomData<BDT>,
}

// For now, we only support a single AccountId (as in the fuchsia.identity protocol).  The local
// account, if it exists, will have AccountId value 1.
const GLOBAL_ACCOUNT_ID: u64 = 1;

/// Given a partition manager, return the list of account IDs that have accounts.
/// This is achieved by enumerating the partitions known to the partition manager, looking for ones
/// with GUID FUCHSIA_DATA_GUID, label ACCOUNT_LABEL, and a first block starting with the zxcrypt
/// magic bytes.  If such a partition is found, we return a list with just a single global
/// AccountId, otherwise we return an empty list.
async fn get_account_ids<PMT, BDT>(partition_manager: &PMT) -> Result<Vec<u64>, Error>
where
    PMT: PartitionManager<BDT>,
    BDT: BlockDevice + Partition,
{
    let mut account_ids = Vec::new();

    let block_devices = partition_manager.partitions().await?;
    for block_dev in block_devices {
        match block_dev.has_guid(FUCHSIA_DATA_GUID).await {
            Ok(true) => {}
            _ => continue,
        }

        match block_dev.has_label(ACCOUNT_LABEL).await {
            Ok(true) => (),
            _ => continue,
        }

        match has_zxcrypt_header(&block_dev).await {
            Ok(true) => {
                account_ids.push(GLOBAL_ACCOUNT_ID);
                // Only bother with the first matching block device for now.
                break;
            }
            Ok(false) => (),
            _ => {
                warn!("Couldn't check header of block device with matching guid and label")
            }
        }
    }
    Ok(account_ids)
}

impl<PMT, BDT> AccountManager<PMT, BDT>
where
    PMT: PartitionManager<BDT>,
    BDT: BlockDevice + Partition,
{
    pub fn new(partition_manager: PMT) -> Self {
        Self { partition_manager, block_dev_type: PhantomData }
    }

    /// Serially process a stream of incoming AccountManager FIDL requests.
    pub async fn handle_requests_for_stream(
        &self,
        mut request_stream: AccountManagerRequestStream,
    ) {
        while let Some(request) = request_stream.try_next().await.expect("read request") {
            self.handle_request(request)
                .unwrap_or_else(|e| {
                    error!("error handling fidl request: {:#}", anyhow!(e));
                })
                .await
        }
    }

    /// Process a single AccountManager FIDL request and send a reply.
    async fn handle_request(&self, request: AccountManagerRequest) -> Result<(), Error> {
        match request {
            GetAccountIds { responder } => {
                let account_ids = get_account_ids(&self.partition_manager).await?;
                responder.send(&account_ids).context("sending GetAccountIds repsonse")?;
            }
            DeprecatedGetAccount { id: _, password: _, account: _, responder } => {
                let mut resp = Err(faccount::Error::UnsupportedOperation);
                responder.send(&mut resp).context("sending DeprecatedGetAccount response")?;
            }
            DeprecatedProvisionNewAccount { password: _, metadata: _, account: _, responder } => {
                let mut resp = Err(faccount::Error::UnsupportedOperation);
                responder
                    .send(&mut resp)
                    .context("sending DeprecatedProvisionNewAccount response")?;
            }
            GetAccountAuthStates { scenario: _, responder } => {
                let mut resp = Err(faccount::Error::UnsupportedOperation);
                responder.send(&mut resp).context("sending GetAccountAuthStates response")?;
            }
            GetAccountMetadata { id: _, responder } => {
                let mut resp = Err(faccount::Error::UnsupportedOperation);
                responder.send(&mut resp).context("sending GetAccountMetadata response")?;
            }
            GetAccount { id: _, context_provider: _, account: _, responder } => {
                let mut resp = Err(faccount::Error::UnsupportedOperation);
                responder.send(&mut resp).context("sending GetAccount response")?;
            }
            RegisterAccountListener { listener: _, options: _, responder } => {
                let mut resp = Err(faccount::Error::UnsupportedOperation);
                responder.send(&mut resp).context("sending RegisterAccountListener response")?;
            }
            RemoveAccount { id: _, force: _, responder } => {
                let mut resp = Err(faccount::Error::UnsupportedOperation);
                responder.send(&mut resp).context("sending RemoveAccount response")?;
            }
            ProvisionNewAccount { lifetime: _, auth_mechanism_id: _, responder } => {
                let mut resp = Err(faccount::Error::UnsupportedOperation);
                responder.send(&mut resp).context("sending ProvisionNewAccount response")?;
            }
            GetAuthenticationMechanisms { responder } => {
                let mut resp = Err(faccount::Error::UnsupportedOperation);
                responder
                    .send(&mut resp)
                    .context("sending GetAuthenticationMechanisms response")?;
            }
        }
        Ok(())
    }
}

#[cfg(test)]
mod test {
    use {
        super::*,
        crate::disk_management::test::{
            make_zxcrypt_superblock, MockPartition, MockPartitionManager, BLOB_GUID, DATA_GUID,
        },
        fuchsia_async as fasync,
    };

    #[fasync::run_singlethreaded(test)]
    async fn test_get_account_ids_wrong_guid() {
        let partitions = vec![MockPartition {
            guid: Ok(BLOB_GUID),
            label: Ok(ACCOUNT_LABEL.to_string()),
            first_block: Some(make_zxcrypt_superblock(4096)),
        }];
        let partition_manager = MockPartitionManager { maybe_partitions: Some(partitions) };
        let account_ids = get_account_ids(&partition_manager).await.expect("get account ids");
        assert_eq!(account_ids, Vec::<u64>::new());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_account_ids_wrong_label() {
        let partitions = vec![MockPartition {
            guid: Ok(DATA_GUID),
            label: Ok("wrong-label".to_string()),
            first_block: Some(make_zxcrypt_superblock(4096)),
        }];
        let partition_manager = MockPartitionManager { maybe_partitions: Some(partitions) };
        let account_ids = get_account_ids(&partition_manager).await.expect("get account ids");
        assert_eq!(account_ids, Vec::<u64>::new());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_account_ids_no_zxcrypt_header() {
        let partitions = vec![MockPartition {
            guid: Ok(DATA_GUID),
            label: Ok(ACCOUNT_LABEL.to_string()),
            first_block: Some([0u8; 4096].to_vec()),
        }];
        let partition_manager = MockPartitionManager { maybe_partitions: Some(partitions) };
        let account_ids = get_account_ids(&partition_manager).await.expect("get account ids");
        assert_eq!(account_ids, Vec::<u64>::new());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_account_ids_found() {
        let partitions = vec![MockPartition {
            guid: Ok(DATA_GUID),
            label: Ok(ACCOUNT_LABEL.to_string()),
            first_block: Some(make_zxcrypt_superblock(4096)),
        }];
        let partition_manager = MockPartitionManager { maybe_partitions: Some(partitions) };
        let account_ids = get_account_ids(&partition_manager).await.expect("get account ids");
        assert_eq!(account_ids, vec![GLOBAL_ACCOUNT_ID]);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_account_ids_just_one_match() {
        let partitions = vec![
            MockPartition {
                guid: Ok(DATA_GUID),
                label: Ok(ACCOUNT_LABEL.to_string()),
                first_block: Some(make_zxcrypt_superblock(4096)),
            },
            MockPartition {
                guid: Ok(DATA_GUID),
                label: Ok(ACCOUNT_LABEL.to_string()),
                first_block: Some(make_zxcrypt_superblock(4096)),
            },
        ];
        let partition_manager = MockPartitionManager { maybe_partitions: Some(partitions) };
        let account_ids = get_account_ids(&partition_manager).await.expect("get account ids");
        // Even if two partitions would match our criteria, we only return one account for now.
        assert_eq!(account_ids, vec![GLOBAL_ACCOUNT_ID]);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_account_ids_not_first_partition() {
        // Expect to ignore the first partition, but notice the second
        let partitions = vec![
            MockPartition {
                guid: Ok(DATA_GUID),
                label: Ok("wrong-label".to_string()),
                first_block: Some(make_zxcrypt_superblock(4096)),
            },
            MockPartition {
                guid: Ok(DATA_GUID),
                label: Ok(ACCOUNT_LABEL.to_string()),
                first_block: Some(make_zxcrypt_superblock(4096)),
            },
        ];
        let partition_manager = MockPartitionManager { maybe_partitions: Some(partitions) };
        let account_ids = get_account_ids(&partition_manager).await.expect("get account ids");
        assert_eq!(account_ids, vec![GLOBAL_ACCOUNT_ID]);
    }
}
