// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    disk_management::{DiskError, EncryptedBlockDevice, Minfs},
    keys::Key,
};
use anyhow::Context;
use fidl::endpoints::ServerEnd;
use fidl_fuchsia_identity_account::{
    self as faccount, AccountRequest, AccountRequestStream, Lifetime,
};
use fidl_fuchsia_io::{OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE};
use fuchsia_zircon::Status;
use futures::{lock::Mutex, prelude::*, select};
use identity_common::{TaskGroup, TaskGroupCancel, TaskGroupError};
use log::{error, info, warn};
use std::sync::Arc;
use thiserror::Error;

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
                // Priority #1 is to seal the encrypted block.
                // Tolerate any failures in shutting down minfs, but log them.
                if let Err(err) = minfs.shutdown().await {
                    warn!("failed to shutdown minfs cleanly: {}", err);
                }
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
                match &*self.state.lock().await {
                    State::Locked => responder.send(&mut Err(faccount::Error::FailedPrecondition)),
                    State::Unlocked { minfs, .. } => {
                        let mut result = minfs
                            .root_dir()
                            .clone(
                                OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
                                ServerEnd::new(data_directory.into_channel()),
                            )
                            .map_err(|_| faccount::Error::Internal);
                        responder.send(&mut result)
                    }
                }
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
mod tests {
    use super::*;
    use crate::disk_management::MockMinfs;
    use crate::testing::CallCounter;
    use async_trait::async_trait;
    use fidl_fuchsia_identity_account::{AccountMarker, AccountProxy};
    use vfs::execution_scope::ExecutionScope;

    /// A mock implementation of [`EncryptedBlockDevice`].
    #[derive(Debug, Clone)]
    struct MockEncryptedBlockDevice {
        // Whether the block encrypted block device can be sealed.
        seal_behavior: Result<(), fn() -> DiskError>,

        // The number of times [`MockEncryptedBlockDevice::seal`] was called.
        seal_call_counter: CallCounter,
    }

    impl MockEncryptedBlockDevice {
        fn new_with_call_counter(
            seal_behavior: Result<(), fn() -> DiskError>,
        ) -> (CallCounter, Self) {
            let seal_call_counter = CallCounter::new(0);
            (seal_call_counter.clone(), Self { seal_behavior, seal_call_counter })
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
    }

    async fn serve_new_client<EB: EncryptedBlockDevice, M: Minfs>(
        account: &Arc<Account<EB, M>>,
    ) -> Result<AccountProxy, AccountError> {
        let (proxy, stream) = fidl::endpoints::create_proxy_and_stream::<AccountMarker>().unwrap();
        account.clone().handle_requests_for_stream(stream).await?;
        Ok(proxy)
    }

    #[fuchsia::test]
    async fn lock_account_with_multiple_concurrent_channels() {
        let scope = ExecutionScope::new();
        let (seal_call_counter, mock_encrypted_block) =
            MockEncryptedBlockDevice::new_with_call_counter(Ok(()));
        let mock_minfs = MockMinfs::simple(scope.clone());
        let account = Arc::new(Account::new([0; 32], mock_encrypted_block, mock_minfs));

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
        let account = Arc::new(Account::new([0; 32], mock_encrypted_block, mock_minfs));

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
