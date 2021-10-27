// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    constants::{ACCOUNT_LABEL, FUCHSIA_DATA_GUID},
    disk_management::{DiskManager, Partition},
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
use std::collections::HashMap;

// For now, we only support a single AccountId (as in the fuchsia.identity protocol).  The local
// account, if it exists, will have AccountId value 1.
const GLOBAL_ACCOUNT_ID: AccountId = 1;

pub type AccountId = u64;

pub struct AccountManager<DM> {
    disk_manager: DM,

    // Maps a TaskGroup to each account, allowing for the cancelation of all tasks running for a
    // particular account.
    account_tasks: Mutex<HashMap<AccountId, TaskGroup>>,
}

impl<DM> AccountManager<DM>
where
    DM: DiskManager,
{
    pub fn new(disk_manager: DM) -> Self {
        Self { disk_manager, account_tasks: Mutex::new(HashMap::new()) }
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

        let partitions = self.disk_manager.partitions().await?;
        for partition in partitions {
            match partition.has_guid(FUCHSIA_DATA_GUID).await {
                Ok(true) => {}
                _ => continue,
            }

            match partition.has_label(ACCOUNT_LABEL).await {
                Ok(true) => (),
                _ => continue,
            }

            let block_device = partition.into_block_device();
            match self.disk_manager.has_zxcrypt_header(&block_device).await {
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
        super::*, crate::disk_management::DiskError, async_trait::async_trait,
        fidl_fuchsia_io::DirectoryMarker, fuchsia_zircon::Status,
    };

    /// Mock implementation of [`DiskManager`].
    struct MockDiskManager {
        // If no partition list is given, partitions() (from the DiskManager trait) will return
        // an error.
        maybe_partitions: Option<Vec<MockPartition>>,
    }

    #[async_trait]
    impl DiskManager for MockDiskManager {
        type BlockDevice = MockBlockDevice;
        type Partition = MockPartition;

        async fn partitions(&self) -> Result<Vec<MockPartition>, DiskError> {
            self.maybe_partitions
                .clone()
                .ok_or_else(|| DiskError::GetBlockInfoFailed(Status::NOT_FOUND))
        }

        async fn has_zxcrypt_header(&self, block_dev: &MockBlockDevice) -> Result<bool, DiskError> {
            match &block_dev.zxcrypt_header {
                Ok(Match::Any) => Ok(true),
                Ok(Match::None) => Ok(false),
                Err(err_factory) => Err(err_factory()),
            }
        }
    }

    impl MockDiskManager {
        fn new() -> Self {
            Self { maybe_partitions: None }
        }

        fn with_partition(self, partition: MockPartition) -> Self {
            let mut partitions = self.maybe_partitions.unwrap_or_else(Vec::new);
            partitions.push(partition);
            MockDiskManager { maybe_partitions: Some(partitions) }
        }
    }

    /// Whether a mock's input should be considered a match for the test case.
    #[derive(Debug, Clone, Copy)]
    enum Match {
        /// Any input is considered a match.
        Any,
        /// Regardless of input, there is no match.
        None,
    }

    /// A mock implementation of [`Partition`].
    #[derive(Debug, Clone)]
    struct MockPartition {
        // Whether the mock's `has_guid` method will match any given GUID, or produce an error.
        guid: Result<Match, fn() -> DiskError>,

        // Whether the mock's `has_label` method will match any given label, or produce an error.
        label: Result<Match, fn() -> DiskError>,

        // BlockDevice representing the partition data.
        block: MockBlockDevice,
    }

    #[async_trait]
    impl Partition for MockPartition {
        type BlockDevice = MockBlockDevice;

        async fn has_guid(&self, _desired_guid: [u8; 16]) -> Result<bool, DiskError> {
            match &self.guid {
                Ok(Match::Any) => Ok(true),
                Ok(Match::None) => Ok(false),
                Err(err_factory) => Err(err_factory()),
            }
        }

        async fn has_label(&self, _desired_label: &str) -> Result<bool, DiskError> {
            match &self.label {
                Ok(Match::Any) => Ok(true),
                Ok(Match::None) => Ok(false),
                Err(err_factory) => Err(err_factory()),
            }
        }

        fn into_block_device(self) -> MockBlockDevice {
            self.block
        }
    }

    #[derive(Debug, Clone)]
    struct MockBlockDevice {
        // Whether or not the block device has a zxcrypt header in the first block.
        zxcrypt_header: Result<Match, fn() -> DiskError>,
    }

    // Create a partition whose GUID and label match the account partition,
    // and whose block device has a zxcrypt header.
    fn make_formatted_account_partition() -> MockPartition {
        MockPartition {
            guid: Ok(Match::Any),
            label: Ok(Match::Any),
            block: MockBlockDevice { zxcrypt_header: Ok(Match::Any) },
        }
    }

    #[fuchsia::test]
    async fn test_get_account_ids_wrong_guid() {
        let disk_manager = MockDiskManager::new().with_partition(MockPartition {
            guid: Ok(Match::None),
            label: Ok(Match::Any),
            block: MockBlockDevice { zxcrypt_header: Ok(Match::Any) },
        });
        let account_manager = AccountManager::new(disk_manager);
        let account_ids = account_manager.get_account_ids().await.expect("get account ids");
        assert_eq!(account_ids, Vec::<u64>::new());
    }

    #[fuchsia::test]
    async fn test_get_account_ids_wrong_label() {
        let disk_manager = MockDiskManager::new().with_partition(MockPartition {
            guid: Ok(Match::Any),
            label: Ok(Match::None),
            block: MockBlockDevice { zxcrypt_header: Ok(Match::Any) },
        });
        let account_manager = AccountManager::new(disk_manager);
        let account_ids = account_manager.get_account_ids().await.expect("get account ids");
        assert_eq!(account_ids, Vec::<u64>::new());
    }

    #[fuchsia::test]
    async fn test_get_account_ids_no_zxcrypt_header() {
        let disk_manager = MockDiskManager::new().with_partition(MockPartition {
            guid: Ok(Match::Any),
            label: Ok(Match::Any),
            block: MockBlockDevice { zxcrypt_header: Ok(Match::None) },
        });
        let account_manager = AccountManager::new(disk_manager);
        let account_ids = account_manager.get_account_ids().await.expect("get account ids");
        assert_eq!(account_ids, Vec::<u64>::new());
    }

    #[fuchsia::test]
    async fn test_get_account_ids_found() {
        let disk_manager =
            MockDiskManager::new().with_partition(make_formatted_account_partition());
        let account_manager = AccountManager::new(disk_manager);
        let account_ids = account_manager.get_account_ids().await.expect("get account ids");
        assert_eq!(account_ids, vec![GLOBAL_ACCOUNT_ID]);
    }

    #[fuchsia::test]
    async fn test_get_account_ids_just_one_match() {
        let disk_manager = MockDiskManager::new()
            .with_partition(make_formatted_account_partition())
            .with_partition(make_formatted_account_partition());
        let account_manager = AccountManager::new(disk_manager);
        let account_ids = account_manager.get_account_ids().await.expect("get account ids");
        // Even if two partitions would match our criteria, we only return one account for now.
        assert_eq!(account_ids, vec![GLOBAL_ACCOUNT_ID]);
    }

    #[fuchsia::test]
    async fn test_get_account_ids_not_first_partition() {
        // Expect to ignore the first partition, but notice the second
        let disk_manager = MockDiskManager::new()
            .with_partition(MockPartition {
                guid: Ok(Match::Any),
                label: Ok(Match::None),
                block: MockBlockDevice { zxcrypt_header: Ok(Match::Any) },
            })
            .with_partition(make_formatted_account_partition());
        let account_manager = AccountManager::new(disk_manager);
        let account_ids = account_manager.get_account_ids().await.expect("get account ids");
        assert_eq!(account_ids, vec![GLOBAL_ACCOUNT_ID]);
    }

    #[fuchsia::test]
    async fn test_get_account_no_accounts() {
        let account_manager = AccountManager::new(MockDiskManager::new());
        let (_, server) = fidl::endpoints::create_endpoints::<AccountMarker>().unwrap();
        assert_eq!(
            account_manager.get_account(GLOBAL_ACCOUNT_ID, "".to_string(), server).await,
            Err(faccount::Error::NotFound)
        );
    }

    #[fuchsia::test]
    async fn test_get_account_not_found() {
        const NON_EXISTENT_ACCOUNT_ID: u64 = 42;
        let disk_manager =
            MockDiskManager::new().with_partition(make_formatted_account_partition());
        let account_manager = AccountManager::new(disk_manager);
        let (_, server) = fidl::endpoints::create_endpoints::<AccountMarker>().unwrap();
        assert_eq!(
            account_manager.get_account(NON_EXISTENT_ACCOUNT_ID, "".to_string(), server).await,
            Err(faccount::Error::NotFound)
        );
    }

    #[fuchsia::test]
    async fn test_get_account_bad_password() {
        const BAD_PASSWORD: &str = "passwd";
        let disk_manager =
            MockDiskManager::new().with_partition(make_formatted_account_partition());
        let account_manager = AccountManager::new(disk_manager);
        let (_, server) = fidl::endpoints::create_endpoints::<AccountMarker>().unwrap();
        assert_eq!(
            account_manager.get_account(GLOBAL_ACCOUNT_ID, BAD_PASSWORD.to_string(), server).await,
            Err(faccount::Error::FailedAuthentication)
        );
    }

    #[fuchsia::test]
    async fn test_get_account_found() {
        let disk_manager =
            MockDiskManager::new().with_partition(make_formatted_account_partition());
        let account_manager = AccountManager::new(disk_manager);
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

    #[fuchsia::test]
    async fn test_multiple_get_account_channels_concurrent() {
        let disk_manager =
            MockDiskManager::new().with_partition(make_formatted_account_partition());
        let account_manager = AccountManager::new(disk_manager);
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

    #[fuchsia::test]
    async fn test_multiple_get_account_channels_serial() {
        let disk_manager =
            MockDiskManager::new().with_partition(make_formatted_account_partition());
        let account_manager = AccountManager::new(disk_manager);
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

    #[fuchsia::test]
    async fn test_account_shutdown() {
        let disk_manager =
            MockDiskManager::new().with_partition(make_formatted_account_partition());
        let account_manager = AccountManager::new(disk_manager);
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
