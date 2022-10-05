// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    account::{Account, AccountError, CheckNewClientResult},
    account_metadata::{
        AccountMetadata, AccountMetadataStore, AccountMetadataStoreError, AuthenticatorMetadata,
    },
    keys::{Key, KeyEnrollment, KeyEnrollmentError, KeyRetrieval, KeyRetrievalError},
    pinweaver::{CredManager, PinweaverKeyEnroller, PinweaverKeyRetriever, PinweaverParams},
    scrypt::{ScryptKeySource, ScryptParams},
};
use anyhow::{Context, Error};
use fidl::endpoints::{ControlHandle, ServerEnd};
use fidl_fuchsia_identity_account::{
    self as faccount, AccountManagerRequest, AccountManagerRequestStream, AccountMarker,
};
use fidl_fuchsia_identity_credential::{ManagerMarker, ManagerProxy};
use fidl_fuchsia_process_lifecycle::{LifecycleRequest, LifecycleRequestStream};
use fuchsia_component::client::connect_to_protocol;
use futures::{lock::Mutex, prelude::*};
use password_authenticator_config::Config;
use std::{collections::HashMap, sync::Arc};
use storage_manager::{
    minfs::{
        disk::{DiskError, DiskManager, EncryptedBlockDevice},
        StorageManagerExtTrait,
    },
    StorageManager,
};
use tracing::{error, info, warn};

/// The singleton account ID on the device.
/// For now, we only support a single account (as in the fuchsia.identity protocol).  The local
/// account, if it exists, will have a value of 1.
pub const GLOBAL_ACCOUNT_ID: u64 = 1;

/// The minimum length of (non-empty) password that is allowed for new accounts, in bytes.
const MIN_PASSWORD_SIZE: usize = 8;

pub type AccountId = u64;

/// A trait to support injecting credential manager implementations into AccountManager to enable
/// unit testing.
pub trait CredManagerProvider {
    type CM: CredManager + std::marker::Send + std::marker::Sync;
    fn new_cred_manager(&self) -> Result<Self::CM, anyhow::Error>;
}

pub struct EnvCredManagerProvider {}

/// A CredManagerProvider that opens a new connection to the CredentialManager in our incoming
/// namespace whenever a CredManager instance is requested.
impl CredManagerProvider for EnvCredManagerProvider {
    type CM = ManagerProxy;

    fn new_cred_manager(&self) -> Result<Self::CM, anyhow::Error> {
        let proxy = connect_to_protocol::<ManagerMarker>().map_err(|err| {
            error!("unable to connect to credential manager from environment: {:?}", err);
            err
        })?;
        Ok(proxy)
    }
}

pub struct AccountManager<DM, AMS, CMP, SM>
where
    DM: DiskManager,
    AMS: AccountMetadataStore,
    CMP: CredManagerProvider,
    SM: StorageManager,
{
    config: Config,
    account_metadata_store: Mutex<AMS>,
    cred_manager_provider: CMP,
    #[allow(dead_code)]
    storage_manager: SM,

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

#[derive(Debug)]
enum EnrollmentScheme {
    Scrypt,
    Pinweaver,
}

impl<DM, AMS, CMP, SM> AccountManager<DM, AMS, CMP, SM>
where
    DM: DiskManager,
    AMS: AccountMetadataStore,
    CMP: CredManagerProvider,
    SM: StorageManager + StorageManagerExtTrait<DM>,
{
    pub fn new(
        config: Config,
        account_metadata_store: AMS,
        cred_manager_provider: CMP,
        storage_manager: SM,
    ) -> Self {
        Self {
            config,
            account_metadata_store: Mutex::new(account_metadata_store),
            cred_manager_provider,
            storage_manager,
            accounts: Mutex::new(HashMap::new()),
        }
    }

    fn disk_manager(&self) -> &DM {
        // Provides access to the storage_manager's internal disk_manager.
        // TODO(https://fxbug.dev/103134): Plan on removing this, since it
        // crosses separation-of-concerns in an unfortunate way.
        self.storage_manager.disk_manager()
    }

    /// Serially process a stream of incoming LifecycleRequest FIDL requests.
    pub async fn handle_requests_for_lifecycle(&self, mut request_stream: LifecycleRequestStream) {
        info!("Watching for lifecycle events from startup handle");
        while let Some(request) = request_stream.try_next().await.expect("read lifecycle request") {
            match request {
                LifecycleRequest::Stop { control_handle } => {
                    // `password_authenticator` supervises a filesystem process, which expects to
                    // receive advance notice when shutdown is imminent so that it can flush any
                    // cached writes to disk.  To uphold our end of that contract, we implement a
                    // lifecycle listener which responds to a stop request by locking all unlocked
                    // accounts, which in turn has the effect of gracefully stopping the filesystem
                    // and locking storage.
                    info!("Received lifecycle stop request; attempting graceful teardown");

                    match self.lock_all_accounts().await {
                        Ok(()) => {
                            info!("Shutdown complete");
                        }
                        Err(e) => {
                            error!(
                                "error shutting down for lifecycle request; data may not be fully \
                                flushed {:?}",
                                e
                            );
                        }
                    }

                    control_handle.shutdown();
                }
            }
        }
    }

    /// Locks all currently-unlocked accounts.
    async fn lock_all_accounts(&self) -> Result<(), AccountError> {
        // Acquire the lock for all accounts.
        let mut accounts_locked = self.accounts.lock().await;

        // For each account, ensure the account is locked.
        let known_account_ids: Vec<u64> = accounts_locked.keys().map(|x| x.clone()).collect();
        for account_id in known_account_ids.iter() {
            // This structure is a little funky -- we need to take ownership of any Account in
            // AccountState::Provisioned so we can call lock() on them, which requires ownership
            // of the contained Account.
            // We don't need to remove anything that was in AccountState::Provisioning, but due to
            // the ownership requirement above, we have to move it out and then put it back.
            let account_state = accounts_locked.remove(account_id);
            match account_state {
                Some(AccountState::Provisioned(account)) => {
                    info!(%account_id, "Locking account...");
                    account.lock_account().await?;
                    info!(%account_id, "account locked.");
                }
                Some(AccountState::Provisioning(provisioning_lock)) => {
                    // Put the account back in the map, as though we never took it out
                    accounts_locked
                        .insert(*account_id, AccountState::Provisioning(provisioning_lock));
                }
                None => {
                    // This should never be reached, because we're holding the accounts lock.
                    unreachable!();
                }
            }
        }

        Ok(())
    }

    /// Serially process a stream of incoming AccountManager FIDL requests.
    pub async fn handle_requests_for_account_manager(
        &self,
        mut request_stream: AccountManagerRequestStream,
    ) {
        while let Some(request) = request_stream.try_next().await.expect("read request") {
            self.handle_account_manager_request(request)
                .unwrap_or_else(|e| {
                    error!("error handling fidl request: {:?}", e);
                })
                .await
        }
    }

    /// Process a single AccountManager FIDL request and send a reply.
    async fn handle_account_manager_request(
        &self,
        request: AccountManagerRequest,
    ) -> Result<(), Error> {
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
            AccountManagerRequest::GetAccountMetadata { id, responder } => {
                let mut resp = self.get_account_metadata(id).await;
                responder.send(&mut resp).context("sending GetAccountMetadata response")?;
            }
            AccountManagerRequest::GetAccount { payload: _, responder } => {
                let mut resp = Err(faccount::Error::UnsupportedOperation);
                responder.send(&mut resp).context("sending GetAccount response")?;
            }
            AccountManagerRequest::RegisterAccountListener { payload: _, responder } => {
                let mut resp = Err(faccount::Error::UnsupportedOperation);
                responder.send(&mut resp).context("sending RegisterAccountListener response")?;
            }
            AccountManagerRequest::RemoveAccount { id, responder } => {
                let mut resp = self.remove_account(id).await;
                responder.send(&mut resp).context("sending RemoveAccount response")?;
            }
            AccountManagerRequest::ProvisionNewAccount { payload: _, responder } => {
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
                warn!(account_id = %id, "get_account_metadata: ID not found in account metadata store");
                faccount::Error::NotFound
            })?
        };

        Ok(faccount::AccountMetadata {
            name: Some(ams_metadata.name().to_string()),
            ..faccount::AccountMetadata::EMPTY
        })
    }

    /// Compute or retrieve the user key for the account described by `meta` using the
    /// user-provided `password` with the metadata's specified key source.
    /// Note that for the Pinweaver key source, this call may fail if we are unable to reach
    /// CredentialManager or if CredentialManager rejects our password or has some other failure.
    /// For ScryptKeySource, we expect this to return an Ok(), but the key returned may not be the
    /// correct key if the user provided an incorrect password.
    async fn retrieve_user_key(
        &self,
        meta: &AccountMetadata,
        password: &str,
    ) -> Result<Key, KeyRetrievalError> {
        match meta.authenticator_metadata() {
            AuthenticatorMetadata::ScryptOnly(s_meta) => {
                let key_source = ScryptKeySource::from(ScryptParams::from(s_meta.clone()));
                info!("retrieve_user_key: retrieving an scrypt key");
                key_source.retrieve_key(&password).await
            }
            AuthenticatorMetadata::Pinweaver(p_meta) => {
                let cred_manager =
                    self.cred_manager_provider.new_cred_manager().map_err(|err| {
                        error!("retrieve_user_key: could not get credential manager: {:?}", err);
                        KeyRetrievalError::CredentialManagerConnectionError(err)
                    })?;
                let key_source =
                    PinweaverKeyRetriever::new(PinweaverParams::from(p_meta.clone()), cred_manager);
                info!("retrieve_user_key: retrieving a pinweaver key");
                key_source.retrieve_key(&password).await
            }
        }
    }

    /// Remove the key from the key source specified in `meta`s authenticator metadata.
    /// This is expected to succeed trivially for the Scrypt key source, since it has
    /// no resources to dispose, and to make a call to Credential Manager for the Pinweaver key
    /// source.
    async fn remove_key_for_account(
        &self,
        meta: &AccountMetadata,
    ) -> Result<(), KeyEnrollmentError> {
        match meta.authenticator_metadata() {
            AuthenticatorMetadata::ScryptOnly(s_meta) => {
                let mut key_source = ScryptKeySource::from(s_meta.scrypt_params);
                key_source.remove_key(s_meta.clone().into()).await
            }
            AuthenticatorMetadata::Pinweaver(p_meta) => {
                let cred_manager =
                    self.cred_manager_provider.new_cred_manager().map_err(|err| {
                        error!(
                            "remove_key_for_account: could not get credential manager: {:?}",
                            err
                        );
                        KeyEnrollmentError::CredentialManagerConnectionError(err)
                    })?;
                let mut key_source = PinweaverKeyEnroller::new(cred_manager);
                key_source.remove_key(p_meta.clone().into()).await
            }
        }
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
                    account_id = %id,
                    "get_account: requested account ID not found in account metadata store",
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
            warn!(account_id = %id, "get_account: rejecting unexpected account ID in account metadata store");
            return Err(faccount::Error::Internal);
        }

        // Verify that the component configuration allows this type of metadata.
        //
        // If we have an account that is not supported by the current configuration (e.g. an scrypt
        // account created before pinweaver was rolled out) it is extremely unlikely the
        // configuration will ever be reverted to make that account usable in the future. We
        // automatically delete the account, leading the client to create a new account.
        if !account_metadata.allowed_by_config(&self.config) {
            warn!("get_account: deleting account that is not allowed by current configuration");
            self.remove_account(id).await.map_err(|err| {
                warn!("get_account: failed to automatically delete account");
                err
            })?;
            // Now that the account has been deleted we report the same error as if it didn't exist.
            return Err(faccount::Error::NotFound);
        }

        // If account metadata present, retrieve key (using the appropriate scheme for the
        // AccountMetadata instance).
        let key = self.retrieve_user_key(&account_metadata, &password).await.map_err(|err| {
            warn!("get_account: retrieve_user_key failed: {:?}", err);
            err
        })?;

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
                            account_id = %id,
                            "get_account: account is already unsealed, but an incorrect \
                              password was given",
                        );
                        return Err(faccount::Error::FailedAuthentication);
                    }
                }
            }
            Some(AccountState::Provisioning(_)) => {
                // This account is in the process of being provisioned, treat it like it doesn't
                // exist.
                warn!(account_id = %id, "get_account: account is still provisioning");
                return Err(faccount::Error::NotFound);
            }
            None => {
                // There is no account associated with the ID in memory. Check if the account can
                // be unsealed from disk.
                let account = self.unseal_account(id, &key).await.map_err(|err| {
                    warn!(account_id = %id, "get_account: failed to unseal account: {:?}", err);
                    err
                })?;
                info!(account_id = %id, "get_account: unsealed");
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
        info!(account_id = %id, "get_account successful");
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
        let block_device = self
            .storage_manager
            .find_account_partition()
            .await
            .ok_or(faccount::Error::NotFound)
            .map_err(|err| {
                error!("unseal_account: couldn't find account partition");
                err
            })?;
        let encrypted_block =
            self.disk_manager().bind_to_encrypted_block(block_device).await.map_err(|err| {
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
        let minfs = self.disk_manager().serve_minfs(block_device).await.map_err(|err| {
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

        if password.len() < MIN_PASSWORD_SIZE {
            // Passwords must always be at least the minimum length.
            warn!(
                "provision_new_account: refusing to provision account with password of \
                  length {}",
                password.len()
            );
            return Err(faccount::Error::InvalidRequest);
        }

        let enrollment_scheme = if self.config.allow_pinweaver {
            EnrollmentScheme::Pinweaver
        } else {
            EnrollmentScheme::Scrypt
        };

        // For now, we only contemplate one account ID
        let account_id = GLOBAL_ACCOUNT_ID;

        info!(
            "provision_new_account: attempting to provision new account with {:?}",
            enrollment_scheme
        );

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

        let block = self
            .storage_manager
            .find_account_partition()
            .await
            .ok_or(faccount::Error::NotFound)
            .map_err(|err| {
                error!("provision_new_account: couldn't find account partition to provision");
                err
            })?;

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
        let (key, authenticator_metadata): (Key, AuthenticatorMetadata) = match enrollment_scheme {
            EnrollmentScheme::Scrypt => {
                let mut key_source = ScryptKeySource::new();
                key_source
                    .enroll_key(&password)
                    .await
                    .map(|enrolled_key| (enrolled_key.key, enrolled_key.enrollment_data.into()))
            }
            EnrollmentScheme::Pinweaver => {
                let cred_manager =
                    self.cred_manager_provider.new_cred_manager().map_err(|err| {
                        error!(
                            "provision_new_account: could not get credential manager: {:?}",
                            err
                        );
                        KeyEnrollmentError::CredentialManagerConnectionError(err)
                    })?;
                let mut key_source = PinweaverKeyEnroller::new(cred_manager);
                key_source
                    .enroll_key(&password)
                    .await
                    .map(|enrolled_key| (enrolled_key.key, enrolled_key.enrollment_data.into()))
            }
        }
        .map_err(|err| {
            error!("provision_new_account: key enrollment failed during provisioning: {:?}", err);
            err
        })?;

        let metadata = AccountMetadata::new(name.to_string(), authenticator_metadata);

        let res: Result<AccountId, ProvisionError> = async {
            let encrypted_block =
                self.disk_manager().bind_to_encrypted_block(block).await.map_err(|err| {
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
            self.disk_manager().format_minfs(&unsealed_block).await.map_err(|err| {
                error!(
                    "provision_new_account: couldn't format minfs on inner block device: {}",
                    err
                );
                err
            })?;
            let minfs = self.disk_manager().serve_minfs(unsealed_block).await.map_err(|err| {
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
                    %account_id,
                    "provision_new_account: couldn't save account metadata"
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

    async fn remove_account(&self, id: AccountId) -> Result<(), faccount::Error> {
        // To remove an account, we need to:
        // 1. lock the account, if it was unlocked
        // 2. remove the metadata from the account metadata store
        // 3. (best effort) remove the keys from the key source
        // 4. (best effort) shred the backing volume
        // 5. mark the account as fully removed (and thus eligible to be provisioned again)
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
                    account_id = %id,
                    "remove_account: can't remove account still in Provisioning state",
                );
                Err(faccount::Error::FailedPrecondition)?
            }
            Some(AccountState::Provisioned(account)) => {
                // Try to lock the Account.  (If it wasn't in
                // self.accounts, we're not serving any requests for it.)
                let res = account.clone().lock_account().await;
                res.map_err(|err| {
                    error!(account_id = %id, "remove_account: could not lock: {:?}", err);
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
        let account_metadata = ams_locked
            .load(&id)
            .await
            .map_err(|err| {
                error!(account_id = %id, "remove_account: couldn't load account metadata");
                err
            })?
            .ok_or_else(|| {
                error!(account_id = %id, "remove_account: no account metadata");
                faccount::Error::NotFound
            })?;
        ams_locked.remove(&id).await.map_err(|err| {
            error!(account_id = %id, "remove_account: couldn't remove account metadata");
            err
        })?;
        drop(ams_locked);

        // step 3: (best effort) remove the key from the key source
        // For key sources that have no resources attached, this will succeed trivially.
        // For key sources that do have resources attached, we will make an attempt to destroy
        // those resources, but if that attempt fails, we carry on anyway and will eventually
        // report success to the user.
        let _remove_res = self.remove_key_for_account(&account_metadata).await.map_err(|err| {
            // Log the failure, but ignore the result.
            warn!(
                account_id = %id,
                "remove_account: couldn't remove enrolled key for account: {} (ignored)",
                err
            );
        });

        // step 4: (best effort) shred the backing volume
        // Try to shred the backing volume, waiting for completion, but ignoring errors.
        // The metadata removal is sufficient to make the volume no longer unsealable, so we
        // should not return a failure if we make it to here.  We should block though, because if
        // we return before we've shredded the volume, a client could race with itself.
        match self.storage_manager.find_account_partition().await {
            Some(block_device) => {
                match self.disk_manager().bind_to_encrypted_block(block_device).await {
                    Ok(encrypted_block) => {
                        let _ = encrypted_block.shred().await.map_err(|err| {
                            warn!(
                                "remove_account: couldn't shred encrypted block device: {} \
                                    (ignored)",
                                err
                            );
                        });
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

        // step 5: mark the account as fully removed (and thus eligible to be provisioned again)
        // Mark that we've finished deprovisioning this user by releasing the self.accounts lock
        // and returning success.
        drop(accounts_locked);
        Ok(())
    }
}

#[cfg(test)]
mod test {
    use {
        super::*,
        crate::{
            account_metadata::{
                test::{TEST_NAME, TEST_PINWEAVER_METADATA, TEST_SCRYPT_METADATA},
                AccountMetadata, AccountMetadataStoreError,
            },
            keys::Key,
            pinweaver::test::{
                MockCredManager, TEST_PINWEAVER_ACCOUNT_KEY, TEST_PINWEAVER_CREDENTIAL_LABEL,
                TEST_PINWEAVER_HE_SECRET, TEST_PINWEAVER_LE_SECRET,
            },
            scrypt::test::{TEST_SCRYPT_KEY, TEST_SCRYPT_PASSWORD},
            testing::{
                Match, MockBlockDevice, MockDiskManager, MockEncryptedBlockDevice, MockPartition,
                UnsealBehavior,
            },
        },
        anyhow::anyhow,
        async_trait::async_trait,
        fidl_fuchsia_io as fio,
        fuchsia_zircon::Status,
        lazy_static::lazy_static,
        storage_manager::minfs::{
            disk::{testing::MockMinfs, DiskError},
            StorageManager as MinfsStorageManager,
        },
        vfs::execution_scope::ExecutionScope,
    };

    // By default, allow any implemented form of password and encryption.
    const DEFAULT_CONFIG: Config = Config { allow_scrypt: true, allow_pinweaver: true };
    // Define more restrictive configs to verify exclusions are implemented correctly.
    const SCRYPT_ONLY_CONFIG: Config = Config { allow_scrypt: true, allow_pinweaver: false };
    const PINWEAVER_ONLY_CONFIG: Config = Config { allow_scrypt: false, allow_pinweaver: true };

    // An account ID that should not exist.
    const UNSUPPORTED_ACCOUNT_ID: u64 = 42;

    #[derive(Debug, Clone)]
    struct MemoryAccountMetadataStore {
        accounts: std::collections::HashMap<AccountId, AccountMetadata>,
    }

    impl MemoryAccountMetadataStore {
        fn new() -> MemoryAccountMetadataStore {
            MemoryAccountMetadataStore { accounts: std::collections::HashMap::new() }
        }

        fn with_password_account(mut self, account_id: &AccountId) -> Self {
            let metadata = TEST_SCRYPT_METADATA.clone();
            self.accounts.insert(*account_id, metadata);
            self
        }

        fn with_pinweaver_account(mut self, account_id: &AccountId) -> Self {
            let metadata = TEST_PINWEAVER_METADATA.clone();
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

    struct MockCredManagerProvider {
        mcm: MockCredManager,
    }

    impl MockCredManagerProvider {
        fn new() -> MockCredManagerProvider {
            MockCredManagerProvider { mcm: MockCredManager::new() }
        }
    }

    impl CredManagerProvider for MockCredManagerProvider {
        type CM = MockCredManager;
        fn new_cred_manager(&self) -> Result<Self::CM, anyhow::Error> {
            Ok(self.mcm.clone())
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

    fn make_storage_manager(disk_manager: MockDiskManager) -> MinfsStorageManager<MockDiskManager> {
        MinfsStorageManager::new(disk_manager)
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
        let cred_manager_provider = MockCredManagerProvider::new();
        let storage_manager = make_storage_manager(disk_manager);
        let account_manager = AccountManager::new(
            DEFAULT_CONFIG,
            account_metadata_store,
            cred_manager_provider,
            storage_manager,
        );
        let account_ids = account_manager.get_account_ids().await.expect("get account ids");
        assert_eq!(account_ids, Vec::<u64>::new());
    }

    #[fuchsia::test]
    async fn test_get_account_ids_found() {
        let disk_manager =
            MockDiskManager::new().with_partition(make_formatted_account_partition_any_key());
        let account_metadata_store =
            MemoryAccountMetadataStore::new().with_password_account(&GLOBAL_ACCOUNT_ID);
        let cred_manager_provider = MockCredManagerProvider::new();
        let storage_manager = make_storage_manager(disk_manager);
        let account_manager = AccountManager::new(
            DEFAULT_CONFIG,
            account_metadata_store,
            cred_manager_provider,
            storage_manager,
        );
        let account_ids = account_manager.get_account_ids().await.expect("get account ids");
        assert_eq!(account_ids, vec![GLOBAL_ACCOUNT_ID]);
    }

    #[fuchsia::test]
    async fn test_get_account_metadata_found() {
        let disk_manager =
            MockDiskManager::new().with_partition(make_formatted_account_partition_any_key());
        let account_metadata_store =
            MemoryAccountMetadataStore::new().with_password_account(&GLOBAL_ACCOUNT_ID);
        let cred_manager_provider = MockCredManagerProvider::new();
        let storage_manager = make_storage_manager(disk_manager);
        let account_manager = AccountManager::new(
            DEFAULT_CONFIG,
            account_metadata_store,
            cred_manager_provider,
            storage_manager,
        );
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
        let cred_manager_provider = MockCredManagerProvider::new();
        let storage_manager = make_storage_manager(disk_manager);
        let account_manager = AccountManager::new(
            DEFAULT_CONFIG,
            account_metadata_store,
            cred_manager_provider,
            storage_manager,
        );
        let err = account_manager.get_account_metadata(UNSUPPORTED_ACCOUNT_ID).await.unwrap_err();
        assert_eq!(err, faccount::Error::NotFound);
    }

    #[fuchsia::test]
    async fn test_get_account_no_accounts() {
        let disk_manager = MockDiskManager::new();
        let storage_manager = make_storage_manager(disk_manager);
        let account_manager = AccountManager::new(
            DEFAULT_CONFIG,
            MemoryAccountMetadataStore::new(),
            MockCredManagerProvider::new(),
            storage_manager,
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
        let cred_manager_provider = MockCredManagerProvider::new();
        let storage_manager = make_storage_manager(disk_manager);
        let account_manager = AccountManager::new(
            DEFAULT_CONFIG,
            account_metadata_store,
            cred_manager_provider,
            storage_manager,
        );
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
        let cred_manager_provider = MockCredManagerProvider::new();
        let storage_manager = make_storage_manager(disk_manager);
        let account_manager = AccountManager::new(
            DEFAULT_CONFIG,
            account_metadata_store,
            cred_manager_provider,
            storage_manager,
        );
        let (_, server) = fidl::endpoints::create_endpoints::<AccountMarker>().unwrap();
        assert_eq!(
            account_manager.get_account(GLOBAL_ACCOUNT_ID, BAD_PASSWORD, server).await,
            Err(faccount::Error::FailedAuthentication)
        );
    }

    #[fuchsia::test]
    async fn test_get_account_found_correct_password_allowed() {
        let disk_manager = MockDiskManager::new()
            .with_partition(make_formatted_account_partition(TEST_SCRYPT_KEY));
        let account_metadata_store =
            MemoryAccountMetadataStore::new().with_password_account(&GLOBAL_ACCOUNT_ID);
        let cred_manager_provider = MockCredManagerProvider::new();
        let storage_manager = make_storage_manager(disk_manager);
        let account_manager = AccountManager::new(
            SCRYPT_ONLY_CONFIG,
            account_metadata_store,
            cred_manager_provider,
            storage_manager,
        );
        let (client, server) = fidl::endpoints::create_proxy::<AccountMarker>().unwrap();
        account_manager
            .get_account(GLOBAL_ACCOUNT_ID, TEST_SCRYPT_PASSWORD, server)
            .await
            .expect("get account");
        let (_, server) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
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
        let cred_manager_provider = MockCredManagerProvider::new();
        let storage_manager = make_storage_manager(disk_manager);
        let account_manager = AccountManager::new(
            PINWEAVER_ONLY_CONFIG,
            account_metadata_store,
            cred_manager_provider,
            storage_manager,
        );
        let (_, server) = fidl::endpoints::create_proxy::<AccountMarker>().unwrap();

        assert_eq!(
            account_manager.get_account_ids().await.expect("get account ids"),
            vec![GLOBAL_ACCOUNT_ID]
        );
        // Attempting to get the account of an unsupported type should fail ...
        assert_eq!(
            account_manager.get_account(GLOBAL_ACCOUNT_ID, TEST_SCRYPT_PASSWORD, server).await,
            Err(faccount::Error::NotFound)
        );
        // ... and should also cause the invalid account to be deleted.
        assert_eq!(
            account_manager.get_account_ids().await.expect("get account ids"),
            Vec::<u64>::new()
        );
    }

    #[fuchsia::test]
    async fn test_get_account_pinweaver_correct_password() {
        let disk_manager = MockDiskManager::new()
            .with_partition(make_formatted_account_partition(TEST_PINWEAVER_ACCOUNT_KEY));
        let account_metadata_store =
            MemoryAccountMetadataStore::new().with_pinweaver_account(&GLOBAL_ACCOUNT_ID);
        let cred_manager_provider = MockCredManagerProvider::new();
        let mut cred_manager = cred_manager_provider.new_cred_manager().expect("get cred manager");
        let label = cred_manager
            .add(&TEST_PINWEAVER_LE_SECRET, &TEST_PINWEAVER_HE_SECRET)
            .await
            .expect("enroll key");
        assert_eq!(label, TEST_PINWEAVER_CREDENTIAL_LABEL);
        let storage_manager = make_storage_manager(disk_manager);
        let account_manager = AccountManager::new(
            PINWEAVER_ONLY_CONFIG,
            account_metadata_store,
            cred_manager_provider,
            storage_manager,
        );
        let (client, server) = fidl::endpoints::create_proxy::<AccountMarker>().unwrap();
        account_manager
            .get_account(GLOBAL_ACCOUNT_ID, TEST_SCRYPT_PASSWORD, server)
            .await
            .expect("get account");
        let (_, server) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
        assert_eq!(
            client.get_data_directory(server).await.expect("get_data_directory FIDL"),
            Ok(())
        );
    }

    #[fuchsia::test]
    async fn test_get_account_pinweaver_wrong_password() {
        const WRONG_PASSWORD: &str = "wrong password";
        let disk_manager = MockDiskManager::new()
            .with_partition(make_formatted_account_partition(TEST_PINWEAVER_ACCOUNT_KEY));
        let account_metadata_store =
            MemoryAccountMetadataStore::new().with_pinweaver_account(&GLOBAL_ACCOUNT_ID);
        let cred_manager_provider = MockCredManagerProvider::new();
        let mut cred_manager = cred_manager_provider.new_cred_manager().expect("get cred manager");
        let label = cred_manager
            .add(&TEST_PINWEAVER_LE_SECRET, &TEST_PINWEAVER_HE_SECRET)
            .await
            .expect("enroll key");
        assert_eq!(label, TEST_PINWEAVER_CREDENTIAL_LABEL);
        let storage_manager = make_storage_manager(disk_manager);
        let account_manager = AccountManager::new(
            PINWEAVER_ONLY_CONFIG,
            account_metadata_store,
            cred_manager_provider,
            storage_manager,
        );
        let (_, server) = fidl::endpoints::create_proxy::<AccountMarker>().unwrap();
        let res = account_manager.get_account(GLOBAL_ACCOUNT_ID, WRONG_PASSWORD, server).await;
        assert_eq!(res, Err(faccount::Error::FailedAuthentication));
    }

    #[fuchsia::test]
    async fn test_multiple_get_account_channels_concurrent() {
        let disk_manager = MockDiskManager::new()
            .with_partition(make_formatted_account_partition(TEST_SCRYPT_KEY));
        let account_metadata_store =
            MemoryAccountMetadataStore::new().with_password_account(&GLOBAL_ACCOUNT_ID);
        let cred_manager_provider = MockCredManagerProvider::new();
        let storage_manager = make_storage_manager(disk_manager);
        let account_manager = AccountManager::new(
            DEFAULT_CONFIG,
            account_metadata_store,
            cred_manager_provider,
            storage_manager,
        );
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
        let (_, server) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
        assert_eq!(
            client1.get_data_directory(server).await.expect("get_data_directory 1 FIDL"),
            Ok(())
        );
        let (_, server) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
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
        let cred_manager_provider = MockCredManagerProvider::new();
        let storage_manager = make_storage_manager(disk_manager);
        let account_manager = AccountManager::new(
            DEFAULT_CONFIG,
            account_metadata_store,
            cred_manager_provider,
            storage_manager,
        );
        let (client, server) = fidl::endpoints::create_proxy::<AccountMarker>().unwrap();
        account_manager
            .get_account(GLOBAL_ACCOUNT_ID, TEST_SCRYPT_PASSWORD, server)
            .await
            .expect("get account 1");

        let (_, server) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
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
        let (_, server) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
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
        let cred_manager_provider = MockCredManagerProvider::new();
        let storage_manager = make_storage_manager(disk_manager);
        let account_manager = AccountManager::new(
            DEFAULT_CONFIG,
            account_metadata_store,
            cred_manager_provider,
            storage_manager,
        );
        let (client, server) = fidl::endpoints::create_proxy::<AccountMarker>().unwrap();
        account_manager
            .get_account(GLOBAL_ACCOUNT_ID, TEST_SCRYPT_PASSWORD, server)
            .await
            .expect("get account");
        let (_, server) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
        assert_eq!(
            client.get_data_directory(server).await.expect("get_data_directory FIDL"),
            Ok(())
        );
        account_manager.lock_all_accounts().await.expect("lock_all_accounts");
        let (_, server) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
        let err =
            client.get_data_directory(server).await.expect_err("get_data_directory should fail");
        assert!(err.is_closed());
    }

    #[fuchsia::test]
    async fn test_deprecated_provision_new_account_requires_name_in_metadata() {
        let disk_manager = MockDiskManager::new()
            .with_partition(make_formatted_account_partition(TEST_SCRYPT_KEY));
        let cred_manager_provider = MockCredManagerProvider::new();
        let storage_manager = make_storage_manager(disk_manager);
        let account_manager = AccountManager::new(
            DEFAULT_CONFIG,
            MemoryAccountMetadataStore::new(),
            cred_manager_provider,
            storage_manager,
        );
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
        let cred_manager_provider = MockCredManagerProvider::new();
        let storage_manager = make_storage_manager(disk_manager);
        let account_manager = AccountManager::new(
            DEFAULT_CONFIG,
            MemoryAccountMetadataStore::new(),
            cred_manager_provider,
            storage_manager,
        );
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
        let cred_manager_provider = MockCredManagerProvider::new();
        let storage_manager = make_storage_manager(disk_manager);
        let account_manager = AccountManager::new(
            DEFAULT_CONFIG,
            MemoryAccountMetadataStore::new(),
            cred_manager_provider,
            storage_manager,
        );
        assert_eq!(
            account_manager
                .provision_new_account(&TEST_FACCOUNT_METADATA, TEST_SCRYPT_PASSWORD)
                .await
                .expect("provision account"),
            GLOBAL_ACCOUNT_ID
        );
    }

    #[fuchsia::test]
    async fn test_deprecated_provision_new_account_password_too_short() {
        // Passwords must be 8 characters or longer
        let disk_manager =
            MockDiskManager::new().with_partition(make_unformatted_account_partition());
        let cred_manager_provider = MockCredManagerProvider::new();
        let storage_manager = make_storage_manager(disk_manager);
        let account_manager = AccountManager::new(
            SCRYPT_ONLY_CONFIG,
            MemoryAccountMetadataStore::new(),
            cred_manager_provider,
            storage_manager,
        );
        assert_eq!(
            account_manager.provision_new_account(&TEST_FACCOUNT_METADATA, "").await,
            Err(faccount::Error::InvalidRequest)
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
        let cred_manager_provider = MockCredManagerProvider::new();
        let storage_manager = make_storage_manager(disk_manager);
        let account_manager = AccountManager::new(
            SCRYPT_ONLY_CONFIG,
            MemoryAccountMetadataStore::new(),
            cred_manager_provider,
            storage_manager,
        );
        assert_eq!(
            account_manager
                .provision_new_account(&TEST_FACCOUNT_METADATA, TEST_SCRYPT_PASSWORD)
                .await,
            Ok(GLOBAL_ACCOUNT_ID)
        );
    }

    #[fuchsia::test]
    async fn test_deprecated_provision_new_account_pinweaver() {
        let disk_manager =
            MockDiskManager::new().with_partition(make_unformatted_account_partition());
        let cred_manager_provider = MockCredManagerProvider::new();
        let storage_manager = make_storage_manager(disk_manager);
        let account_manager = AccountManager::new(
            PINWEAVER_ONLY_CONFIG,
            MemoryAccountMetadataStore::new(),
            cred_manager_provider,
            storage_manager,
        );
        assert_eq!(
            account_manager
                .provision_new_account(&TEST_FACCOUNT_METADATA, TEST_SCRYPT_PASSWORD)
                .await,
            Ok(GLOBAL_ACCOUNT_ID)
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
        let cred_manager_provider = MockCredManagerProvider::new();
        let storage_manager = make_storage_manager(disk_manager);
        let account_manager = AccountManager::new(
            DEFAULT_CONFIG,
            MemoryAccountMetadataStore::new(),
            cred_manager_provider,
            storage_manager,
        );
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
        let cred_manager_provider = MockCredManagerProvider::new();
        let storage_manager = make_storage_manager(disk_manager);
        let account_manager = AccountManager::new(
            DEFAULT_CONFIG,
            MemoryAccountMetadataStore::new(),
            cred_manager_provider,
            storage_manager,
        );
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
        let cred_manager_provider = MockCredManagerProvider::new();
        let storage_manager = make_storage_manager(disk_manager);
        let account_manager = AccountManager::new(
            DEFAULT_CONFIG,
            MemoryAccountMetadataStore::new(),
            cred_manager_provider,
            storage_manager,
        );
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
        let cred_manager_provider = MockCredManagerProvider::new();
        let storage_manager = make_storage_manager(disk_manager);
        let account_manager = AccountManager::new(
            DEFAULT_CONFIG,
            MemoryAccountMetadataStore::new(),
            cred_manager_provider,
            storage_manager,
        );
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
        let file = fuchsia_fs::directory::open_file(
            &root_dir,
            "test",
            fio::OpenFlags::CREATE
                | fio::OpenFlags::RIGHT_READABLE
                | fio::OpenFlags::RIGHT_WRITABLE,
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

        let actual_content = fuchsia_fs::file::read(&file).await.expect("read file");
        assert_eq!(&actual_content, expected_content);
    }

    #[fuchsia::test]
    async fn test_already_provisioned_get_data_directory() {
        let disk_manager = MockDiskManager::new()
            .with_partition(make_formatted_account_partition(TEST_SCRYPT_KEY));
        let account_metadata_store =
            MemoryAccountMetadataStore::new().with_password_account(&GLOBAL_ACCOUNT_ID);
        let cred_manager_provider = MockCredManagerProvider::new();
        let storage_manager = make_storage_manager(disk_manager);
        let account_manager = AccountManager::new(
            DEFAULT_CONFIG,
            account_metadata_store,
            cred_manager_provider,
            storage_manager,
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
        let file = fuchsia_fs::directory::open_file(
            &root_dir,
            "test",
            fio::OpenFlags::CREATE
                | fio::OpenFlags::RIGHT_READABLE
                | fio::OpenFlags::RIGHT_WRITABLE,
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

        let actual_content = fuchsia_fs::file::read(&file).await.expect("read file");
        assert_eq!(&actual_content, expected_content);
    }

    #[fuchsia::test]
    async fn test_recover_from_failed_provisioning() {
        let scope = ExecutionScope::new();
        let mut one_time_failure = Some(DiskError::MinfsServeError(anyhow!("Fake serve error")));
        let disk_manager = MockDiskManager::new()
            .with_partition(make_unformatted_account_partition())
            .with_serve_minfs(move || {
                if let Some(err) = one_time_failure.take() {
                    Err(err)
                } else {
                    Ok(MockMinfs::simple(scope.clone()))
                }
            });
        let cred_manager_provider = MockCredManagerProvider::new();
        let storage_manager = make_storage_manager(disk_manager);
        let account_manager = AccountManager::new(
            DEFAULT_CONFIG,
            MemoryAccountMetadataStore::new(),
            cred_manager_provider,
            storage_manager,
        );

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
        let cred_manager_provider = MockCredManagerProvider::new();
        let storage_manager = make_storage_manager(disk_manager);
        let account_manager = AccountManager::new(
            DEFAULT_CONFIG,
            account_metadata_store,
            cred_manager_provider,
            storage_manager,
        );

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

        account_manager.lock_all_accounts().await.expect("lock_all_accounts");

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
    async fn test_key_retrieval() {
        let disk_manager = MockDiskManager::new();
        let account_metadata_store = MemoryAccountMetadataStore::new();
        let cred_manager_provider = MockCredManagerProvider::new();
        let mut cred_manager = cred_manager_provider.new_cred_manager().expect("new_cred_manager");
        let label = cred_manager
            .add(&TEST_PINWEAVER_LE_SECRET, &TEST_PINWEAVER_HE_SECRET)
            .await
            .expect("enroll key");
        assert_eq!(label, TEST_PINWEAVER_CREDENTIAL_LABEL);
        let storage_manager = make_storage_manager(disk_manager);
        let account_manager = AccountManager::new(
            DEFAULT_CONFIG,
            account_metadata_store,
            cred_manager_provider,
            storage_manager,
        );

        // scrypt
        let key = account_manager
            .retrieve_user_key(&TEST_SCRYPT_METADATA, TEST_SCRYPT_PASSWORD)
            .await
            .expect("retrieve key (scrypt)");
        assert_eq!(key, TEST_SCRYPT_KEY);

        // pinweaver
        let key = account_manager
            .retrieve_user_key(&TEST_PINWEAVER_METADATA, TEST_SCRYPT_PASSWORD)
            .await
            .expect("retrieve key (pinweaver)");
        assert_eq!(key, TEST_PINWEAVER_ACCOUNT_KEY);
    }

    #[fuchsia::test]
    async fn test_remove_account_okay() {
        let disk_manager = MockDiskManager::new()
            .with_partition(make_formatted_account_partition(TEST_SCRYPT_KEY));
        let account_metadata_store =
            MemoryAccountMetadataStore::new().with_password_account(&GLOBAL_ACCOUNT_ID);
        let cred_manager_provider = MockCredManagerProvider::new();
        let storage_manager = make_storage_manager(disk_manager);
        let account_manager = AccountManager::new(
            DEFAULT_CONFIG,
            account_metadata_store,
            cred_manager_provider,
            storage_manager,
        );

        let account_ids_before = account_manager.get_account_ids().await.expect("get account ids");
        assert_eq!(account_ids_before, vec![GLOBAL_ACCOUNT_ID]);

        account_manager.remove_account(GLOBAL_ACCOUNT_ID).await.expect("remove_account");

        let account_ids = account_manager.get_account_ids().await.expect("get account ids");
        assert_eq!(account_ids, Vec::<u64>::new());
    }

    #[fuchsia::test]
    async fn test_remove_account_while_unlocked_okay() {
        let disk_manager = MockDiskManager::new()
            .with_partition(make_formatted_account_partition(TEST_SCRYPT_KEY));
        let account_metadata_store =
            MemoryAccountMetadataStore::new().with_password_account(&GLOBAL_ACCOUNT_ID);
        let cred_manager_provider = MockCredManagerProvider::new();
        let storage_manager = make_storage_manager(disk_manager);
        let account_manager = AccountManager::new(
            DEFAULT_CONFIG,
            account_metadata_store,
            cred_manager_provider,
            storage_manager,
        );

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

        account_manager.remove_account(GLOBAL_ACCOUNT_ID).await.expect("remove_account");

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
        let cred_manager_provider = MockCredManagerProvider::new();
        let storage_manager = make_storage_manager(disk_manager);
        let account_manager = AccountManager::new(
            DEFAULT_CONFIG,
            account_metadata_store,
            cred_manager_provider,
            storage_manager,
        );

        let account_ids_before = account_manager.get_account_ids().await.expect("get account ids");
        assert_eq!(account_ids_before, vec![GLOBAL_ACCOUNT_ID]);

        account_manager.remove_account(GLOBAL_ACCOUNT_ID).await.expect("remove_account");

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
        let cred_manager_provider = MockCredManagerProvider::new();
        let storage_manager = make_storage_manager(disk_manager);
        let account_manager = AccountManager::new(
            DEFAULT_CONFIG,
            account_metadata_store,
            cred_manager_provider,
            storage_manager,
        );

        let account_ids_before = account_manager.get_account_ids().await.expect("get account ids");
        assert_eq!(account_ids_before, vec![GLOBAL_ACCOUNT_ID]);

        account_manager.remove_account(GLOBAL_ACCOUNT_ID).await.expect("remove_account");

        let account_ids = account_manager.get_account_ids().await.expect("get account ids");
        assert_eq!(account_ids, Vec::<u64>::new());
    }

    #[fuchsia::test]
    async fn test_remove_account_remove_key_fails_but_remove_account_succeeds() {
        let disk_manager = MockDiskManager::new()
            .with_partition(make_formatted_account_partition(TEST_PINWEAVER_ACCOUNT_KEY));
        let account_metadata_store =
            MemoryAccountMetadataStore::new().with_pinweaver_account(&GLOBAL_ACCOUNT_ID);
        // This cred manager will not know about the label `TEST_PINWEAVER_CREDENTIAL_LABEL`.
        let cred_manager_provider = MockCredManagerProvider::new();
        let storage_manager = make_storage_manager(disk_manager);
        let account_manager = AccountManager::new(
            PINWEAVER_ONLY_CONFIG,
            account_metadata_store,
            cred_manager_provider,
            storage_manager,
        );

        let account_ids_before = account_manager.get_account_ids().await.expect("get account ids");
        assert_eq!(account_ids_before, vec![GLOBAL_ACCOUNT_ID]);

        account_manager.remove_account(GLOBAL_ACCOUNT_ID).await.expect("remove_account");

        let account_ids = account_manager.get_account_ids().await.expect("get account ids");
        assert_eq!(account_ids, Vec::<u64>::new());
    }
}
