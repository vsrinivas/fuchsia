// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    constants::{ACCOUNT_LABEL, FUCHSIA_DATA_GUID},
    disk_management::{has_zxcrypt_header, BlockDevice, Partition, PartitionManager},
};
use anyhow::{anyhow, Context, Error};
use fidl::endpoints::ServerEnd;
use fidl_fuchsia_identity_account::{
    self as faccount, AccountManagerRequest, AccountManagerRequestStream, AccountMarker,
    AccountRequest, AccountRequestStream, Lifetime,
};
use futures::{lock::Mutex, prelude::*, select};
use identity_common::{TaskGroup, TaskGroupCancel};
use log::{error, warn};
use std::{collections::HashMap, marker::PhantomData};

// For now, we only support a single AccountId (as in the fuchsia.identity protocol).  The local
// account, if it exists, will have AccountId value 1.
const GLOBAL_ACCOUNT_ID: AccountId = 1;

pub type AccountId = u64;

pub struct AccountManager<PMT, BDT>
where
    PMT: PartitionManager<BDT>,
    BDT: BlockDevice + Partition,
{
    partition_manager: PMT,
    block_dev_type: PhantomData<BDT>,

    // Maps a TaskGroup to each account, allowing for the cancelation of all tasks running for a
    // particular account.
    account_tasks: Mutex<HashMap<AccountId, TaskGroup>>,
}

impl<PMT, BDT> AccountManager<PMT, BDT>
where
    PMT: PartitionManager<BDT>,
    BDT: BlockDevice + Partition,
{
    pub fn new(partition_manager: PMT) -> Self {
        Self {
            partition_manager,
            block_dev_type: PhantomData,
            account_tasks: Mutex::new(HashMap::new()),
        }
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
            AccountManagerRequest::GetAccountIds { responder } => {
                let account_ids = self.get_account_ids().await?;
                responder.send(&account_ids).context("sending GetAccountIds repsonse")?;
            }
            AccountManagerRequest::DeprecatedGetAccount { id, password, account, responder } => {
                let mut resp = self.get_account(id, password, account).await;
                responder.send(&mut resp).context("sending DeprecatedGetAccount response")?;
            }
            AccountManagerRequest::DeprecatedProvisionNewAccount {
                password: _,
                metadata: _,
                account: _,
                responder,
            } => {
                let mut resp = Err(faccount::Error::UnsupportedOperation);
                responder
                    .send(&mut resp)
                    .context("sending DeprecatedProvisionNewAccount response")?;
            }
            AccountManagerRequest::GetAccountAuthStates { scenario: _, responder } => {
                let mut resp = Err(faccount::Error::UnsupportedOperation);
                responder.send(&mut resp).context("sending GetAccountAuthStates response")?;
            }
            AccountManagerRequest::GetAccountMetadata { id: _, responder } => {
                let mut resp = Err(faccount::Error::UnsupportedOperation);
                responder.send(&mut resp).context("sending GetAccountMetadata response")?;
            }
            AccountManagerRequest::GetAccount {
                id: _,
                context_provider: _,
                account: _,
                responder,
            } => {
                let mut resp = Err(faccount::Error::UnsupportedOperation);
                responder.send(&mut resp).context("sending GetAccount response")?;
            }
            AccountManagerRequest::RegisterAccountListener {
                listener: _,
                options: _,
                responder,
            } => {
                let mut resp = Err(faccount::Error::UnsupportedOperation);
                responder.send(&mut resp).context("sending RegisterAccountListener response")?;
            }
            AccountManagerRequest::RemoveAccount { id: _, force: _, responder } => {
                let mut resp = Err(faccount::Error::UnsupportedOperation);
                responder.send(&mut resp).context("sending RemoveAccount response")?;
            }
            AccountManagerRequest::ProvisionNewAccount {
                lifetime: _,
                auth_mechanism_id: _,
                responder,
            } => {
                let mut resp = Err(faccount::Error::UnsupportedOperation);
                responder.send(&mut resp).context("sending ProvisionNewAccount response")?;
            }
            AccountManagerRequest::GetAuthenticationMechanisms { responder } => {
                let mut resp = Err(faccount::Error::UnsupportedOperation);
                responder
                    .send(&mut resp)
                    .context("sending GetAuthenticationMechanisms response")?;
            }
        }
        Ok(())
    }

    /// Return the list of account IDs that have accounts.
    /// This is achieved by enumerating the partitions known to the partition manager, looking for ones
    /// with GUID FUCHSIA_DATA_GUID, label ACCOUNT_LABEL, and a first block starting with the zxcrypt
    /// magic bytes.  If such a partition is found, we return a list with just a single global
    /// AccountId, otherwise we return an empty list.
    async fn get_account_ids(&self) -> Result<Vec<u64>, Error> {
        let mut account_ids = Vec::new();

        let block_devices = self.partition_manager.partitions().await?;
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

    /// Authenticates an account and serves the Account FIDL protocol over the `account` channel.
    /// The `id` is verified to be present, and the password is verified to be the empty string.
    /// As per `get_account_ids`, the only id that can be present is GLOBAL_ACCOUNT_ID.
    async fn get_account(
        &self,
        id: AccountId,
        password: String,
        account: ServerEnd<AccountMarker>,
    ) -> Result<(), faccount::Error> {
        // Get the list of account IDs on the device. `id` must be one of those.
        let account_ids = self.get_account_ids().await.map_err(|_| faccount::Error::NotFound)?;
        if account_ids.into_iter().find(|i| *i == id).is_none() {
            return Err(faccount::Error::NotFound);
        }

        if password != "" {
            return Err(faccount::Error::FailedAuthentication);
        }

        let account_stream = account.into_stream().map_err(|_| faccount::Error::Internal)?;
        let mut account_tasks = self.account_tasks.lock().await;
        let task_group = account_tasks.entry(id).or_insert_with(TaskGroup::new);
        task_group
            .spawn(move |cancel| Account.handle_requests_for_stream(account_stream, cancel))
            .await
            .expect("spawn");
        Ok(())
    }

    #[cfg(test)]
    async fn cancel_account_channels(&self, id: AccountId) {
        let mut account_tasks = self.account_tasks.lock().await;
        if let Some(task_group) = account_tasks.remove(&id) {
            task_group.cancel().await.expect("TaskGroup cancel");
        }
    }
}

/// Serves the `fuchsia.identity.account.Account` FIDL protocol.
struct Account;

impl Account {
    /// Serially process a stream of incoming Account FIDL requests, shutting down the channel when
    /// `cancel` is signaled.
    pub async fn handle_requests_for_stream(
        &self,
        account_stream: AccountRequestStream,
        mut cancel: TaskGroupCancel,
    ) {
        let mut account_stream = account_stream.fuse();
        loop {
            select! {
                res = account_stream.try_next() => match res {
                    Ok(Some(request)) => {
                        self.handle_request(request)
                            .unwrap_or_else(|err| {
                                error!("error handling FIDL request: {:#}", err)
                            })
                            .await;
                    }
                    Ok(None) => {
                        break;
                    }
                    Err(err) => {
                        error!("error reading FIDL request from stream: {:#}", err);
                        break;
                    }
                },
                _ = &mut cancel => {
                    warn!("Account FIDL server canceled");
                    break;
                }
            }
        }
    }

    /// Handles a single Account FIDL request.
    async fn handle_request(&self, request: AccountRequest) -> Result<(), Error> {
        match request {
            AccountRequest::Lock { responder } => {
                responder
                    .send(&mut Err(faccount::Error::UnsupportedOperation))
                    .context("sending Lock response")?;
            }
            AccountRequest::GetDataDirectory { data_directory: _, responder } => {
                responder
                    .send(&mut Err(faccount::Error::UnsupportedOperation))
                    .context("sending GetDataDirectory response")?;
            }
            AccountRequest::GetAuthState { scenario: _, responder } => {
                responder
                    .send(&mut Err(faccount::Error::UnsupportedOperation))
                    .context("sending GetAuthState response")?;
            }
            AccountRequest::GetLifetime { responder } => {
                responder.send(Lifetime::Persistent).context("sending GetLifetime response")?;
            }
            AccountRequest::GetDefaultPersona { persona: _, responder } => {
                responder
                    .send(&mut Err(faccount::Error::UnsupportedOperation))
                    .context("sending GetDefaultPersona response")?;
            }
            AccountRequest::GetPersona { id: _, persona: _, responder } => {
                responder
                    .send(&mut Err(faccount::Error::UnsupportedOperation))
                    .context("sending GetPersona response")?;
            }
            AccountRequest::GetPersonaIds { responder } => {
                responder.send(&[]).context("sending GetPersonaIds response")?;
            }
            AccountRequest::RegisterAuthListener {
                scenario: _,
                listener: _,
                initial_state: _,
                granularity: _,
                responder,
            } => {
                responder
                    .send(&mut Err(faccount::Error::UnsupportedOperation))
                    .context("sending RegisterAuthListener response")?;
            }
            AccountRequest::GetAuthMechanismEnrollments { responder } => {
                responder
                    .send(&mut Err(faccount::Error::UnsupportedOperation))
                    .context("sending GetAuthMechanismEnrollments response")?;
            }
            AccountRequest::CreateAuthMechanismEnrollment { auth_mechanism_id: _, responder } => {
                responder
                    .send(&mut Err(faccount::Error::UnsupportedOperation))
                    .context("sending CreateAuthMechanismEnrollment response")?;
            }
            AccountRequest::RemoveAuthMechanismEnrollment { enrollment_id: _, responder } => {
                responder
                    .send(&mut Err(faccount::Error::UnsupportedOperation))
                    .context("sending RemoveAuthMechanismEnrollment response")?;
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
        fidl_fuchsia_io::DirectoryMarker,
        fuchsia_async as fasync,
    };

    fn make_account_partition() -> MockPartition {
        MockPartition {
            guid: Ok(DATA_GUID),
            label: Ok(ACCOUNT_LABEL.to_string()),
            first_block: Some(make_zxcrypt_superblock(4096)),
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_account_ids_wrong_guid() {
        let partitions = vec![MockPartition {
            guid: Ok(BLOB_GUID),
            label: Ok(ACCOUNT_LABEL.to_string()),
            first_block: Some(make_zxcrypt_superblock(4096)),
        }];
        let account_manager =
            AccountManager::new(MockPartitionManager { maybe_partitions: Some(partitions) });
        let account_ids = account_manager.get_account_ids().await.expect("get account ids");
        assert_eq!(account_ids, Vec::<u64>::new());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_account_ids_wrong_label() {
        let partitions = vec![MockPartition {
            guid: Ok(DATA_GUID),
            label: Ok("wrong-label".to_string()),
            first_block: Some(make_zxcrypt_superblock(4096)),
        }];
        let account_manager =
            AccountManager::new(MockPartitionManager { maybe_partitions: Some(partitions) });
        let account_ids = account_manager.get_account_ids().await.expect("get account ids");
        assert_eq!(account_ids, Vec::<u64>::new());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_account_ids_no_zxcrypt_header() {
        let partitions = vec![MockPartition {
            guid: Ok(DATA_GUID),
            label: Ok(ACCOUNT_LABEL.to_string()),
            first_block: Some([0u8; 4096].to_vec()),
        }];
        let account_manager =
            AccountManager::new(MockPartitionManager { maybe_partitions: Some(partitions) });
        let account_ids = account_manager.get_account_ids().await.expect("get account ids");
        assert_eq!(account_ids, Vec::<u64>::new());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_account_ids_found() {
        let partitions = vec![make_account_partition()];
        let account_manager =
            AccountManager::new(MockPartitionManager { maybe_partitions: Some(partitions) });
        let account_ids = account_manager.get_account_ids().await.expect("get account ids");
        assert_eq!(account_ids, vec![GLOBAL_ACCOUNT_ID]);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_account_ids_just_one_match() {
        let partitions = vec![make_account_partition(), make_account_partition()];
        let account_manager =
            AccountManager::new(MockPartitionManager { maybe_partitions: Some(partitions) });
        let account_ids = account_manager.get_account_ids().await.expect("get account ids");
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
            make_account_partition(),
        ];
        let account_manager =
            AccountManager::new(MockPartitionManager { maybe_partitions: Some(partitions) });
        let account_ids = account_manager.get_account_ids().await.expect("get account ids");
        assert_eq!(account_ids, vec![GLOBAL_ACCOUNT_ID]);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_account_no_accounts() {
        let account_manager = AccountManager::new(MockPartitionManager { maybe_partitions: None });
        let (_, server) = fidl::endpoints::create_endpoints::<AccountMarker>().unwrap();
        assert_eq!(
            account_manager.get_account(GLOBAL_ACCOUNT_ID, "".to_string(), server).await,
            Err(faccount::Error::NotFound)
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_account_not_found() {
        const NON_EXISTENT_ACCOUNT_ID: u64 = 42;
        let partitions = vec![make_account_partition()];
        let account_manager =
            AccountManager::new(MockPartitionManager { maybe_partitions: Some(partitions) });
        let (_, server) = fidl::endpoints::create_endpoints::<AccountMarker>().unwrap();
        assert_eq!(
            account_manager.get_account(NON_EXISTENT_ACCOUNT_ID, "".to_string(), server).await,
            Err(faccount::Error::NotFound)
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_account_bad_password() {
        const BAD_PASSWORD: &str = "passwd";
        let partitions = vec![make_account_partition()];
        let account_manager =
            AccountManager::new(MockPartitionManager { maybe_partitions: Some(partitions) });
        let (_, server) = fidl::endpoints::create_endpoints::<AccountMarker>().unwrap();
        assert_eq!(
            account_manager.get_account(GLOBAL_ACCOUNT_ID, BAD_PASSWORD.to_string(), server).await,
            Err(faccount::Error::FailedAuthentication)
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_account_found() {
        let partitions = vec![make_account_partition()];
        let account_manager =
            AccountManager::new(MockPartitionManager { maybe_partitions: Some(partitions) });
        let (client, server) = fidl::endpoints::create_proxy::<AccountMarker>().unwrap();
        account_manager
            .get_account(GLOBAL_ACCOUNT_ID, "".to_string(), server)
            .await
            .expect("get account");
        let (_, server) = fidl::endpoints::create_proxy::<DirectoryMarker>().unwrap();
        assert_eq!(
            client.get_data_directory(server).await.expect("get_data_directory FIDL"),
            Err(faccount::Error::UnsupportedOperation)
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_multiple_get_account_channels_concurrent() {
        let partitions = vec![make_account_partition()];
        let account_manager =
            AccountManager::new(MockPartitionManager { maybe_partitions: Some(partitions) });
        let (client1, server) = fidl::endpoints::create_proxy::<AccountMarker>().unwrap();
        account_manager
            .get_account(GLOBAL_ACCOUNT_ID, "".to_string(), server)
            .await
            .expect("get account 1");
        let (client2, server) = fidl::endpoints::create_proxy::<AccountMarker>().unwrap();
        account_manager
            .get_account(GLOBAL_ACCOUNT_ID, "".to_string(), server)
            .await
            .expect("get account 2");
        let (_, server) = fidl::endpoints::create_proxy::<DirectoryMarker>().unwrap();
        assert_eq!(
            client1.get_data_directory(server).await.expect("get_data_directory 1 FIDL"),
            Err(faccount::Error::UnsupportedOperation)
        );
        let (_, server) = fidl::endpoints::create_proxy::<DirectoryMarker>().unwrap();
        assert_eq!(
            client2.get_data_directory(server).await.expect("get_data_directory 2 FIDL"),
            Err(faccount::Error::UnsupportedOperation)
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_multiple_get_account_channels_serial() {
        let partitions = vec![make_account_partition()];
        let account_manager =
            AccountManager::new(MockPartitionManager { maybe_partitions: Some(partitions) });
        let (client, server) = fidl::endpoints::create_proxy::<AccountMarker>().unwrap();
        account_manager
            .get_account(GLOBAL_ACCOUNT_ID, "".to_string(), server)
            .await
            .expect("get account 1");

        let (_, server) = fidl::endpoints::create_proxy::<DirectoryMarker>().unwrap();
        assert_eq!(
            client.get_data_directory(server).await.expect("get_data_directory 1 FIDL"),
            Err(faccount::Error::UnsupportedOperation)
        );
        drop(client);
        let (client, server) = fidl::endpoints::create_proxy::<AccountMarker>().unwrap();
        account_manager
            .get_account(GLOBAL_ACCOUNT_ID, "".to_string(), server)
            .await
            .expect("get account 2");
        let (_, server) = fidl::endpoints::create_proxy::<DirectoryMarker>().unwrap();
        assert_eq!(
            client.get_data_directory(server).await.expect("get_data_directory 2 FIDL"),
            Err(faccount::Error::UnsupportedOperation)
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_account_shutdown() {
        let partitions = vec![make_account_partition()];
        let account_manager =
            AccountManager::new(MockPartitionManager { maybe_partitions: Some(partitions) });
        let (client, server) = fidl::endpoints::create_proxy::<AccountMarker>().unwrap();
        account_manager
            .get_account(GLOBAL_ACCOUNT_ID, "".to_string(), server)
            .await
            .expect("get account");
        let (_, server) = fidl::endpoints::create_proxy::<DirectoryMarker>().unwrap();
        assert_eq!(
            client.get_data_directory(server).await.expect("get_data_directory FIDL"),
            Err(faccount::Error::UnsupportedOperation)
        );
        account_manager.cancel_account_channels(GLOBAL_ACCOUNT_ID).await;
        let (_, server) = fidl::endpoints::create_proxy::<DirectoryMarker>().unwrap();
        let err =
            client.get_data_directory(server).await.expect_err("get_data_directory should fail");
        assert!(err.is_closed());
    }
}
