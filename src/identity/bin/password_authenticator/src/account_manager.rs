// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    constants::{ACCOUNT_LABEL, FUCHSIA_DATA_GUID},
    disk_management::{DiskError, DiskManager, EncryptedBlockDevice, Partition},
    keys::KeyDerivation,
    prototype::{GLOBAL_ACCOUNT_ID, GLOBAL_ACCOUNT_PASSWORD},
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

pub type AccountId = u64;

pub struct AccountManager<DM, KD> {
    disk_manager: DM,
    key_derivation: KD,

    // Maps a TaskGroup to each account, allowing for the cancelation of all tasks running for a
    // particular account.
    account_tasks: Mutex<HashMap<AccountId, TaskGroup>>,
}

impl<DM, KD> AccountManager<DM, KD>
where
    DM: DiskManager,
    KD: KeyDerivation,
{
    pub fn new(disk_manager: DM, key_derivation: KD) -> Self {
        Self { disk_manager, key_derivation, account_tasks: Mutex::new(HashMap::new()) }
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
                password,
                metadata: _,
                account,
                responder,
            } => {
                let mut resp = self
                    .provision_new_account(password.clone())
                    .and_then(|account_id| self.get_account(account_id, password, account))
                    .await;
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

    /// Returns a stream of partitions that match the user data account GUID and label.
    /// Skips any partitions whose labels or GUIDs can't be read.
    async fn get_account_partitions(
        &self,
    ) -> Result<impl Stream<Item = DM::BlockDevice>, DiskError> {
        let partitions = self.disk_manager.partitions().await?;
        Ok(futures::stream::iter(partitions).filter_map(|partition| async {
            match partition.has_guid(FUCHSIA_DATA_GUID).await {
                Ok(true) => match partition.has_label(ACCOUNT_LABEL).await {
                    Ok(true) => Some(partition.into_block_device()),
                    _ => None,
                },
                _ => None,
            }
        }))
    }

    /// Find the first partition that matches the user data account GUID and label.
    async fn find_account_partition(&self) -> Option<DM::BlockDevice> {
        let account_partitions = self.get_account_partitions().await.ok()?;
        futures::pin_mut!(account_partitions);
        // Return the first matching partition.
        account_partitions.next().await
    }

    /// Return the list of account IDs that have accounts.
    /// This is achieved by enumerating the partitions known to the partition manager, looking for
    /// ones with GUID FUCHSIA_DATA_GUID, label ACCOUNT_LABEL, and a first block starting with the
    /// zxcrypt magic bytes.  If such a partition is found, we return a list with just a single
    /// global AccountId, otherwise we return an empty list.
    async fn get_account_ids(&self) -> Result<Vec<u64>, Error> {
        let mut account_ids = Vec::new();

        let account_partitions = self.get_account_partitions().await?;
        futures::pin_mut!(account_partitions);
        while let Some(partition) = account_partitions.next().await {
            match self.disk_manager.has_zxcrypt_header(&partition).await {
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

        if password != GLOBAL_ACCOUNT_PASSWORD {
            return Err(faccount::Error::FailedAuthentication);
        }

        let account_stream = account.into_stream().map_err(|_| faccount::Error::Resource)?;
        let mut account_tasks = self.account_tasks.lock().await;
        let task_group = account_tasks.entry(id).or_insert_with(TaskGroup::new);
        task_group
            .spawn(move |cancel| Account.handle_requests_for_stream(account_stream, cancel))
            .await
            .map_err(|_| faccount::Error::Resource)?;
        Ok(())
    }

    async fn provision_new_account(&self, password: String) -> Result<AccountId, faccount::Error> {
        if password != GLOBAL_ACCOUNT_PASSWORD {
            return Err(faccount::Error::InvalidRequest);
        }

        let block = self.find_account_partition().await.ok_or(faccount::Error::NotFound)?;

        // Check that an account has not already been provisioned.
        if self
            .disk_manager
            .has_zxcrypt_header(&block)
            .await
            .map_err(|_| faccount::Error::Resource)?
        {
            return Err(faccount::Error::FailedPrecondition);
        }

        let key = self.key_derivation.derive_key(&password)?;
        let res: Result<AccountId, DiskError> = async {
            let encrypted_block = self.disk_manager.bind_to_encrypted_block(block).await?;
            encrypted_block.format(&key).await?;
            let unsealed_block = encrypted_block.unseal(&key).await?;
            self.disk_manager.format_minfs(&unsealed_block).await?;
            Ok(GLOBAL_ACCOUNT_ID)
        }
        .await;
        match res {
            Ok(id) => Ok(id),
            Err(err) => {
                error!("Failed to provision new account: {:?}", err);
                Err(err.into())
            }
        }
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
        crate::{disk_management::DiskError, keys::Key, prototype::NullKeyDerivation},
        async_trait::async_trait,
        fidl_fuchsia_io::DirectoryMarker,
        fuchsia_zircon::Status,
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
        type EncryptedBlockDevice = MockEncryptedBlockDevice;

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

        async fn bind_to_encrypted_block(
            &self,
            block_dev: MockBlockDevice,
        ) -> Result<MockEncryptedBlockDevice, DiskError> {
            block_dev.bind.map_err(|err_factory| err_factory())
        }

        async fn format_minfs(&self, _block_dev: &MockBlockDevice) -> Result<(), DiskError> {
            Ok(())
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
        // Whether or not the block device should succeed in binding zxcrypt
        bind: Result<MockEncryptedBlockDevice, fn() -> DiskError>,
    }

    /// A mock implementation of [`EncryptedBlockDevice`].
    #[derive(Debug, Clone)]
    struct MockEncryptedBlockDevice {
        // Whether the block encrypted block device can format successfully.
        format: Result<(), fn() -> DiskError>,
        // Whether the block encrypted block device can be unsealed.
        unseal: Result<Box<MockBlockDevice>, fn() -> DiskError>,
    }

    #[async_trait]
    impl EncryptedBlockDevice for MockEncryptedBlockDevice {
        type BlockDevice = MockBlockDevice;

        async fn format(&self, _key: &Key) -> Result<(), DiskError> {
            self.format.clone().map_err(|err_factory| err_factory())
        }

        async fn unseal(&self, _key: &Key) -> Result<MockBlockDevice, DiskError> {
            self.unseal.clone().map(|b| *b).map_err(|err_factory| err_factory())
        }
    }

    // Create a partition whose GUID and label match the account partition,
    // and whose block device has a zxcrypt header.
    fn make_formatted_account_partition() -> MockPartition {
        MockPartition {
            guid: Ok(Match::Any),
            label: Ok(Match::Any),
            block: MockBlockDevice {
                zxcrypt_header: Ok(Match::Any),
                bind: Ok(MockEncryptedBlockDevice {
                    format: Ok(()),
                    unseal: Ok(Box::new(MockBlockDevice {
                        zxcrypt_header: Ok(Match::None),
                        bind: Err(|| DiskError::BindZxcryptDriverFailed(Status::NOT_SUPPORTED)),
                    })),
                }),
            },
        }
    }

    // Create a partition whose GUID and label match the account partition,
    // and whose block device does not have a zxcrypt header.
    fn make_unformatted_account_partition() -> MockPartition {
        MockPartition {
            guid: Ok(Match::Any),
            label: Ok(Match::Any),
            block: MockBlockDevice {
                zxcrypt_header: Ok(Match::None),
                bind: Ok(MockEncryptedBlockDevice {
                    format: Ok(()),
                    unseal: Ok(Box::new(MockBlockDevice {
                        zxcrypt_header: Ok(Match::None),
                        bind: Err(|| DiskError::BindZxcryptDriverFailed(Status::NOT_SUPPORTED)),
                    })),
                }),
            },
        }
    }

    #[fuchsia::test]
    async fn test_get_account_ids_wrong_guid() {
        let disk_manager = MockDiskManager::new().with_partition(MockPartition {
            guid: Ok(Match::None),
            label: Ok(Match::Any),
            block: MockBlockDevice {
                zxcrypt_header: Ok(Match::Any),
                bind: Err(|| DiskError::BindZxcryptDriverFailed(Status::NOT_SUPPORTED)),
            },
        });
        let account_manager = AccountManager::new(disk_manager, NullKeyDerivation);
        let account_ids = account_manager.get_account_ids().await.expect("get account ids");
        assert_eq!(account_ids, Vec::<u64>::new());
    }

    #[fuchsia::test]
    async fn test_get_account_ids_wrong_label() {
        let disk_manager = MockDiskManager::new().with_partition(MockPartition {
            guid: Ok(Match::Any),
            label: Ok(Match::None),
            block: MockBlockDevice {
                zxcrypt_header: Ok(Match::Any),
                bind: Err(|| DiskError::BindZxcryptDriverFailed(Status::NOT_SUPPORTED)),
            },
        });
        let account_manager = AccountManager::new(disk_manager, NullKeyDerivation);
        let account_ids = account_manager.get_account_ids().await.expect("get account ids");
        assert_eq!(account_ids, Vec::<u64>::new());
    }

    #[fuchsia::test]
    async fn test_get_account_ids_no_zxcrypt_header() {
        let disk_manager = MockDiskManager::new().with_partition(MockPartition {
            guid: Ok(Match::Any),
            label: Ok(Match::Any),
            block: MockBlockDevice {
                zxcrypt_header: Ok(Match::None),
                bind: Err(|| DiskError::BindZxcryptDriverFailed(Status::NOT_SUPPORTED)),
            },
        });
        let account_manager = AccountManager::new(disk_manager, NullKeyDerivation);
        let account_ids = account_manager.get_account_ids().await.expect("get account ids");
        assert_eq!(account_ids, Vec::<u64>::new());
    }

    #[fuchsia::test]
    async fn test_get_account_ids_found() {
        let disk_manager =
            MockDiskManager::new().with_partition(make_formatted_account_partition());
        let account_manager = AccountManager::new(disk_manager, NullKeyDerivation);
        let account_ids = account_manager.get_account_ids().await.expect("get account ids");
        assert_eq!(account_ids, vec![GLOBAL_ACCOUNT_ID]);
    }

    #[fuchsia::test]
    async fn test_get_account_ids_just_one_match() {
        let disk_manager = MockDiskManager::new()
            .with_partition(make_formatted_account_partition())
            .with_partition(make_formatted_account_partition());
        let account_manager = AccountManager::new(disk_manager, NullKeyDerivation);
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
                block: MockBlockDevice {
                    zxcrypt_header: Ok(Match::Any),
                    bind: Err(|| DiskError::BindZxcryptDriverFailed(Status::NOT_SUPPORTED)),
                },
            })
            .with_partition(make_formatted_account_partition());
        let account_manager = AccountManager::new(disk_manager, NullKeyDerivation);
        let account_ids = account_manager.get_account_ids().await.expect("get account ids");
        assert_eq!(account_ids, vec![GLOBAL_ACCOUNT_ID]);
    }

    #[fuchsia::test]
    async fn test_get_account_no_accounts() {
        let account_manager = AccountManager::new(MockDiskManager::new(), NullKeyDerivation);
        let (_, server) = fidl::endpoints::create_endpoints::<AccountMarker>().unwrap();
        assert_eq!(
            account_manager
                .get_account(GLOBAL_ACCOUNT_ID, GLOBAL_ACCOUNT_PASSWORD.to_string(), server)
                .await,
            Err(faccount::Error::NotFound)
        );
    }

    #[fuchsia::test]
    async fn test_get_account_not_found() {
        const NON_EXISTENT_ACCOUNT_ID: u64 = 42;
        let disk_manager =
            MockDiskManager::new().with_partition(make_formatted_account_partition());
        let account_manager = AccountManager::new(disk_manager, NullKeyDerivation);
        let (_, server) = fidl::endpoints::create_endpoints::<AccountMarker>().unwrap();
        assert_eq!(
            account_manager
                .get_account(NON_EXISTENT_ACCOUNT_ID, GLOBAL_ACCOUNT_PASSWORD.to_string(), server)
                .await,
            Err(faccount::Error::NotFound)
        );
    }

    #[fuchsia::test]
    async fn test_get_account_bad_password() {
        const BAD_PASSWORD: &str = "passwd";
        let disk_manager =
            MockDiskManager::new().with_partition(make_formatted_account_partition());
        let account_manager = AccountManager::new(disk_manager, NullKeyDerivation);
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
        let account_manager = AccountManager::new(disk_manager, NullKeyDerivation);
        let (client, server) = fidl::endpoints::create_proxy::<AccountMarker>().unwrap();
        account_manager
            .get_account(GLOBAL_ACCOUNT_ID, GLOBAL_ACCOUNT_PASSWORD.to_string(), server)
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
        let account_manager = AccountManager::new(disk_manager, NullKeyDerivation);
        let (client1, server) = fidl::endpoints::create_proxy::<AccountMarker>().unwrap();
        account_manager
            .get_account(GLOBAL_ACCOUNT_ID, GLOBAL_ACCOUNT_PASSWORD.to_string(), server)
            .await
            .expect("get account 1");
        let (client2, server) = fidl::endpoints::create_proxy::<AccountMarker>().unwrap();
        account_manager
            .get_account(GLOBAL_ACCOUNT_ID, GLOBAL_ACCOUNT_PASSWORD.to_string(), server)
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
        let account_manager = AccountManager::new(disk_manager, NullKeyDerivation);
        let (client, server) = fidl::endpoints::create_proxy::<AccountMarker>().unwrap();
        account_manager
            .get_account(GLOBAL_ACCOUNT_ID, GLOBAL_ACCOUNT_PASSWORD.to_string(), server)
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
            .get_account(GLOBAL_ACCOUNT_ID, GLOBAL_ACCOUNT_PASSWORD.to_string(), server)
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
        let account_manager = AccountManager::new(disk_manager, NullKeyDerivation);
        let (client, server) = fidl::endpoints::create_proxy::<AccountMarker>().unwrap();
        account_manager
            .get_account(GLOBAL_ACCOUNT_ID, GLOBAL_ACCOUNT_PASSWORD.to_string(), server)
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

    #[fuchsia::test]
    async fn test_deprecated_provision_new_account_on_formatted_block() {
        let disk_manager =
            MockDiskManager::new().with_partition(make_formatted_account_partition());
        let account_manager = AccountManager::new(disk_manager, NullKeyDerivation);
        assert_eq!(
            account_manager.provision_new_account(GLOBAL_ACCOUNT_PASSWORD.to_string()).await,
            Err(faccount::Error::FailedPrecondition)
        );
    }

    #[fuchsia::test]
    async fn test_deprecated_provision_new_account_on_unformatted_block() {
        let disk_manager =
            MockDiskManager::new().with_partition(make_unformatted_account_partition());
        let account_manager = AccountManager::new(disk_manager, NullKeyDerivation);
        assert_eq!(
            account_manager
                .provision_new_account(GLOBAL_ACCOUNT_PASSWORD.to_string())
                .await
                .expect("provision account"),
            GLOBAL_ACCOUNT_ID
        );
    }

    #[fuchsia::test]
    async fn test_deprecated_provision_new_account_password_not_empty() {
        let disk_manager =
            MockDiskManager::new().with_partition(make_unformatted_account_partition());
        let account_manager = AccountManager::new(disk_manager, NullKeyDerivation);
        assert_eq!(
            account_manager.provision_new_account("passwd".to_string()).await,
            Err(faccount::Error::InvalidRequest)
        );
    }

    #[fuchsia::test]
    async fn test_deprecated_provision_new_account_zxcrypt_driver_failed() {
        let disk_manager = MockDiskManager::new().with_partition(MockPartition {
            guid: Ok(Match::Any),
            label: Ok(Match::Any),
            block: MockBlockDevice {
                zxcrypt_header: Ok(Match::None),
                bind: Err(|| DiskError::BindZxcryptDriverFailed(Status::UNAVAILABLE)),
            },
        });
        let account_manager = AccountManager::new(disk_manager, NullKeyDerivation);
        assert_eq!(
            account_manager.provision_new_account(GLOBAL_ACCOUNT_PASSWORD.to_string()).await,
            Err(faccount::Error::Resource)
        );
    }

    #[fuchsia::test]
    async fn test_deprecated_provision_new_account_format_failed() {
        let disk_manager = MockDiskManager::new().with_partition(MockPartition {
            guid: Ok(Match::Any),
            label: Ok(Match::Any),
            block: MockBlockDevice {
                zxcrypt_header: Ok(Match::None),
                bind: Ok(MockEncryptedBlockDevice {
                    format: Err(|| DiskError::FailedToFormatZxcrypt(Status::IO)),
                    unseal: Err(|| DiskError::FailedToUnsealZxcrypt(Status::IO)),
                }),
            },
        });
        let account_manager = AccountManager::new(disk_manager, NullKeyDerivation);
        assert_eq!(
            account_manager.provision_new_account(GLOBAL_ACCOUNT_PASSWORD.to_string()).await,
            Err(faccount::Error::Resource)
        );
    }

    #[fuchsia::test]
    async fn test_deprecated_provision_new_account_unseal_failed() {
        let disk_manager = MockDiskManager::new().with_partition(MockPartition {
            guid: Ok(Match::Any),
            label: Ok(Match::Any),
            block: MockBlockDevice {
                zxcrypt_header: Ok(Match::None),
                bind: Ok(MockEncryptedBlockDevice {
                    format: Ok(()),
                    unseal: Err(|| DiskError::FailedToUnsealZxcrypt(Status::IO)),
                }),
            },
        });
        let account_manager = AccountManager::new(disk_manager, NullKeyDerivation);
        assert_eq!(
            account_manager.provision_new_account(GLOBAL_ACCOUNT_PASSWORD.to_string()).await,
            Err(faccount::Error::Resource)
        );
    }
}
