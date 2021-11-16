// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    disk_management::{DiskError, Minfs},
    keys::Key,
};
use anyhow::Context;
use fidl::endpoints::ServerEnd;
use fidl_fuchsia_identity_account::{
    self as faccount, AccountRequest, AccountRequestStream, Lifetime,
};
use fidl_fuchsia_io::{OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE};
use futures::{lock::Mutex, prelude::*, select};
use identity_common::{TaskGroup, TaskGroupCancel, TaskGroupError};
use log::{error, warn};
use std::sync::Arc;
use thiserror::Error;

/// Serves the `fuchsia.identity.account.Account` FIDL protocol.
pub struct Account<M> {
    /// Tasks handling work on behalf of the Account are spawned in this group.
    /// The lifetime of those tasks are tied to this account.
    task_group: TaskGroup,
    /// The state of the account.
    state: Mutex<State<M>>,
}

/// The internal state of the Account.
enum State<M> {
    /// The account is sealed/encrypted.
    Sealed,
    /// The account is unsealed/unencrypted.
    Unsealed {
        /// The derived key used to unseal the partition.
        key: Key,

        /// The instance of minfs serving the filesystem.
        minfs: M,
    },
}

/// The result of the [`Account::check_new_client()`] method.
pub enum CheckNewClientResult {
    /// The [`Account`] instance has been sealed and should not be re-used.
    Sealed,
    /// The [`Account`] instance is unsealed and the keys match.
    UnsealedSameKey,
    /// The [`Account`] instance is unsealed but the keys did *NOT* match.
    UnsealedDifferentKey,
}

#[derive(Debug, Error)]
pub enum AccountError {
    #[error("Failed to spawn a task: {0}")]
    SpawnTaskError(#[source] TaskGroupError),
    #[error("Failed to cancel tasks: {0}")]
    CancelTaskGroupError(#[source] TaskGroupError),
    #[error("Failed to shutdown minfs: {0}")]
    ShutdownError(#[source] DiskError),
}

impl<M: Minfs> Account<M> {
    /// Creates a new `Account` in the unsealed state, with the `key` used to unseal the account
    /// and the instance of minfs serving the filesystem.
    pub fn new(key: Key, minfs: M) -> Self {
        Account { task_group: TaskGroup::new(), state: Mutex::new(State::Unsealed { key, minfs }) }
    }

    /// Checks whether a new client can be served by this [`Account`] instance.
    pub async fn check_new_client(&self, client_key: &Key) -> CheckNewClientResult {
        let state = self.state.lock().await;
        match &*state {
            State::Sealed => CheckNewClientResult::Sealed,
            State::Unsealed { key: account_key, .. } => {
                if client_key == account_key {
                    CheckNewClientResult::UnsealedSameKey
                } else {
                    CheckNewClientResult::UnsealedDifferentKey
                }
            }
        }
    }

    #[allow(unused)]
    pub async fn seal(self: Arc<Self>) -> Result<(), AccountError> {
        let mut state = self.state.lock().await;
        self.task_group.cancel().await.map_err(AccountError::CancelTaskGroupError)?;
        match std::mem::replace(&mut *state, State::Sealed) {
            State::Sealed => Ok(()),
            State::Unsealed { minfs, .. } => {
                minfs.shutdown().await.map_err(AccountError::ShutdownError)?;
                Ok(())
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
                    warn!("Account FIDL server canceled");
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
                responder
                    .send(&mut Err(faccount::Error::UnsupportedOperation))
                    .context("sending Lock response")?;
            }
            AccountRequest::GetDataDirectory { data_directory, responder } => {
                match &*self.state.lock().await {
                    State::Sealed => responder.send(&mut Err(faccount::Error::FailedPrecondition)),
                    State::Unsealed { minfs, .. } => {
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
