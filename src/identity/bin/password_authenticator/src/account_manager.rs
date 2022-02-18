// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    account::{Account, CheckNewClientResult},
    account_metadata::{AccountMetadata, AccountMetadataStore, AccountMetadataStoreError},
    constants::{ACCOUNT_LABEL, FUCHSIA_DATA_GUID},
    disk_management::{DiskError, DiskManager, EncryptedBlockDevice, Partition},
    keys::{Key, KeyDerivation},
    prototype::{GLOBAL_ACCOUNT_ID, INSECURE_EMPTY_PASSWORD},
    Options,
};
use anyhow::{anyhow, Context, Error};
use fidl::endpoints::ServerEnd;
use fidl_fuchsia_identity_account::{
    self as faccount, AccountManagerRequest, AccountManagerRequestStream, AccountMarker,
};
use futures::{lock::Mutex, prelude::*};
use log::{error, info, warn};
use std::{collections::HashMap, sync::Arc};

/// The minimum length of (non-empty) password that is allowed for new accounts, in bytes.
const MIN_PASSWORD_SIZE: usize = 8;

pub type AccountId = u64;

pub struct AccountManager<DM, AMS>
where
    DM: DiskManager,
    AMS: AccountMetadataStore,
{
    options: Options,
    disk_manager: DM,
    account_metadata_store: Mutex<AMS>,

    accounts: Mutex<HashMap<AccountId, AccountState<DM::EncryptedBlockDevice, DM::Minfs>>>,
}

/// The external state of the account.
enum AccountState<EB, M> {
    Provisioning(Arc<Mutex<()>>),
    Provisioned(Arc<Account<EB, M>>),
}

#[derive(thiserror::Error, Debug)]
enum ProvisionError {
    #[error("Failed to provision disk: {0}")]
    DiskError(#[from] DiskError),
    #[error("Failed to save account metadata: {0}")]
    MetadataError(#[from] AccountMetadataStoreError),
}

impl From<ProvisionError> for faccount::Error {
    fn from(e: ProvisionError) -> Self {
        match e {
            ProvisionError::DiskError(d) => Self::from(d),
            ProvisionError::MetadataError(m) => Self::from(m),
        }
    }
}

impl<DM, AMS> AccountManager<DM, AMS>
where
    DM: DiskManager,
    AMS: AccountMetadataStore,
{
    pub fn new(options: Options, disk_manager: DM, account_metadata_store: AMS) -> Self {
        Self {
            options,
            disk_manager,
            account_metadata_store: Mutex::new(account_metadata_store),
            accounts: Mutex::new(HashMap::new()),
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
                responder.send(&account_ids).context("sending GetAccountIds response")?;
            }
            AccountManagerRequest::DeprecatedGetAccount { id, password, account, responder } => {
                let mut resp = self.get_account(id, &password, account).await;
                responder.send(&mut resp).context("sending DeprecatedGetAccount response")?;
            }
            AccountManagerRequest::DeprecatedProvisionNewAccount {
                password,
                metadata,
                account,
                responder,
            } => {
                let mut resp = self
                    .provision_new_account(&metadata, &password)
                    .and_then(|account_id| self.get_account(account_id, &password, account))
                    .await;
                responder
                    .send(&mut resp)
                    .context("sending DeprecatedProvisionNewAccount response")?;
            }
            AccountManagerRequest::GetAccountAuthStates { scenario: _, responder } => {
                let mut resp = Err(faccount::Error::UnsupportedOperation);
                responder.send(&mut resp).context("sending GetAccountAuthStates response")?;
            }
            AccountManagerRequest::GetAccountMetadata { id, responder } => {
                let mut resp = self.get_account_metadata(id).await;
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
            AccountManagerRequest::RemoveAccount { id, force, responder } => {
                let mut resp = self.remove_account(id, force).await;
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

    /// Return the list of account IDs that have accounts by querying the account metadata store.
    async fn get_account_ids(&self) -> Result<Vec<u64>, Error> {
        let account_ids = {
            let ams_locked = self.account_metadata_store.lock().await;
            ams_locked.account_ids().await?
        };

        Ok(account_ids)
    }

    /// Return the metadata for the requested account.
    async fn get_account_metadata(
        &self,
        id: u64,
    ) -> Result<faccount::AccountMetadata, faccount::Error> {
        // Load metadata from account metadata store.
        let ams_metadata = {
            let ams_locked = self.account_metadata_store.lock().await;
            ams_locked.load(&id).await?.ok_or_else(|| {
                warn!("get_account_metadata: ID {} not found in account metadata store", id);
                faccount::Error::NotFound
            })?
        };

        Ok(faccount::AccountMetadata {
            name: Some(ams_metadata.name().to_string()),
            ..faccount::AccountMetadata::EMPTY
        })
    }

    /// Authenticates an account and serves the Account FIDL protocol over the `account` channel.
    /// The `id` is verified to be present.
    /// The only id that we accept is GLOBAL_ACCOUNT_ID.
    async fn get_account(
        &self,
        id: AccountId,
        password: &str,
        server_end: ServerEnd<AccountMarker>,
    ) -> Result<(), faccount::Error> {
        // Load metadata from account metadata store.
        // If account metadata missing, there's no way to get the account, return NotFound.
        let account_metadata = {
            let ams_locked = self.account_metadata_store.lock().await;
            ams_locked.load(&id).await?.ok_or_else(|| {
                warn!(
                    "get_account: requested account ID {} not found in account metadata store",
                    id
                );
                faccount::Error::NotFound
            })?
        };

        // Since we directly use a singleton partition below, and that partition is associated with
        // GLOBAL_ACCOUNT_ID, we should make sure that we don't allow any other account IDs to be
        // used here.
        if id != GLOBAL_ACCOUNT_ID {
            // We prefer Internal to NotFound here because we still return the account ID in
            // get_account_ids, and it's more that account_manager's policy layer is forbidding its
            // use than that we failed to find the account ID.
            warn!("get_account: rejecting unexpected account ID {} in account metadata store", id);
            return Err(faccount::Error::Internal);
        }

        // Verify that the command line options allow this type of metadata.
        //
        // TODO(zarvox, jsankey): Once account deletion is available it probably makes sense to
        // automatically delete an account that is not allowed by the current command line options
        // (for example an account that was created with an empty password once allow_null=false)
        // but for now we simply log a warning and return an error.
        if !account_metadata.allowed_by_options(&self.options) {
            warn!("get_account: account metadata is not allowed by current command line options");
            return Err(faccount::Error::UnsupportedOperation);
        }

        // If account metadata present, derive key (using the appropriate scheme for the
        // AccountMetadata instance).
        let key = account_metadata.derive_key(&password).await?;

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
                        warn!(
                            "get_account: account ID {} is already unsealed, but an incorrect \
                              password was given",
                            id
                        );
                        return Err(faccount::Error::FailedAuthentication);
                    }
                }
            }
            Some(AccountState::Provisioning(_)) => {
                // This account is in the process of being provisioned, treat it like it doesn't
                // exist.
                warn!("get_account: account ID {} is still provisioning", id);
                return Err(faccount::Error::NotFound);
            }
            None => {
                // There is no account associated with the ID in memory. Check if the account can
                // be unsealed from disk.
                let account = self.unseal_account(id, &key).await.map_err(|err| {
                    warn!("get_account: failed to unseal account ID {}: {:?}", id, err);
                    err
                })?;
                info!("get_account: unsealed account ID {}", id);
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
        info!("get_account for account ID {} successful", id);
        Ok(())
    }

    async fn unseal_account(
        &self,
        id: AccountId,
        key: &Key,
    ) -> Result<Arc<Account<DM::EncryptedBlockDevice, DM::Minfs>>, faccount::Error> {
        let account_ids = self.get_account_ids().await.map_err(|err| {
            warn!("unseal_account: couldn't list account IDs: {}", err);
            faccount::Error::NotFound
        })?;
        if account_ids.into_iter().find(|i| *i == id).is_none() {
            return Err(faccount::Error::NotFound);
        }
        let block_device =
            self.find_account_partition().await.ok_or(faccount::Error::NotFound).map_err(
                |err| {
                    error!("unseal_account: couldn't find account partition");
                    err
                },
            )?;
        let encrypted_block =
            self.disk_manager.bind_to_encrypted_block(block_device).await.map_err(|err| {
                error!(
                    "unseal_account: couldn't bind zxcrypt driver to encrypted block device: {}",
                    err
                );
                err
            })?;
        let block_device = match encrypted_block.unseal(&key).await {
            Ok(block_device) => block_device,
            Err(DiskError::FailedToUnsealZxcrypt(err)) => {
                info!("unseal_account: failed to unseal zxcrypt (wrong password?): {}", err);
                return Err(faccount::Error::FailedAuthentication);
            }
            Err(err) => {
                warn!("unseal_account: failed to unseal zxcrypt: {}", err);
                return Err(err.into());
            }
        };
        let minfs = self.disk_manager.serve_minfs(block_device).await.map_err(|err| {
            error!("unseal_account: couldn't serve minfs: {}", err);
            err
        })?;
        Ok(Arc::new(Account::new(key.clone(), encrypted_block, minfs)))
    }

    async fn provision_new_account(
        &self,
        account_metadata: &faccount::AccountMetadata,
        password: &str,
    ) -> Result<AccountId, faccount::Error> {
        // Clients must provide a name in the account metadata.
        let name = account_metadata.name.as_ref().ok_or_else(|| {
            error!("provision_new_account: refusing to provision account with no name in metadata");
            faccount::Error::InvalidRequest
        })?;

        let metadata = if password == INSECURE_EMPTY_PASSWORD {
            if !self.options.allow_null {
                // Empty passwords are only supported when the allow_null option is true.
                warn!(
                    "provision_new_account: refusing to provision account with empty password when \
                    allow_null=false");
                return Err(faccount::Error::InvalidRequest);
            }
            AccountMetadata::new_null(name.to_string())
        } else {
            if !(self.options.allow_scrypt || self.options.allow_pinweaver) {
                // Non-empty passwords are only supported when scrypt or pinweaver is allowed.
                warn!(
                    "provision_new_account: refusing to provision account with non-empty password \
                    when allow_scrypt=false and allow_pinweaver=false"
                );
                return Err(faccount::Error::InvalidRequest);
            } else if password.len() < MIN_PASSWORD_SIZE {
                // Non-empty passwords must always be at least the minimum length.
                warn!(
                    "provision_new_account: refusing to provision account with password of \
                      length {}",
                    password.len()
                );
                return Err(faccount::Error::InvalidRequest);
            }
            AccountMetadata::new_scrypt(name.to_string())
        };

        // For now, we only contemplate one account ID
        let account_id = GLOBAL_ACCOUNT_ID;

        info!("provision_new_account: attempting to provision new account");

        // Acquire the lock for all accounts.
        let mut accounts_locked = self.accounts.lock().await;

        // Check if the global account is already provisioned or being provisioned.
        if let Some(state) = accounts_locked.get(&GLOBAL_ACCOUNT_ID) {
            match state {
                AccountState::Provisioned(_) => {
                    warn!("provision_new_account: global account is already provisioned");
                    return Err(faccount::Error::FailedPrecondition);
                }
                AccountState::Provisioning(lock) => {
                    // The account was being provisioned at some point. Try to acquire the lock.
                    if lock.try_lock().is_none() {
                        // The lock is locked, someone else is provisioning the account.
                        warn!("provision_new_account: global account is already being provisioned");
                        return Err(faccount::Error::FailedPrecondition);
                    }
                    // The lock was unlocked, meaning the original provisioner failed.
                }
            }
        }

        let block = self.find_account_partition().await.ok_or(faccount::Error::NotFound).map_err(
            |err| {
                error!("provision_new_account: couldn't find account partition to provision");
                err
            },
        )?;

        // Reserve the new account ID and mark it as being provisioned so other tasks don't try to
        // provision the same account or unseal it.
        let provisioning_lock = Arc::new(Mutex::new(()));
        accounts_locked.insert(account_id, AccountState::Provisioning(provisioning_lock.clone()));

        // Acquire the provisioning lock. That way if we fail to provision, this lock will be
        // automatically released and another task can try to provision again.
        let _ = provisioning_lock.lock().await;

        // Release the lock for all accounts, allowing other tasks to access unrelated accounts.
        drop(accounts_locked);

        // Provision the new account.
        let key = metadata.derive_key(&password).await.map_err(|err| {
            error!("provision_new_account: key derivation failed during provisioning: {}", err);
            err
        })?;
        let res: Result<AccountId, ProvisionError> = async {
            let encrypted_block =
                self.disk_manager.bind_to_encrypted_block(block).await.map_err(|err| {
                    error!(
                        "provision_new_account: couldn't bind zxcrypt driver to encrypted \
                           block device: {}",
                        err
                    );
                    err
                })?;
            encrypted_block.format(&key).await.map_err(|err| {
                error!("provision_new_account: couldn't format encrypted block device: {}", err);
                err
            })?;
            let unsealed_block = encrypted_block.unseal(&key).await.map_err(|err| {
                error!("provision_new_account: couldn't unseal encrypted block device: {}", err);
                err
            })?;
            self.disk_manager.format_minfs(&unsealed_block).await.map_err(|err| {
                error!(
                    "provision_new_account: couldn't format minfs on inner block device: {}",
                    err
                );
                err
            })?;
            let minfs = self.disk_manager.serve_minfs(unsealed_block).await.map_err(|err| {
                error!(
                    "provision_new_account: couldn't serve minfs on inner unsealed block \
                       device: {}",
                    err
                );
                err
            })?;

            // Save the new account metadata.  Drop the lock once done.
            let mut ams_locked = self.account_metadata_store.lock().await;
            ams_locked.save(&account_id, &metadata).await.map_err(|err| {
                error!(
                    "provision_new_account: couldn't save account metadata for account ID {}",
                    &account_id
                );
                err
            })?;
            drop(ams_locked);

            // Register the newly provisioned and unsealed account.
            let mut accounts_locked = self.accounts.lock().await;
            accounts_locked.insert(
                account_id,
                AccountState::Provisioned(Arc::new(Account::new(key, encrypted_block, minfs))),
            );

            Ok(account_id)
        }
        .await;

        let id = res?;
        info!("provision_new_account: successfully provisioned new account {}", id);
        Ok(id)
    }

    async fn remove_account(&self, id: AccountId, _force: bool) -> Result<(), faccount::Error> {
        // To remove an account, we need to:
        // 1. lock the account, if it was unlocked
        // 2. remove the metadata from the account metadata store
        // 3. (best effort) shred the backing volume
        // 4. mark the account as fully removed (and thus eligible to be provisioned again)
        //
        // We hold the accounts lock throughout, since it will exclude concurrent access to both
        // the accounts hashmap and our direct access to the zxcrypt partition.

        // step 1: lock the account if it was unlocked.
        // If we can't lock the account, fail the request.  If it wasn't in memory, then by
        // construction it is not unlocked, and it's safe to proceed.
        let mut accounts_locked = self.accounts.lock().await;
        match accounts_locked.get(&id) {
            Some(AccountState::Provisioning(_)) => {
                warn!(
                    "remove_account: can't remove account ID {} still in Provisioning state",
                    &id
                );
                Err(faccount::Error::FailedPrecondition)?
            }
            Some(AccountState::Provisioned(account)) => {
                // Try to lock the Account.  (If it wasn't in
                // self.accounts, we're not serving any requests for it.)
                let res = account.clone().lock().await;
                res.map_err(|err| {
                    error!("remove_account: could not lock account ID {}: {:?}", id, err);
                    faccount::Error::Internal
                })?;
            }
            None => {
                // The account had no previous state, so we don't need to lock it.
            }
        }
        accounts_locked.remove(&id);

        // step 2: remove the metadata from the account metadata store
        // By now we have guaranteed that the Account is either explicitly locked, or implicitly
        // was never unlocked.  We still hold the accounts lock, preventing that state from
        // changing.  It is thus safe to destroy the account metadata.

        // Remove the account from the account metadata store.  Once this completes, the account
        // removal is guaranteed to have succeeded -- everything after that is best-effort cleanup.
        let mut ams_locked = self.account_metadata_store.lock().await;
        ams_locked.remove(&id).await.map_err(|err| {
            error!("remove_account: couldn't remove account metadata for account ID {}", &id);
            err
        })?;
        drop(ams_locked);

        // step 3: (best effort) shred the backing volume
        // Try to shred the backing volume, waiting for completion, but ignoring errors.
        // The metadata removal is sufficient to make the volume no longer unsealable, so we
        // should not return a failure if we make it to here.  We should block though, because if
        // we return before we've shredded the volume, a client could race with itself.
        match self.find_account_partition().await {
            Some(block_device) => {
                match self.disk_manager.bind_to_encrypted_block(block_device).await {
                    Ok(encrypted_block) => {
                        let shred_res = encrypted_block.shred().await.map_err(|err| {
                            warn!(
                                "remove_account: couldn't shred encrypted block device: {} \
                                    (ignored)",
                                err
                            );
                        });
                        // Ignore the result.
                        drop(shred_res);
                    }
                    Err(err) => {
                        // Ignore the failure.
                        warn!(
                            "remove_account: couldn't bind zxcrypt driver to encrypted block \
                            device: {} (ignored)",
                            err
                        );
                    }
                }
            }
            None => {
                warn!("remove_account: couldn't find account partition, carrying on anyway");
            }
        }

        // step 4: mark the account as fully removed (and thus eligible to be provisioned again)
        // Mark that we've finished deprovisioning this user by releasing the self.accounts lock
        // and returning success.
        drop(accounts_locked);
        Ok(())
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
            account_metadata::{
                test::{TEST_NAME, TEST_SCRYPT_KEY, TEST_SCRYPT_METADATA, TEST_SCRYPT_PASSWORD},
                AccountMetadata, AccountMetadataStoreError,
            },
            disk_management::{DiskError, MockMinfs},
            keys::Key,
            prototype::INSECURE_EMPTY_KEY,
        },
        async_trait::async_trait,
        fidl_fuchsia_io::{
            DirectoryMarker, OPEN_FLAG_CREATE, OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE,
        },
        fs_management::ServeError,
        fuchsia_zircon::Status,
        lazy_static::lazy_static,
        vfs::execution_scope::ExecutionScope,
    };

    // By default allow any form of password and encryption.
    const DEFAULT_OPTIONS: Options =
        Options { allow_null: true, allow_scrypt: true, allow_pinweaver: true };
    // Define more restrictive options to verify exclusions are implemented correctly.
    const NULL_ONLY_OPTIONS: Options =
        Options { allow_null: true, allow_scrypt: false, allow_pinweaver: false };
    const SCRYPT_ONLY_OPTIONS: Options =
        Options { allow_null: false, allow_scrypt: true, allow_pinweaver: false };

    // An account ID that should not exist.
    const UNSUPPORTED_ACCOUNT_ID: u64 = 42;

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

    #[derive(Debug, Clone)]
    enum UnsealBehavior {
        AcceptAnyKey(Box<MockBlockDevice>),
        AcceptExactKeys((Vec<Key>, Box<MockBlockDevice>)),
        RejectWithError(fn() -> DiskError),
    }

    /// A mock implementation of [`EncryptedBlockDevice`].
    #[derive(Debug, Clone)]
    struct MockEncryptedBlockDevice {
        // Whether the block encrypted block device can format successfully.
        format_behavior: Result<(), fn() -> DiskError>,
        // What behavior the encrypted block device should have when unseal is attempted.
        unseal_behavior: UnsealBehavior,
        // Whether the block encrypted block device can be shredded successfully
        shred_behavior: Result<(), fn() -> DiskError>,
    }

    #[async_trait]
    impl EncryptedBlockDevice for MockEncryptedBlockDevice {
        type BlockDevice = MockBlockDevice;

        async fn format(&self, _key: &Key) -> Result<(), DiskError> {
            self.format_behavior.clone().map_err(|err_factory| err_factory())
        }

        async fn unseal(&self, key: &Key) -> Result<MockBlockDevice, DiskError> {
            match &self.unseal_behavior {
                UnsealBehavior::AcceptAnyKey(b) => Ok(*b.clone()),
                UnsealBehavior::AcceptExactKeys((keys, b)) => {
                    if keys.contains(&key) {
                        Ok(*b.clone())
                    } else {
                        Err(DiskError::FailedToUnsealZxcrypt(Status::ACCESS_DENIED))
                    }
                }
                UnsealBehavior::RejectWithError(err_factory) => Err(err_factory()),
            }
        }

        async fn seal(&self) -> Result<(), DiskError> {
            Ok(())
        }

        async fn shred(&self) -> Result<(), DiskError> {
            self.shred_behavior.clone().map_err(|err_factory| err_factory())
        }
    }

    #[derive(Debug, Clone)]
    struct MemoryAccountMetadataStore {
        accounts: std::collections::HashMap<AccountId, AccountMetadata>,
    }

    impl MemoryAccountMetadataStore {
        fn new() -> MemoryAccountMetadataStore {
            MemoryAccountMetadataStore { accounts: std::collections::HashMap::new() }
        }

        fn with_null_keyed_account(mut self, account_id: &AccountId) -> Self {
            let metadata = AccountMetadata::new_null(TEST_NAME.into());
            self.accounts.insert(*account_id, metadata);
            self
        }

        fn with_password_account(mut self, account_id: &AccountId) -> Self {
            let metadata = TEST_SCRYPT_METADATA.clone();
            self.accounts.insert(*account_id, metadata);
            self
        }
    }

    #[async_trait]
    impl AccountMetadataStore for MemoryAccountMetadataStore {
        async fn account_ids(&self) -> Result<Vec<AccountId>, AccountMetadataStoreError> {
            Ok(self.accounts.keys().map(|id| *id).collect())
        }

        async fn save(
            &mut self,
            account_id: &AccountId,
            metadata: &AccountMetadata,
        ) -> Result<(), AccountMetadataStoreError> {
            self.accounts.insert(*account_id, metadata.clone());
            Ok(())
        }

        async fn load(
            &self,
            account_id: &AccountId,
        ) -> Result<Option<AccountMetadata>, AccountMetadataStoreError> {
            Ok(self.accounts.get(account_id).map(|meta| meta.clone()))
        }

        async fn remove(
            &mut self,
            account_id: &AccountId,
        ) -> Result<(), AccountMetadataStoreError> {
            self.accounts.remove(account_id);
            Ok(())
        }
    }

    // Create a partition whose GUID and label match the account partition,
    // whose block device has a zxcrypt header, and which can be unsealed with any arbitrary key
    fn make_formatted_account_partition_any_key() -> MockPartition {
        MockPartition {
            guid_behavior: Ok(Match::Any),
            label_behavior: Ok(Match::Any),
            block: MockBlockDevice {
                zxcrypt_header_behavior: Ok(Match::Any),
                bind_behavior: Ok(MockEncryptedBlockDevice {
                    format_behavior: Ok(()),
                    unseal_behavior: UnsealBehavior::AcceptAnyKey(Box::new(MockBlockDevice {
                        zxcrypt_header_behavior: Ok(Match::None),
                        bind_behavior: Err(|| {
                            DiskError::BindZxcryptDriverFailed(Status::NOT_SUPPORTED)
                        }),
                    })),
                    shred_behavior: Ok(()),
                }),
            },
        }
    }

    // Create a partition whose GUID and label match the account partition,
    // and whose block device has a zxcrypt header.
    fn make_formatted_account_partition(accepted_key: Key) -> MockPartition {
        let acceptable_keys = vec![accepted_key];
        MockPartition {
            guid_behavior: Ok(Match::Any),
            label_behavior: Ok(Match::Any),
            block: MockBlockDevice {
                zxcrypt_header_behavior: Ok(Match::Any),
                bind_behavior: Ok(MockEncryptedBlockDevice {
                    format_behavior: Ok(()),
                    unseal_behavior: UnsealBehavior::AcceptExactKeys((
                        acceptable_keys,
                        Box::new(MockBlockDevice {
                            zxcrypt_header_behavior: Ok(Match::None),
                            bind_behavior: Err(|| {
                                DiskError::BindZxcryptDriverFailed(Status::NOT_SUPPORTED)
                            }),
                        }),
                    )),
                    shred_behavior: Ok(()),
                }),
            },
        }
    }

    // Create a partition whose GUID and label match the account partition,
    // and whose block device has a zxcrypt header, and which, if told to shred, will
    // fail with an IO error
    fn make_formatted_account_partition_fail_shred(accepted_key: Key) -> MockPartition {
        let acceptable_keys = vec![accepted_key];
        MockPartition {
            guid_behavior: Ok(Match::Any),
            label_behavior: Ok(Match::Any),
            block: MockBlockDevice {
                zxcrypt_header_behavior: Ok(Match::Any),
                bind_behavior: Ok(MockEncryptedBlockDevice {
                    format_behavior: Ok(()),
                    unseal_behavior: UnsealBehavior::AcceptExactKeys((
                        acceptable_keys,
                        Box::new(MockBlockDevice {
                            zxcrypt_header_behavior: Ok(Match::None),
                            bind_behavior: Err(|| {
                                DiskError::BindZxcryptDriverFailed(Status::NOT_SUPPORTED)
                            }),
                        }),
                    )),
                    shred_behavior: Err(|| DiskError::FailedToShredZxcrypt(Status::IO)),
                }),
            },
        }
    }

    // Create a partition whose GUID and label match the account partition,
    // and whose block device does not have a zxcrypt header.  Expect the client to
    // format and unseal it, at which point we will accept any key and expose an empty
    // inner block device.
    fn make_unformatted_account_partition() -> MockPartition {
        MockPartition {
            guid_behavior: Ok(Match::Any),
            label_behavior: Ok(Match::Any),
            block: MockBlockDevice {
                zxcrypt_header_behavior: Ok(Match::None),
                bind_behavior: Ok(MockEncryptedBlockDevice {
                    format_behavior: Ok(()),
                    unseal_behavior: UnsealBehavior::AcceptAnyKey(Box::new(MockBlockDevice {
                        zxcrypt_header_behavior: Ok(Match::None),
                        bind_behavior: Err(|| {
                            DiskError::BindZxcryptDriverFailed(Status::NOT_SUPPORTED)
                        }),
                    })),
                    shred_behavior: Ok(()),
                }),
            },
        }
    }

    fn make_test_metadata() -> faccount::AccountMetadata {
        faccount::AccountMetadata {
            name: Some("Test Display Name".into()),
            ..faccount::AccountMetadata::EMPTY
        }
    }
    lazy_static! {
        pub static ref TEST_FACCOUNT_METADATA: faccount::AccountMetadata = make_test_metadata();
    }

    #[fuchsia::test]
    async fn test_get_account_ids_empty() {
        let disk_manager = MockDiskManager::new().with_partition(MockPartition {
            guid_behavior: Ok(Match::None),
            label_behavior: Ok(Match::Any),
            block: MockBlockDevice {
                zxcrypt_header_behavior: Ok(Match::Any),
                bind_behavior: Err(|| DiskError::BindZxcryptDriverFailed(Status::NOT_SUPPORTED)),
            },
        });
        let account_metadata_store = MemoryAccountMetadataStore::new();
        let account_manager =
            AccountManager::new(DEFAULT_OPTIONS, disk_manager, account_metadata_store);
        let account_ids = account_manager.get_account_ids().await.expect("get account ids");
        assert_eq!(account_ids, Vec::<u64>::new());
    }

    #[fuchsia::test]
    async fn test_get_account_ids_found() {
        let disk_manager =
            MockDiskManager::new().with_partition(make_formatted_account_partition_any_key());
        let account_metadata_store =
            MemoryAccountMetadataStore::new().with_password_account(&GLOBAL_ACCOUNT_ID);
        let account_manager =
            AccountManager::new(DEFAULT_OPTIONS, disk_manager, account_metadata_store);
        let account_ids = account_manager.get_account_ids().await.expect("get account ids");
        assert_eq!(account_ids, vec![GLOBAL_ACCOUNT_ID]);
    }

    #[fuchsia::test]
    async fn test_get_account_metadata_found() {
        let disk_manager =
            MockDiskManager::new().with_partition(make_formatted_account_partition_any_key());
        let account_metadata_store =
            MemoryAccountMetadataStore::new().with_password_account(&GLOBAL_ACCOUNT_ID);
        let account_manager =
            AccountManager::new(DEFAULT_OPTIONS, disk_manager, account_metadata_store);
        let account_metadata =
            account_manager.get_account_metadata(GLOBAL_ACCOUNT_ID).await.unwrap();
        assert_eq!(
            account_metadata,
            faccount::AccountMetadata {
                name: Some(TEST_NAME.to_string()),
                ..faccount::AccountMetadata::EMPTY
            }
        );
    }

    #[fuchsia::test]
    async fn test_get_account_metadata_not_found() {
        let disk_manager =
            MockDiskManager::new().with_partition(make_formatted_account_partition_any_key());
        let account_metadata_store =
            MemoryAccountMetadataStore::new().with_password_account(&GLOBAL_ACCOUNT_ID);
        let account_manager =
            AccountManager::new(DEFAULT_OPTIONS, disk_manager, account_metadata_store);
        let err = account_manager.get_account_metadata(UNSUPPORTED_ACCOUNT_ID).await.unwrap_err();
        assert_eq!(err, faccount::Error::NotFound);
    }

    #[fuchsia::test]
    async fn test_get_account_no_accounts() {
        let account_manager = AccountManager::new(
            DEFAULT_OPTIONS,
            MockDiskManager::new(),
            MemoryAccountMetadataStore::new(),
        );
        let (_, server) = fidl::endpoints::create_endpoints::<AccountMarker>().unwrap();
        assert_eq!(
            account_manager.get_account(GLOBAL_ACCOUNT_ID, TEST_SCRYPT_PASSWORD, server).await,
            Err(faccount::Error::NotFound)
        );
    }

    #[fuchsia::test]
    async fn test_get_account_unsupported_id() {
        // All account IDs except for 1 are rejected with Internal (but only if the account
        // metadata is actually present for such a non-1 account).
        let disk_manager =
            MockDiskManager::new().with_partition(make_formatted_account_partition_any_key());
        let account_metadata_store =
            MemoryAccountMetadataStore::new().with_password_account(&UNSUPPORTED_ACCOUNT_ID);
        let account_manager =
            AccountManager::new(DEFAULT_OPTIONS, disk_manager, account_metadata_store);
        let (_, server) = fidl::endpoints::create_endpoints::<AccountMarker>().unwrap();
        assert_eq!(
            account_manager.get_account(UNSUPPORTED_ACCOUNT_ID, TEST_SCRYPT_PASSWORD, server).await,
            Err(faccount::Error::Internal)
        );
    }

    #[fuchsia::test]
    async fn test_get_account_found_wrong_password() {
        const BAD_PASSWORD: &str = "passwd";
        let disk_manager = MockDiskManager::new()
            .with_partition(make_formatted_account_partition(TEST_SCRYPT_KEY));
        let account_metadata_store =
            MemoryAccountMetadataStore::new().with_password_account(&GLOBAL_ACCOUNT_ID);
        let account_manager =
            AccountManager::new(DEFAULT_OPTIONS, disk_manager, account_metadata_store);
        let (_, server) = fidl::endpoints::create_endpoints::<AccountMarker>().unwrap();
        assert_eq!(
            account_manager.get_account(GLOBAL_ACCOUNT_ID, BAD_PASSWORD, server).await,
            Err(faccount::Error::FailedAuthentication)
        );
    }

    #[fuchsia::test]
    async fn test_get_account_found_no_password_allowed() {
        let disk_manager = MockDiskManager::new()
            .with_partition(make_formatted_account_partition(INSECURE_EMPTY_KEY));
        let account_metadata_store =
            MemoryAccountMetadataStore::new().with_null_keyed_account(&GLOBAL_ACCOUNT_ID);
        let account_manager =
            AccountManager::new(NULL_ONLY_OPTIONS, disk_manager, account_metadata_store);
        let (client, server) = fidl::endpoints::create_proxy::<AccountMarker>().unwrap();
        account_manager
            .get_account(GLOBAL_ACCOUNT_ID, INSECURE_EMPTY_PASSWORD, server)
            .await
            .expect("get account");
        let (_, server) = fidl::endpoints::create_proxy::<DirectoryMarker>().unwrap();
        assert_eq!(
            client.get_data_directory(server).await.expect("get_data_directory FIDL"),
            Ok(())
        );
    }

    #[fuchsia::test]
    async fn test_get_account_found_no_password_not_allowed() {
        let disk_manager = MockDiskManager::new()
            .with_partition(make_formatted_account_partition(INSECURE_EMPTY_KEY));
        let account_metadata_store =
            MemoryAccountMetadataStore::new().with_null_keyed_account(&GLOBAL_ACCOUNT_ID);
        let account_manager =
            AccountManager::new(SCRYPT_ONLY_OPTIONS, disk_manager, account_metadata_store);
        let (_, server) = fidl::endpoints::create_proxy::<AccountMarker>().unwrap();
        assert_eq!(
            account_manager.get_account(GLOBAL_ACCOUNT_ID, INSECURE_EMPTY_PASSWORD, server).await,
            Err(faccount::Error::UnsupportedOperation)
        );
    }

    #[fuchsia::test]
    async fn test_get_account_found_correct_password_allowed() {
        let disk_manager = MockDiskManager::new()
            .with_partition(make_formatted_account_partition(TEST_SCRYPT_KEY));
        let account_metadata_store =
            MemoryAccountMetadataStore::new().with_password_account(&GLOBAL_ACCOUNT_ID);
        let account_manager =
            AccountManager::new(SCRYPT_ONLY_OPTIONS, disk_manager, account_metadata_store);
        let (client, server) = fidl::endpoints::create_proxy::<AccountMarker>().unwrap();
        account_manager
            .get_account(GLOBAL_ACCOUNT_ID, TEST_SCRYPT_PASSWORD, server)
            .await
            .expect("get account");
        let (_, server) = fidl::endpoints::create_proxy::<DirectoryMarker>().unwrap();
        assert_eq!(
            client.get_data_directory(server).await.expect("get_data_directory FIDL"),
            Ok(())
        );
    }

    #[fuchsia::test]
    async fn test_get_account_found_correct_password_not_allowed() {
        let disk_manager = MockDiskManager::new()
            .with_partition(make_formatted_account_partition(TEST_SCRYPT_KEY));
        let account_metadata_store =
            MemoryAccountMetadataStore::new().with_password_account(&GLOBAL_ACCOUNT_ID);
        let account_manager =
            AccountManager::new(NULL_ONLY_OPTIONS, disk_manager, account_metadata_store);
        let (_, server) = fidl::endpoints::create_proxy::<AccountMarker>().unwrap();
        assert_eq!(
            account_manager.get_account(GLOBAL_ACCOUNT_ID, TEST_SCRYPT_PASSWORD, server).await,
            Err(faccount::Error::UnsupportedOperation)
        );
    }

    #[fuchsia::test]
    async fn test_multiple_get_account_channels_concurrent() {
        let disk_manager = MockDiskManager::new()
            .with_partition(make_formatted_account_partition(TEST_SCRYPT_KEY));
        let account_metadata_store =
            MemoryAccountMetadataStore::new().with_password_account(&GLOBAL_ACCOUNT_ID);
        let account_manager =
            AccountManager::new(DEFAULT_OPTIONS, disk_manager, account_metadata_store);
        let (client1, server) = fidl::endpoints::create_proxy::<AccountMarker>().unwrap();
        account_manager
            .get_account(GLOBAL_ACCOUNT_ID, TEST_SCRYPT_PASSWORD, server)
            .await
            .expect("get account 1");
        let (client2, server) = fidl::endpoints::create_proxy::<AccountMarker>().unwrap();
        account_manager
            .get_account(GLOBAL_ACCOUNT_ID, TEST_SCRYPT_PASSWORD, server)
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
        let disk_manager = MockDiskManager::new()
            .with_partition(make_formatted_account_partition(TEST_SCRYPT_KEY));
        let account_metadata_store =
            MemoryAccountMetadataStore::new().with_password_account(&GLOBAL_ACCOUNT_ID);
        let account_manager =
            AccountManager::new(DEFAULT_OPTIONS, disk_manager, account_metadata_store);
        let (client, server) = fidl::endpoints::create_proxy::<AccountMarker>().unwrap();
        account_manager
            .get_account(GLOBAL_ACCOUNT_ID, TEST_SCRYPT_PASSWORD, server)
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
            .get_account(GLOBAL_ACCOUNT_ID, TEST_SCRYPT_PASSWORD, server)
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
        let disk_manager = MockDiskManager::new()
            .with_partition(make_formatted_account_partition(TEST_SCRYPT_KEY));
        let account_metadata_store =
            MemoryAccountMetadataStore::new().with_password_account(&GLOBAL_ACCOUNT_ID);
        let account_manager =
            AccountManager::new(DEFAULT_OPTIONS, disk_manager, account_metadata_store);
        let (client, server) = fidl::endpoints::create_proxy::<AccountMarker>().unwrap();
        account_manager
            .get_account(GLOBAL_ACCOUNT_ID, TEST_SCRYPT_PASSWORD, server)
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
    async fn test_deprecated_provision_new_account_requires_name_in_metadata() {
        let disk_manager = MockDiskManager::new()
            .with_partition(make_formatted_account_partition(TEST_SCRYPT_KEY));
        let account_manager =
            AccountManager::new(DEFAULT_OPTIONS, disk_manager, MemoryAccountMetadataStore::new());
        let metadata = faccount::AccountMetadata { name: None, ..faccount::AccountMetadata::EMPTY };
        assert_eq!(
            account_manager.provision_new_account(&metadata, TEST_SCRYPT_PASSWORD).await,
            Err(faccount::Error::InvalidRequest)
        );
    }

    #[fuchsia::test]
    async fn test_deprecated_provision_new_account_on_formatted_block() {
        let disk_manager =
            MockDiskManager::new().with_partition(make_formatted_account_partition_any_key());
        let account_manager =
            AccountManager::new(DEFAULT_OPTIONS, disk_manager, MemoryAccountMetadataStore::new());
        assert_eq!(
            account_manager
                .provision_new_account(&TEST_FACCOUNT_METADATA, TEST_SCRYPT_PASSWORD)
                .await
                .expect("provision account"),
            GLOBAL_ACCOUNT_ID
        );
    }

    #[fuchsia::test]
    async fn test_deprecated_provision_new_account_on_unformatted_block() {
        let disk_manager =
            MockDiskManager::new().with_partition(make_unformatted_account_partition());
        let account_manager =
            AccountManager::new(DEFAULT_OPTIONS, disk_manager, MemoryAccountMetadataStore::new());
        assert_eq!(
            account_manager
                .provision_new_account(&TEST_FACCOUNT_METADATA, TEST_SCRYPT_PASSWORD)
                .await
                .expect("provision account"),
            GLOBAL_ACCOUNT_ID
        );
    }

    #[fuchsia::test]
    async fn test_deprecated_provision_new_account_password_empty_allowed() {
        let disk_manager =
            MockDiskManager::new().with_partition(make_unformatted_account_partition());
        let account_manager =
            AccountManager::new(NULL_ONLY_OPTIONS, disk_manager, MemoryAccountMetadataStore::new());
        assert_eq!(
            account_manager
                .provision_new_account(&TEST_FACCOUNT_METADATA, INSECURE_EMPTY_PASSWORD)
                .await,
            Ok(GLOBAL_ACCOUNT_ID)
        );
    }

    #[fuchsia::test]
    async fn test_deprecated_provision_new_account_password_empty_not_allowed() {
        let disk_manager =
            MockDiskManager::new().with_partition(make_unformatted_account_partition());
        let account_manager = AccountManager::new(
            SCRYPT_ONLY_OPTIONS,
            disk_manager,
            MemoryAccountMetadataStore::new(),
        );
        assert_eq!(
            account_manager
                .provision_new_account(&TEST_FACCOUNT_METADATA, INSECURE_EMPTY_PASSWORD)
                .await,
            Err(faccount::Error::InvalidRequest)
        );
    }

    #[fuchsia::test]
    async fn test_deprecated_provision_new_account_password_too_short() {
        // Passwords must be 8 characters or longer
        let disk_manager =
            MockDiskManager::new().with_partition(make_unformatted_account_partition());
        let account_manager = AccountManager::new(
            SCRYPT_ONLY_OPTIONS,
            disk_manager,
            MemoryAccountMetadataStore::new(),
        );
        assert_eq!(
            account_manager.provision_new_account(&TEST_FACCOUNT_METADATA, "7 chars").await,
            Err(faccount::Error::InvalidRequest)
        );
        assert_eq!(
            account_manager.provision_new_account(&TEST_FACCOUNT_METADATA, "8 chars!").await,
            Ok(GLOBAL_ACCOUNT_ID)
        );
    }

    #[fuchsia::test]
    async fn test_deprecated_provision_new_account_password_not_empty_allowed() {
        let disk_manager =
            MockDiskManager::new().with_partition(make_unformatted_account_partition());
        let account_manager = AccountManager::new(
            SCRYPT_ONLY_OPTIONS,
            disk_manager,
            MemoryAccountMetadataStore::new(),
        );
        assert_eq!(
            account_manager
                .provision_new_account(&TEST_FACCOUNT_METADATA, TEST_SCRYPT_PASSWORD)
                .await,
            Ok(GLOBAL_ACCOUNT_ID)
        );
    }

    #[fuchsia::test]
    async fn test_deprecated_provision_new_account_password_not_empty_not_allowed() {
        let disk_manager =
            MockDiskManager::new().with_partition(make_unformatted_account_partition());
        let account_manager =
            AccountManager::new(NULL_ONLY_OPTIONS, disk_manager, MemoryAccountMetadataStore::new());
        assert_eq!(
            account_manager
                .provision_new_account(&TEST_FACCOUNT_METADATA, TEST_SCRYPT_PASSWORD)
                .await,
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
        let account_manager =
            AccountManager::new(DEFAULT_OPTIONS, disk_manager, MemoryAccountMetadataStore::new());
        assert_eq!(
            account_manager
                .provision_new_account(&TEST_FACCOUNT_METADATA, TEST_SCRYPT_PASSWORD)
                .await,
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
                    unseal_behavior: UnsealBehavior::RejectWithError(|| {
                        DiskError::FailedToUnsealZxcrypt(Status::IO)
                    }),
                    shred_behavior: Ok(()),
                }),
            },
        });
        let account_manager =
            AccountManager::new(DEFAULT_OPTIONS, disk_manager, MemoryAccountMetadataStore::new());
        assert_eq!(
            account_manager
                .provision_new_account(&TEST_FACCOUNT_METADATA, TEST_SCRYPT_PASSWORD)
                .await,
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
                    unseal_behavior: UnsealBehavior::RejectWithError(|| {
                        DiskError::FailedToUnsealZxcrypt(Status::IO)
                    }),
                    shred_behavior: Ok(()),
                }),
            },
        });
        let account_manager =
            AccountManager::new(DEFAULT_OPTIONS, disk_manager, MemoryAccountMetadataStore::new());
        assert_eq!(
            account_manager
                .provision_new_account(&TEST_FACCOUNT_METADATA, TEST_SCRYPT_PASSWORD)
                .await,
            Err(faccount::Error::Resource)
        );
    }

    #[fuchsia::test]
    async fn test_deprecated_provision_new_account_get_data_directory() {
        let disk_manager =
            MockDiskManager::new().with_partition(make_unformatted_account_partition());
        let account_manager =
            AccountManager::new(DEFAULT_OPTIONS, disk_manager, MemoryAccountMetadataStore::new());
        assert_eq!(
            account_manager
                .provision_new_account(&TEST_FACCOUNT_METADATA, TEST_SCRYPT_PASSWORD)
                .await
                .expect("provision account"),
            GLOBAL_ACCOUNT_ID
        );

        let (account, server_end) = fidl::endpoints::create_proxy().unwrap();
        account_manager
            .get_account(GLOBAL_ACCOUNT_ID, TEST_SCRYPT_PASSWORD, server_end)
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
        let bytes_written = file
            .write_at(expected_content, 0)
            .await
            .expect("file write")
            .map_err(Status::from_raw)
            .expect("file write failed");
        assert_eq!(bytes_written, expected_content.len() as u64);

        let actual_content = io_util::file::read(&file).await.expect("read file");
        assert_eq!(&actual_content, expected_content);
    }

    #[fuchsia::test]
    async fn test_already_provisioned_get_data_directory() {
        let disk_manager = MockDiskManager::new()
            .with_partition(make_formatted_account_partition(TEST_SCRYPT_KEY));
        let account_metadata_store =
            MemoryAccountMetadataStore::new().with_password_account(&GLOBAL_ACCOUNT_ID);
        let account_manager =
            AccountManager::new(DEFAULT_OPTIONS, disk_manager, account_metadata_store);

        let (account, server_end) = fidl::endpoints::create_proxy().unwrap();
        account_manager
            .get_account(GLOBAL_ACCOUNT_ID, TEST_SCRYPT_PASSWORD, server_end)
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
        let bytes_written = file
            .write_at(expected_content, 0)
            .await
            .expect("file write")
            .map_err(Status::from_raw)
            .expect("file write failed");
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
        let account_manager =
            AccountManager::new(DEFAULT_OPTIONS, disk_manager, MemoryAccountMetadataStore::new());

        // Expect a Resource failure.
        assert_eq!(
            account_manager
                .provision_new_account(&TEST_FACCOUNT_METADATA, TEST_SCRYPT_PASSWORD)
                .await,
            Err(faccount::Error::Resource)
        );

        // Provisioning again should succeed.
        assert_eq!(
            account_manager
                .provision_new_account(&TEST_FACCOUNT_METADATA, TEST_SCRYPT_PASSWORD)
                .await,
            Ok(GLOBAL_ACCOUNT_ID)
        );
    }

    #[fuchsia::test]
    async fn test_unlock_after_account_locked() {
        let disk_manager = MockDiskManager::new()
            .with_partition(make_formatted_account_partition(TEST_SCRYPT_KEY));
        let account_metadata_store =
            MemoryAccountMetadataStore::new().with_password_account(&GLOBAL_ACCOUNT_ID);
        let account_manager =
            AccountManager::new(DEFAULT_OPTIONS, disk_manager, account_metadata_store);

        let (account, server_end) = fidl::endpoints::create_proxy().unwrap();
        account_manager
            .get_account(GLOBAL_ACCOUNT_ID, TEST_SCRYPT_PASSWORD, server_end)
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
            .get_account(GLOBAL_ACCOUNT_ID, TEST_SCRYPT_PASSWORD, server_end)
            .await
            .expect("get_account");

        let (_, server_end) = fidl::endpoints::create_proxy().unwrap();
        account
            .get_data_directory(server_end)
            .await
            .expect("get_data_directory FIDL")
            .expect("get_data_directory");
    }

    #[fuchsia::test]
    async fn test_remove_account_okay() {
        let disk_manager = MockDiskManager::new()
            .with_partition(make_formatted_account_partition(TEST_SCRYPT_KEY));
        let account_metadata_store =
            MemoryAccountMetadataStore::new().with_password_account(&GLOBAL_ACCOUNT_ID);
        let account_manager =
            AccountManager::new(DEFAULT_OPTIONS, disk_manager, account_metadata_store);

        let account_ids_before = account_manager.get_account_ids().await.expect("get account ids");
        assert_eq!(account_ids_before, vec![GLOBAL_ACCOUNT_ID]);

        account_manager.remove_account(GLOBAL_ACCOUNT_ID, true).await.expect("remove_account");

        let account_ids = account_manager.get_account_ids().await.expect("get account ids");
        assert_eq!(account_ids, Vec::<u64>::new());
    }

    #[fuchsia::test]
    async fn test_remove_account_while_unlocked_okay() {
        let disk_manager = MockDiskManager::new()
            .with_partition(make_formatted_account_partition(TEST_SCRYPT_KEY));
        let account_metadata_store =
            MemoryAccountMetadataStore::new().with_password_account(&GLOBAL_ACCOUNT_ID);
        let account_manager =
            AccountManager::new(DEFAULT_OPTIONS, disk_manager, account_metadata_store);

        let (account, server_end) = fidl::endpoints::create_proxy().unwrap();
        account_manager
            .get_account(GLOBAL_ACCOUNT_ID, TEST_SCRYPT_PASSWORD, server_end)
            .await
            .expect("get_account");

        let (_, server_end) = fidl::endpoints::create_proxy().unwrap();
        account
            .get_data_directory(server_end)
            .await
            .expect("get_data_directory FIDL")
            .expect("get_data_directory");

        account_manager.remove_account(GLOBAL_ACCOUNT_ID, true).await.expect("remove_account");

        // Expect actions on account to fail since the account should have been locked
        let (_, server_end) = fidl::endpoints::create_proxy().unwrap();
        let _ = account
            .get_data_directory(server_end)
            .await
            .expect_err("get_data_directory should fail");

        let account_ids = account_manager.get_account_ids().await.expect("get account ids");
        assert_eq!(account_ids, Vec::<u64>::new());
    }

    #[fuchsia::test]
    async fn test_remove_account_bind_zxcrypt_fails_but_remove_account_succeeds() {
        let disk_manager = MockDiskManager::new().with_partition(MockPartition {
            guid_behavior: Ok(Match::None),
            label_behavior: Ok(Match::Any),
            block: MockBlockDevice {
                zxcrypt_header_behavior: Ok(Match::Any),
                bind_behavior: Err(|| DiskError::BindZxcryptDriverFailed(Status::NOT_SUPPORTED)),
            },
        });
        let account_metadata_store =
            MemoryAccountMetadataStore::new().with_password_account(&GLOBAL_ACCOUNT_ID);
        let account_manager =
            AccountManager::new(DEFAULT_OPTIONS, disk_manager, account_metadata_store);

        let account_ids_before = account_manager.get_account_ids().await.expect("get account ids");
        assert_eq!(account_ids_before, vec![GLOBAL_ACCOUNT_ID]);

        account_manager.remove_account(GLOBAL_ACCOUNT_ID, true).await.expect("remove_account");

        let account_ids = account_manager.get_account_ids().await.expect("get account ids");
        assert_eq!(account_ids, Vec::<u64>::new());
    }

    #[fuchsia::test]
    async fn test_remove_account_shred_fails_but_remove_account_succeeds() {
        // Shredding the account partition is opportunistic but not required for us to return
        // success from remove_account.  Removal of the account metadata is sufficient.
        let disk_manager = MockDiskManager::new()
            .with_partition(make_formatted_account_partition_fail_shred(TEST_SCRYPT_KEY));
        let account_metadata_store =
            MemoryAccountMetadataStore::new().with_password_account(&GLOBAL_ACCOUNT_ID);
        let account_manager =
            AccountManager::new(DEFAULT_OPTIONS, disk_manager, account_metadata_store);

        let account_ids_before = account_manager.get_account_ids().await.expect("get account ids");
        assert_eq!(account_ids_before, vec![GLOBAL_ACCOUNT_ID]);

        account_manager.remove_account(GLOBAL_ACCOUNT_ID, true).await.expect("remove_account");

        let account_ids = account_manager.get_account_ids().await.expect("get account ids");
        assert_eq!(account_ids, Vec::<u64>::new());
    }
}
