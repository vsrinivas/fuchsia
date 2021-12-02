// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    account::{Account, CheckNewClientResult},
    constants::{ACCOUNT_LABEL, FUCHSIA_DATA_GUID},
    disk_management::{DiskError, DiskManager, EncryptedBlockDevice, Partition},
    keys::{Key, KeyDerivation},
    prototype::{GLOBAL_ACCOUNT_ID, GLOBAL_ACCOUNT_PASSWORD},
};
use anyhow::{anyhow, Context, Error};
use fidl::endpoints::ServerEnd;
use fidl_fuchsia_identity_account::{
    self as faccount, AccountManagerRequest, AccountManagerRequestStream, AccountMarker,
};
use futures::{lock::Mutex, prelude::*};
use log::{error, warn};
use std::{collections::HashMap, sync::Arc};

pub type AccountId = u64;

pub struct AccountManager<DM, KD>
where
    DM: DiskManager,
    KD: KeyDerivation,
{
    disk_manager: DM,
    key_derivation: KD,

    accounts: Mutex<HashMap<AccountId, AccountState<DM::EncryptedBlockDevice, DM::Minfs>>>,
}

/// The external state of the account.
enum AccountState<EB, M> {
    Provisioning(Arc<Mutex<()>>),
    Provisioned(Arc<Account<EB, M>>),
}

impl<DM, KD> AccountManager<DM, KD>
where
    DM: DiskManager,
    KD: KeyDerivation,
{
    pub fn new(disk_manager: DM, key_derivation: KD) -> Self {
        Self { disk_manager, key_derivation, accounts: Mutex::new(HashMap::new()) }
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
        server_end: ServerEnd<AccountMarker>,
    ) -> Result<(), faccount::Error> {
        if password != GLOBAL_ACCOUNT_PASSWORD {
            return Err(faccount::Error::FailedAuthentication);
        }

        let key = self.key_derivation.derive_key(&password)?;

        // Acquire the lock for all accounts.
        let mut accounts_locked = self.accounts.lock().await;
        let account = match accounts_locked.get(&id) {
            Some(AccountState::Provisioned(account)) => {
                // Attempt to authenticate with the account using the derived key.
                match account.check_new_client(&key).await {
                    CheckNewClientResult::Locked => {
                        // The account has been sealed. We'll need to unseal the account from disk.
                        let account = self.unseal_account(id, &key).await?;
                        accounts_locked.insert(id, AccountState::Provisioned(account.clone()));
                        account
                    }
                    CheckNewClientResult::UnlockedSameKey => {
                        // The account is unsealed and the keys match. We can reuse this `Account`
                        // instance.
                        // It is possible for this Account to be sealed by the time the new client
                        // channel is served below. The result will be that the new channel is
                        // dropped as soon as it is scheduled to be served.
                        account.clone()
                    }
                    CheckNewClientResult::UnlockedDifferentKey => {
                        // The account is unsealed but the keys don't match.
                        return Err(faccount::Error::FailedAuthentication);
                    }
                }
            }
            Some(AccountState::Provisioning(_)) => {
                // This account is in the process of being provisioned, treat it like it doesn't
                // exist.
                return Err(faccount::Error::NotFound);
            }
            None => {
                // There is no account associated with the ID in memory. Check if the account can
                // be unsealed from disk.
                let account = self.unseal_account(id, &key).await?;
                accounts_locked.insert(id, AccountState::Provisioned(account.clone()));
                account
            }
        };

        account
            .clone()
            .handle_requests_for_stream(
                server_end.into_stream().map_err(|_| faccount::Error::Resource)?,
            )
            .await
            .map_err(|_| faccount::Error::Resource)?;
        Ok(())
    }

    async fn unseal_account(
        &self,
        id: AccountId,
        key: &Key,
    ) -> Result<Arc<Account<DM::EncryptedBlockDevice, DM::Minfs>>, faccount::Error> {
        let account_ids = self.get_account_ids().await.map_err(|_| faccount::Error::NotFound)?;
        if account_ids.into_iter().find(|i| *i == id).is_none() {
            return Err(faccount::Error::NotFound);
        }
        let block_device = self.find_account_partition().await.ok_or(faccount::Error::NotFound)?;
        let encrypted_block = self.disk_manager.bind_to_encrypted_block(block_device).await?;
        let block_device = match encrypted_block.unseal(&key).await {
            Ok(block_device) => block_device,
            Err(DiskError::FailedToUnsealZxcrypt(_)) => {
                return Err(faccount::Error::FailedAuthentication)
            }
            Err(err) => return Err(err.into()),
        };
        let minfs = self.disk_manager.serve_minfs(block_device).await?;
        Ok(Arc::new(Account::new(key.clone(), encrypted_block, minfs)))
    }

    async fn provision_new_account(&self, password: String) -> Result<AccountId, faccount::Error> {
        if password != GLOBAL_ACCOUNT_PASSWORD {
            return Err(faccount::Error::InvalidRequest);
        }

        // Acquire the lock for all accounts.
        let mut accounts_locked = self.accounts.lock().await;

        // Check if the global account is already provisioned or being provisioned.
        if let Some(state) = accounts_locked.get(&GLOBAL_ACCOUNT_ID) {
            match state {
                AccountState::Provisioned(_) => return Err(faccount::Error::FailedPrecondition),
                AccountState::Provisioning(lock) => {
                    // The account was being provisioned at some point. Try to acquire the lock.
                    if lock.try_lock().is_none() {
                        // The lock is locked, someone else is provisioning the account.
                        return Err(faccount::Error::FailedPrecondition);
                    }
                    // The lock was unlocked, meaning the original provisioner failed.
                }
            }
        }

        let block = self.find_account_partition().await.ok_or(faccount::Error::NotFound)?;

        // Check that an account has not already been provisioned on disk.
        if self
            .disk_manager
            .has_zxcrypt_header(&block)
            .await
            .map_err(|_| faccount::Error::Resource)?
        {
            return Err(faccount::Error::FailedPrecondition);
        }

        // Reserve the new account ID and mark it as being provisioned so other tasks don't try to
        // provision the same account or unseal it.
        let provisioning_lock = Arc::new(Mutex::new(()));
        accounts_locked
            .insert(GLOBAL_ACCOUNT_ID, AccountState::Provisioning(provisioning_lock.clone()));

        // Acquire the provisioning lock. That way if we fail to provision, this lock will be
        // automatically released and another task can try to provision again.
        let _ = provisioning_lock.lock().await;

        // Release the lock for all accounts, allowing other tasks to access unrelated accounts.
        drop(accounts_locked);

        // Provision the new account.
        let key = self.key_derivation.derive_key(&password)?;
        let res: Result<AccountId, DiskError> = async {
            let encrypted_block = self.disk_manager.bind_to_encrypted_block(block).await?;
            encrypted_block.format(&key).await?;
            let unsealed_block = encrypted_block.unseal(&key).await?;
            self.disk_manager.format_minfs(&unsealed_block).await?;
            let minfs = self.disk_manager.serve_minfs(unsealed_block).await?;

            // Register the newly provisioned and unsealed account.
            let mut accounts_locked = self.accounts.lock().await;
            accounts_locked.insert(
                GLOBAL_ACCOUNT_ID,
                AccountState::Provisioned(Arc::new(Account::new(key, encrypted_block, minfs))),
            );

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
    async fn lock_account(&self, id: AccountId) {
        let mut accounts_locked = self.accounts.lock().await;
        if let Some(AccountState::Provisioned(account)) = accounts_locked.remove(&id) {
            account.lock().await.expect("lock");
        }
    }
}

#[cfg(test)]
mod test {
    use {
        super::*,
        crate::{
            disk_management::{DiskError, MockMinfs},
            keys::Key,
            prototype::NullKeyDerivation,
        },
        async_trait::async_trait,
        fidl_fuchsia_io::{
            DirectoryMarker, OPEN_FLAG_CREATE, OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE,
        },
        fs_management::ServeError,
        fuchsia_zircon::Status,
        vfs::execution_scope::ExecutionScope,
    };

    /// Mock implementation of [`DiskManager`].
    struct MockDiskManager {
        scope: ExecutionScope,
        // If no partition list is given, partitions() (from the DiskManager trait) will return
        // an error.
        maybe_partitions: Option<Vec<MockPartition>>,
        format_minfs_behavior: Result<(), fn() -> DiskError>,
        serve_minfs_fn: Arc<Mutex<dyn FnMut() -> Result<MockMinfs, DiskError> + Send>>,
    }

    impl Default for MockDiskManager {
        fn default() -> Self {
            let scope = ExecutionScope::build()
                .entry_constructor(vfs::directory::mutable::simple::tree_constructor(
                    |_parent, _name| {
                        Ok(vfs::file::vmo::read_write(
                            vfs::file::vmo::simple_init_vmo_resizable_with_capacity(&[], 100),
                            |_| async {},
                        ))
                    },
                ))
                .new();
            Self {
                scope: scope.clone(),
                maybe_partitions: None,
                format_minfs_behavior: Ok(()),
                serve_minfs_fn: Arc::new(Mutex::new(move || Ok(MockMinfs::simple(scope.clone())))),
            }
        }
    }

    impl Drop for MockDiskManager {
        fn drop(&mut self) {
            self.scope.shutdown();
        }
    }

    #[async_trait]
    impl DiskManager for MockDiskManager {
        type BlockDevice = MockBlockDevice;
        type Partition = MockPartition;
        type EncryptedBlockDevice = MockEncryptedBlockDevice;
        type Minfs = MockMinfs;

        async fn partitions(&self) -> Result<Vec<MockPartition>, DiskError> {
            self.maybe_partitions
                .clone()
                .ok_or_else(|| DiskError::GetBlockInfoFailed(Status::NOT_FOUND))
        }

        async fn has_zxcrypt_header(&self, block_dev: &MockBlockDevice) -> Result<bool, DiskError> {
            match &block_dev.zxcrypt_header_behavior {
                Ok(Match::Any) => Ok(true),
                Ok(Match::None) => Ok(false),
                Err(err_factory) => Err(err_factory()),
            }
        }

        async fn bind_to_encrypted_block(
            &self,
            block_dev: MockBlockDevice,
        ) -> Result<MockEncryptedBlockDevice, DiskError> {
            block_dev.bind_behavior.map_err(|err_factory| err_factory())
        }

        async fn format_minfs(&self, _block_dev: &MockBlockDevice) -> Result<(), DiskError> {
            self.format_minfs_behavior.clone().map_err(|err_factory| err_factory())
        }

        async fn serve_minfs(&self, _block_dev: MockBlockDevice) -> Result<MockMinfs, DiskError> {
            let mut locked_fn = self.serve_minfs_fn.lock().await;
            (*locked_fn)()
        }
    }

    impl MockDiskManager {
        fn new() -> Self {
            Self::default()
        }

        fn with_partition(mut self, partition: MockPartition) -> Self {
            self.maybe_partitions.get_or_insert_with(Vec::new).push(partition);
            self
        }

        fn with_serve_minfs<F>(mut self, serve_minfs: F) -> Self
        where
            F: FnMut() -> Result<MockMinfs, DiskError> + Send + 'static,
        {
            self.serve_minfs_fn = Arc::new(Mutex::new(serve_minfs));
            self
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
        guid_behavior: Result<Match, fn() -> DiskError>,

        // Whether the mock's `has_label` method will match any given label, or produce an error.
        label_behavior: Result<Match, fn() -> DiskError>,

        // BlockDevice representing the partition data.
        block: MockBlockDevice,
    }

    #[async_trait]
    impl Partition for MockPartition {
        type BlockDevice = MockBlockDevice;

        async fn has_guid(&self, _desired_guid: [u8; 16]) -> Result<bool, DiskError> {
            match &self.guid_behavior {
                Ok(Match::Any) => Ok(true),
                Ok(Match::None) => Ok(false),
                Err(err_factory) => Err(err_factory()),
            }
        }

        async fn has_label(&self, _desired_label: &str) -> Result<bool, DiskError> {
            match &self.label_behavior {
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
        zxcrypt_header_behavior: Result<Match, fn() -> DiskError>,
        // Whether or not the block device should succeed in binding zxcrypt
        bind_behavior: Result<MockEncryptedBlockDevice, fn() -> DiskError>,
    }

    /// A mock implementation of [`EncryptedBlockDevice`].
    #[derive(Debug, Clone)]
    struct MockEncryptedBlockDevice {
        // Whether the block encrypted block device can format successfully.
        format_behavior: Result<(), fn() -> DiskError>,
        // Whether the block encrypted block device can be unsealed.
        unseal_behavior: Result<Box<MockBlockDevice>, fn() -> DiskError>,
    }

    #[async_trait]
    impl EncryptedBlockDevice for MockEncryptedBlockDevice {
        type BlockDevice = MockBlockDevice;

        async fn format(&self, _key: &Key) -> Result<(), DiskError> {
            self.format_behavior.clone().map_err(|err_factory| err_factory())
        }

        async fn unseal(&self, _key: &Key) -> Result<MockBlockDevice, DiskError> {
            self.unseal_behavior.clone().map(|b| *b).map_err(|err_factory| err_factory())
        }

        async fn seal(&self) -> Result<(), DiskError> {
            Ok(())
        }
    }

    // Create a partition whose GUID and label match the account partition,
    // and whose block device has a zxcrypt header.
    fn make_formatted_account_partition() -> MockPartition {
        MockPartition {
            guid_behavior: Ok(Match::Any),
            label_behavior: Ok(Match::Any),
            block: MockBlockDevice {
                zxcrypt_header_behavior: Ok(Match::Any),
                bind_behavior: Ok(MockEncryptedBlockDevice {
                    format_behavior: Ok(()),
                    unseal_behavior: Ok(Box::new(MockBlockDevice {
                        zxcrypt_header_behavior: Ok(Match::None),
                        bind_behavior: Err(|| {
                            DiskError::BindZxcryptDriverFailed(Status::NOT_SUPPORTED)
                        }),
                    })),
                }),
            },
        }
    }

    // Create a partition whose GUID and label match the account partition,
    // and whose block device does not have a zxcrypt header.
    fn make_unformatted_account_partition() -> MockPartition {
        MockPartition {
            guid_behavior: Ok(Match::Any),
            label_behavior: Ok(Match::Any),
            block: MockBlockDevice {
                zxcrypt_header_behavior: Ok(Match::None),
                bind_behavior: Ok(MockEncryptedBlockDevice {
                    format_behavior: Ok(()),
                    unseal_behavior: Ok(Box::new(MockBlockDevice {
                        zxcrypt_header_behavior: Ok(Match::None),
                        bind_behavior: Err(|| {
                            DiskError::BindZxcryptDriverFailed(Status::NOT_SUPPORTED)
                        }),
                    })),
                }),
            },
        }
    }

    #[fuchsia::test]
    async fn test_get_account_ids_wrong_guid() {
        let disk_manager = MockDiskManager::new().with_partition(MockPartition {
            guid_behavior: Ok(Match::None),
            label_behavior: Ok(Match::Any),
            block: MockBlockDevice {
                zxcrypt_header_behavior: Ok(Match::Any),
                bind_behavior: Err(|| DiskError::BindZxcryptDriverFailed(Status::NOT_SUPPORTED)),
            },
        });
        let account_manager = AccountManager::new(disk_manager, NullKeyDerivation);
        let account_ids = account_manager.get_account_ids().await.expect("get account ids");
        assert_eq!(account_ids, Vec::<u64>::new());
    }

    #[fuchsia::test]
    async fn test_get_account_ids_wrong_label() {
        let disk_manager = MockDiskManager::new().with_partition(MockPartition {
            guid_behavior: Ok(Match::Any),
            label_behavior: Ok(Match::None),
            block: MockBlockDevice {
                zxcrypt_header_behavior: Ok(Match::Any),
                bind_behavior: Err(|| DiskError::BindZxcryptDriverFailed(Status::NOT_SUPPORTED)),
            },
        });
        let account_manager = AccountManager::new(disk_manager, NullKeyDerivation);
        let account_ids = account_manager.get_account_ids().await.expect("get account ids");
        assert_eq!(account_ids, Vec::<u64>::new());
    }

    #[fuchsia::test]
    async fn test_get_account_ids_no_zxcrypt_header() {
        let disk_manager = MockDiskManager::new().with_partition(MockPartition {
            guid_behavior: Ok(Match::Any),
            label_behavior: Ok(Match::Any),
            block: MockBlockDevice {
                zxcrypt_header_behavior: Ok(Match::None),
                bind_behavior: Err(|| DiskError::BindZxcryptDriverFailed(Status::NOT_SUPPORTED)),
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
                guid_behavior: Ok(Match::Any),
                label_behavior: Ok(Match::None),
                block: MockBlockDevice {
                    zxcrypt_header_behavior: Ok(Match::Any),
                    bind_behavior: Err(|| {
                        DiskError::BindZxcryptDriverFailed(Status::NOT_SUPPORTED)
                    }),
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
            Ok(())
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
            Ok(())
        );
        let (_, server) = fidl::endpoints::create_proxy::<DirectoryMarker>().unwrap();
        assert_eq!(
            client2.get_data_directory(server).await.expect("get_data_directory 2 FIDL"),
            Ok(())
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
            Ok(())
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
            Ok(())
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
            Ok(())
        );
        account_manager.lock_account(GLOBAL_ACCOUNT_ID).await;
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
            guid_behavior: Ok(Match::Any),
            label_behavior: Ok(Match::Any),
            block: MockBlockDevice {
                zxcrypt_header_behavior: Ok(Match::None),
                bind_behavior: Err(|| DiskError::BindZxcryptDriverFailed(Status::UNAVAILABLE)),
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
            guid_behavior: Ok(Match::Any),
            label_behavior: Ok(Match::Any),
            block: MockBlockDevice {
                zxcrypt_header_behavior: Ok(Match::None),
                bind_behavior: Ok(MockEncryptedBlockDevice {
                    format_behavior: Err(|| DiskError::FailedToFormatZxcrypt(Status::IO)),
                    unseal_behavior: Err(|| DiskError::FailedToUnsealZxcrypt(Status::IO)),
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
            guid_behavior: Ok(Match::Any),
            label_behavior: Ok(Match::Any),
            block: MockBlockDevice {
                zxcrypt_header_behavior: Ok(Match::None),
                bind_behavior: Ok(MockEncryptedBlockDevice {
                    format_behavior: Ok(()),
                    unseal_behavior: Err(|| DiskError::FailedToUnsealZxcrypt(Status::IO)),
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
    async fn test_deprecated_provision_new_account_get_data_directory() {
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

        let (account, server_end) = fidl::endpoints::create_proxy().unwrap();
        account_manager
            .get_account(GLOBAL_ACCOUNT_ID, GLOBAL_ACCOUNT_PASSWORD.to_string(), server_end)
            .await
            .expect("get_account");

        let (root_dir, server_end) = fidl::endpoints::create_proxy().unwrap();
        account
            .get_data_directory(server_end)
            .await
            .expect("get_data_directory FIDL")
            .expect("get_data_directory");

        let expected_content = b"some data";
        let file = io_util::directory::open_file(
            &root_dir,
            "test",
            OPEN_FLAG_CREATE | OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
        )
        .await
        .expect("create file");
        let (status, bytes_written) = file.write_at(expected_content, 0).await.expect("file write");
        Status::ok(status).expect("file write failed");
        assert_eq!(bytes_written, expected_content.len() as u64);

        let actual_content = io_util::file::read(&file).await.expect("read file");
        assert_eq!(&actual_content, expected_content);
    }

    #[fuchsia::test]
    async fn test_already_provisioned_get_data_directory() {
        let disk_manager =
            MockDiskManager::new().with_partition(make_formatted_account_partition());
        let account_manager = AccountManager::new(disk_manager, NullKeyDerivation);

        let (account, server_end) = fidl::endpoints::create_proxy().unwrap();
        account_manager
            .get_account(GLOBAL_ACCOUNT_ID, GLOBAL_ACCOUNT_PASSWORD.to_string(), server_end)
            .await
            .expect("get_account");

        let (root_dir, server_end) = fidl::endpoints::create_proxy().unwrap();
        account
            .get_data_directory(server_end)
            .await
            .expect("get_data_directory FIDL")
            .expect("get_data_directory");

        let expected_content = b"some data";
        let file = io_util::directory::open_file(
            &root_dir,
            "test",
            OPEN_FLAG_CREATE | OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
        )
        .await
        .expect("create file");
        let (status, bytes_written) = file.write_at(expected_content, 0).await.expect("file write");
        Status::ok(status).expect("file write failed");
        assert_eq!(bytes_written, expected_content.len() as u64);

        let actual_content = io_util::file::read(&file).await.expect("read file");
        assert_eq!(&actual_content, expected_content);
    }

    #[fuchsia::test]
    async fn test_recover_from_failed_provisioning() {
        let scope = ExecutionScope::new();
        let mut one_time_failure =
            Some(DiskError::MinfsServeError(ServeError::Fidl(fidl::Error::Invalid)));
        let disk_manager = MockDiskManager::new()
            .with_partition(make_unformatted_account_partition())
            .with_serve_minfs(move || {
                if let Some(err) = one_time_failure.take() {
                    Err(err)
                } else {
                    Ok(MockMinfs::simple(scope.clone()))
                }
            });
        let account_manager = AccountManager::new(disk_manager, NullKeyDerivation);

        // Expect a Resource failure.
        assert_eq!(
            account_manager.provision_new_account(GLOBAL_ACCOUNT_PASSWORD.to_string()).await,
            Err(faccount::Error::Resource)
        );

        // Provisioning again should succeed.
        assert_eq!(
            account_manager.provision_new_account(GLOBAL_ACCOUNT_PASSWORD.to_string()).await,
            Ok(GLOBAL_ACCOUNT_ID)
        );
    }

    #[fuchsia::test]
    async fn test_unlock_after_account_locked() {
        let disk_manager =
            MockDiskManager::new().with_partition(make_formatted_account_partition());
        let account_manager = AccountManager::new(disk_manager, NullKeyDerivation);

        let (account, server_end) = fidl::endpoints::create_proxy().unwrap();
        account_manager
            .get_account(GLOBAL_ACCOUNT_ID, GLOBAL_ACCOUNT_PASSWORD.to_string(), server_end)
            .await
            .expect("get_account");

        let (_, server_end) = fidl::endpoints::create_proxy().unwrap();
        account
            .get_data_directory(server_end)
            .await
            .expect("get_data_directory FIDL")
            .expect("get_data_directory");

        account_manager.lock_account(GLOBAL_ACCOUNT_ID).await;

        let (account, server_end) = fidl::endpoints::create_proxy().unwrap();
        account_manager
            .get_account(GLOBAL_ACCOUNT_ID, GLOBAL_ACCOUNT_PASSWORD.to_string(), server_end)
            .await
            .expect("get_account");

        let (_, server_end) = fidl::endpoints::create_proxy().unwrap();
        account
            .get_data_directory(server_end)
            .await
            .expect("get_data_directory FIDL")
            .expect("get_data_directory");
    }
}
