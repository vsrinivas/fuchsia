// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::keys::Key;
use anyhow::Context;
use fidl::endpoints::ServerEnd;
use fidl_fuchsia_identity_account::{
    self as faccount, AccountRequest, AccountRequestStream, Lifetime,
};
use fidl_fuchsia_io as fio;
use fuchsia_zircon::Status;
use futures::{lock::Mutex, prelude::*, select};
use identity_common::{TaskGroup, TaskGroupCancel, TaskGroupError};
use std::sync::Arc;
use storage_manager::minfs::disk::{DiskError, EncryptedBlockDevice, Minfs};
use thiserror::Error;
use tracing::{error, info, warn};

/// The default directory on the filesystem that we return to all clients. Returning a subdirectory
/// rather than the root provides scope to store private account data on the encrypted filesystem
/// that FIDL clients cannot access, and to potentially serve different directories to different
/// clients in the future.
const DEFAULT_DIR: &str = "default";

/// Serves the `fuchsia.identity.account.Account` FIDL protocol.
pub struct Account<EB, M> {
    /// Tasks handling work on behalf of the Account are spawned in this group.
    /// The lifetime of those tasks are tied to this account.
    task_group: TaskGroup,
    /// The state of the account.
    state: Mutex<State<EB, M>>,
}

/// The internal state of the Account.
enum State<EB, M> {
    /// The account is locked/encrypted.
    Locked,
    /// The account is unlocked/unencrypted.
    Unlocked {
        /// The derived key used to unseal the partition.
        key: Key,

        /// The unsealed encrypted block device.
        encrypted_block: EB,

        /// The instance of minfs serving the filesystem.
        minfs: M,
    },
}

/// The result of the [`Account::check_new_client()`] method.
#[derive(Debug, PartialEq)]
pub enum CheckNewClientResult {
    /// The [`Account`] instance has been locked and should not be re-used.
    Locked,
    /// The [`Account`] instance is unlocked and the keys match.
    UnlockedSameKey,
    /// The [`Account`] instance is unlocked but the keys did *NOT* match.
    UnlockedDifferentKey,
}

#[derive(Debug, Error)]
pub enum AccountError {
    #[error("Failed to spawn a task: {0}")]
    SpawnTaskError(#[source] TaskGroupError),
    #[error("Failed to lock account: {0}")]
    LockError(#[source] DiskError),
}

impl<EB: EncryptedBlockDevice, M: Minfs> Account<EB, M> {
    /// Creates a new `Account` in the unlocked state, with the `key` used to unlock the account
    /// and the instance of minfs serving the filesystem.
    pub fn new(key: Key, encrypted_block: EB, minfs: M) -> Self {
        Account {
            task_group: TaskGroup::new(),
            state: Mutex::new(State::Unlocked { key, encrypted_block, minfs }),
        }
    }

    /// Checks whether a new client can be served by this [`Account`] instance.
    pub async fn check_new_client(&self, client_key: &Key) -> CheckNewClientResult {
        let state = self.state.lock().await;
        match &*state {
            State::Locked => CheckNewClientResult::Locked,
            State::Unlocked { key: account_key, .. } => {
                if client_key == account_key {
                    CheckNewClientResult::UnlockedSameKey
                } else {
                    CheckNewClientResult::UnlockedDifferentKey
                }
            }
        }
    }

    /// Locks this [`Account`] instance, shutting down the filesystem and canceling any outstanding
    /// tasks.
    pub async fn lock(self: Arc<Self>) -> Result<(), AccountError> {
        let mut state = self.state.lock().await;
        match self.task_group.cancel_no_wait().await {
            // Tolerate an already canceling task group, in case a previous attempt to lock failed.
            // This really shouldn't happen but the error shouldn't be a show stopper.
            Ok(()) | Err(TaskGroupError::AlreadyCancelled) => (),
        }
        match std::mem::replace(&mut *state, State::Locked) {
            State::Locked => {
                warn!("account has already been locked");
                Ok(())
            }
            State::Unlocked { encrypted_block, minfs, .. } => {
                minfs.shutdown().await;
                let seal_fut = encrypted_block.seal();
                match seal_fut.await {
                    Ok(()) => Ok(()),
                    Err(DiskError::FailedToSealZxcrypt(Status::BAD_STATE)) => {
                        // The block device is already sealed. We're in a bad state, but
                        // technically the device is sealed, so job complete?
                        warn!("block device was already sealed");
                        Ok(())
                    }
                    Err(e) => Err(AccountError::LockError(e)),
                }
            }
        }
    }

    pub async fn handle_requests_for_stream(
        self: Arc<Self>,
        account_stream: AccountRequestStream,
    ) -> Result<(), AccountError> {
        let account = self.clone();
        self.task_group
            .spawn(move |cancel| account.handle_requests_for_stream_impl(account_stream, cancel))
            .await
            .map_err(AccountError::SpawnTaskError)
    }

    /// Serially process a stream of incoming Account FIDL requests, shutting down the channel when
    /// `cancel` is signaled.
    async fn handle_requests_for_stream_impl(
        self: Arc<Self>,
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
                    info!("Account FIDL server canceled");
                    break;
                }
            }
        }
    }

    /// Handles a single Account FIDL request.
    async fn handle_request(
        self: &Arc<Self>,
        request: AccountRequest,
    ) -> Result<(), anyhow::Error> {
        match request {
            AccountRequest::Lock { responder } => {
                let mut res = match self.clone().lock().await {
                    Ok(()) => Ok(()),
                    Err(err) => {
                        error!("{}", err);
                        Err(faccount::Error::Resource)
                    }
                };
                responder.send(&mut res).context("sending Lock response")?;
            }
            AccountRequest::GetDataDirectory { data_directory, responder } => {
                let mut result = self.get_data_directory(data_directory).await;
                responder.send(&mut result).context("sending GetDataDirectory response")?;
            }
            AccountRequest::GetAuthState { responder } => {
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
            AccountRequest::RegisterAuthListener { payload: _, responder } => {
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

    /// Implements the GetDataDirectory method
    async fn get_data_directory(
        &self,
        data_directory: ServerEnd<fio::DirectoryMarker>,
    ) -> Result<(), faccount::Error> {
        match &*self.state.lock().await {
            State::Locked => {
                warn!("get_data_directory: account is locked");
                Err(faccount::Error::FailedPrecondition)
            }
            State::Unlocked { minfs, .. } => minfs
                .root_dir()
                .open(
                    fio::OpenFlags::RIGHT_READABLE
                        | fio::OpenFlags::RIGHT_WRITABLE
                        | fio::OpenFlags::DIRECTORY
                        | fio::OpenFlags::CREATE,
                    fio::MODE_TYPE_DIRECTORY,
                    DEFAULT_DIR,
                    ServerEnd::new(data_directory.into_channel()),
                )
                .map_err(|err| {
                    error!("get_data_directory: couldn't open data dir out of minfs: {}", err);
                    faccount::Error::Resource
                }),
        }
    }
}

#[cfg(test)]
mod test {
    use {
        super::*,
        crate::{
            keys::{Key, KEY_LEN},
            testing::CallCounter,
        },
        async_trait::async_trait,
        fidl_fuchsia_identity_account::{AccountMarker, AccountProxy},
        fuchsia_fs::{directory, file, node::OpenError},
        fuchsia_zircon::Status,
        storage_manager::minfs::disk::testing::MockMinfs,
        vfs::execution_scope::ExecutionScope,
    };

    const TEST_KEY: Key = [1; KEY_LEN];
    const WRONG_KEY: Key = [2; KEY_LEN];

    /// A mock implementation of [`EncryptedBlockDevice`].
    #[derive(Debug, Clone)]
    struct MockEncryptedBlockDevice {
        /// Whether the block encrypted block device can be sealed.
        seal_behavior: Result<(), fn() -> DiskError>,

        /// The number of times [`MockEncryptedBlockDevice::seal`] was called.
        seal_call_counter: CallCounter,
    }

    impl MockEncryptedBlockDevice {
        /// Returns a new `MockEncryptedBlockDevice` that returns the supplied result on seal, and
        /// the call counter for this `MockEncryptedBlockDevice`.
        fn new_with_call_counter(
            seal_behavior: Result<(), fn() -> DiskError>,
        ) -> (CallCounter, Self) {
            let seal_call_counter = CallCounter::new(0);
            (seal_call_counter.clone(), Self { seal_behavior, seal_call_counter })
        }
    }

    impl Default for MockEncryptedBlockDevice {
        fn default() -> Self {
            Self { seal_behavior: Ok(()), seal_call_counter: CallCounter::new(0) }
        }
    }

    #[async_trait]
    impl EncryptedBlockDevice for MockEncryptedBlockDevice {
        type BlockDevice = ();

        async fn format(&self, _key: &Key) -> Result<(), DiskError> {
            unimplemented!("format should not be called");
        }

        async fn unseal(&self, _key: &Key) -> Result<(), DiskError> {
            unimplemented!("unseal should not be called");
        }

        async fn seal(&self) -> Result<(), DiskError> {
            self.seal_call_counter.increment();
            self.seal_behavior.clone().map_err(|err_factory| err_factory())
        }

        async fn shred(&self) -> Result<(), DiskError> {
            unimplemented!("shred should not be called");
        }
    }

    async fn directory_exists<EB: EncryptedBlockDevice, M: Minfs>(
        account: &Account<EB, M>,
        path: &str,
    ) -> bool {
        let lock = account.state.lock().await;
        match &*lock {
            State::Locked => panic!("Account should not be locked"),
            State::Unlocked { minfs, .. } => {
                match directory::open_directory(minfs.root_dir(), path, fio::OpenFlags::empty())
                    .await
                {
                    Ok(_) => true,
                    Err(OpenError::OpenError(Status::NOT_FOUND)) => false,
                    Err(err) => panic!("Unexpected error opening directory: {:?}", err),
                }
            }
        }
    }

    async fn serve_new_client<EB: EncryptedBlockDevice, M: Minfs>(
        account: &Arc<Account<EB, M>>,
    ) -> Result<AccountProxy, AccountError> {
        let (proxy, stream) = fidl::endpoints::create_proxy_and_stream::<AccountMarker>().unwrap();
        account.clone().handle_requests_for_stream(stream).await?;
        Ok(proxy)
    }

    #[fuchsia::test]
    async fn test_check_new_client() {
        let minfs = MockMinfs::default();
        let eb = MockEncryptedBlockDevice::default();
        let account = Arc::new(Account::new(TEST_KEY, eb, minfs));

        assert_eq!(
            account.check_new_client(&TEST_KEY).await,
            CheckNewClientResult::UnlockedSameKey
        );
        assert_eq!(
            account.check_new_client(&WRONG_KEY).await,
            CheckNewClientResult::UnlockedDifferentKey
        );
        assert!(Arc::clone(&account).lock().await.is_ok());
        assert_eq!(account.check_new_client(&TEST_KEY).await, CheckNewClientResult::Locked);
    }

    #[fuchsia::test]
    async fn test_get_data_directory() {
        let minfs = MockMinfs::default();
        let eb = MockEncryptedBlockDevice::default();
        let account = Account::new(TEST_KEY, eb, minfs);

        // The freshly created filesystem should not contain a default client directory.
        assert!(!directory_exists(&account, DEFAULT_DIR).await);

        // Get a directory.
        let (dir, dir_server_end) =
            fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
        assert_eq!(account.get_data_directory(dir_server_end).await, Ok(()));

        // The act of getting a data directory should have created the default client directory.
        assert!(directory_exists(&account, DEFAULT_DIR).await);

        // Verify the directory we got is usable.
        let file = directory::open_file(
            &dir,
            "testfile",
            fio::OpenFlags::CREATE | fio::OpenFlags::RIGHT_WRITABLE,
        )
        .await
        .expect("opening test file");
        file::write(&file, "test").await.expect("writing file");
    }

    #[fuchsia::test]
    async fn lock_account_with_multiple_concurrent_channels() {
        let scope = ExecutionScope::new();
        let (seal_call_counter, mock_encrypted_block) =
            MockEncryptedBlockDevice::new_with_call_counter(Ok(()));
        let mock_minfs = MockMinfs::simple(scope.clone());
        let account = Arc::new(Account::new([0; KEY_LEN], mock_encrypted_block, mock_minfs));

        let proxy1 = serve_new_client(&account).await.expect("serve client 1");
        let proxy2 = serve_new_client(&account).await.expect("serve client 2");

        // Check that a FIDL call can be made from both clients.
        {
            let (_, server_end) = fidl::endpoints::create_proxy().unwrap();
            proxy1
                .get_data_directory(server_end)
                .await
                .expect("get data directory FIDL 1")
                .expect("get data directory 1");
            let (_, server_end) = fidl::endpoints::create_proxy().unwrap();
            proxy2
                .get_data_directory(server_end)
                .await
                .expect("get data directory FIDL 2")
                .expect("get data directory 2");
        }

        proxy1.lock().await.expect("lock FIDL").expect("lock account");

        // Verify that the underlying block was sealed.
        assert_eq!(seal_call_counter.count(), 1);

        // Check that the clients are disconnected.
        {
            let (_, server_end) = fidl::endpoints::create_proxy().unwrap();
            proxy1.get_data_directory(server_end).await.expect_err("get data directory 1");
            let (_, server_end) = fidl::endpoints::create_proxy().unwrap();
            proxy2.get_data_directory(server_end).await.expect_err("get data directory 2");
        }

        // No new clients can be served.
        serve_new_client(&account).await.expect_err("serve client 3");

        scope.shutdown();
        scope.wait().await;
    }

    /// Test that when the zxcrypt device is already sealed, locking the account succeeds.
    /// This is not a common case, but should be handled correctly.
    #[fuchsia::test]
    async fn lock_account_succeeds_with_zxcrypt_seal_bad_state() {
        let scope = ExecutionScope::new();
        let (seal_call_counter, mock_encrypted_block) =
            MockEncryptedBlockDevice::new_with_call_counter(Err(|| {
                DiskError::FailedToSealZxcrypt(Status::BAD_STATE)
            }));
        let mock_minfs = MockMinfs::simple(scope.clone());
        let account = Arc::new(Account::new([0; KEY_LEN], mock_encrypted_block, mock_minfs));

        let proxy = serve_new_client(&account).await.expect("serve client");

        // Expect the locking to succeed, even though the zxcrypt block is already sealed.
        proxy.lock().await.expect("lock FIDL").expect("lock account");

        // Verify that `seal` was called on the underlying block.
        assert_eq!(seal_call_counter.count(), 1);

        // Check that the client isdisconnected.
        let (_, server_end) = fidl::endpoints::create_proxy().unwrap();
        proxy.get_data_directory(server_end).await.expect_err("get data directory");

        scope.shutdown();
        scope.wait().await;
    }
}
