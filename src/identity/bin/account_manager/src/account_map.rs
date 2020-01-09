// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! AccountMap defines the set of accounts on the current Fuchsia device.
//! It caches AccountHandlerConnectionImpls for accounts for repeat access.

use account_common::{AccountManagerError, LocalAccountId, ResultExt};
use anyhow::format_err;
use fidl_fuchsia_identity_account::{Error as ApiError, Lifetime};
use fuchsia_inspect::{Node, Property};
use std::collections::BTreeMap;
use std::path::PathBuf;
use std::sync::Arc;

use crate::account_handler_connection::AccountHandlerConnection;
use crate::account_handler_context::AccountHandlerContext;
use crate::inspect;
use crate::stored_account_list::{StoredAccountList, StoredAccountMetadata};

/// Type alias for the inner map type used in AccountMap.
// TODO(dnordstrom): Replace `Option` with something more flexible, perhaps
// a custom type which can cache data such as account lifetime, eliminating
// the need to open a connection.
type InnerMap<AHC> = BTreeMap<LocalAccountId, Option<Arc<AHC>>>;

/// The AccountMap maintains adding and removing accounts, as well as opening
/// connections to their respective handlers.
pub struct AccountMap<AHC: AccountHandlerConnection> {
    /// The actual map representing the provisioned accounts on the device.
    accounts: InnerMap<AHC>,

    /// The directory where the account list metadata resides. The metadata may
    /// be both read and written during the lifetime of the account map.
    data_dir: PathBuf,

    /// The AccountHandlerContext which is used to provide contextual
    /// information to account handlers spawned by the account map.
    context: Arc<AccountHandlerContext>,

    /// An inspect node which reports information about the accounts on the
    /// device and whether they are currently active.
    inspect: inspect::Accounts,
}

impl<AHC: AccountHandlerConnection> AccountMap<AHC> {
    /// Load an account map from disk by providing the data directory. If the
    /// metadata file does not exist, it will be created.
    ///
    /// `data_dir`          The data directory.
    /// `context`           An AccountHandlerContext which is used to create
    ///                     AccountHandlerConnections.
    /// `inspect_parent`    An inspect node which will be the parent of the
    ///                     `Accounts` node which the account map will populate.
    pub fn load(
        data_dir: PathBuf,
        context: Arc<AccountHandlerContext>,
        inspect_parent: &Node,
    ) -> Result<Self, AccountManagerError> {
        let mut accounts = InnerMap::new();
        let stored_account_list = StoredAccountList::load(&data_dir)?;
        for stored_account in stored_account_list.accounts().into_iter() {
            accounts.insert(stored_account.account_id().clone(), None);
        }
        Ok(Self::new(accounts, data_dir, context, inspect_parent))
    }

    fn new(
        accounts: InnerMap<AHC>,
        data_dir: PathBuf,
        context: Arc<AccountHandlerContext>,
        inspect_parent: &Node,
    ) -> Self {
        let inspect = inspect::Accounts::new(inspect_parent);
        let account_map = Self { accounts, data_dir, context, inspect };
        account_map.refresh_inspect();
        account_map
    }

    /// Get an account handler connection if one exists for the account, either
    /// by returning a previously added or cached connection, or by pre-loading
    /// an account from disk and returning its connection.
    ///
    /// Returns: A reference to the account handler connection or `NotFound`
    /// error if the account does not exist.
    pub async fn get_handler<'a>(
        &'a mut self,
        account_id: &'a LocalAccountId,
    ) -> Result<Arc<AHC>, AccountManagerError> {
        match self.accounts.get_mut(account_id) {
            None => Err(AccountManagerError::new(ApiError::NotFound)),
            Some(Some(handler)) => Ok(Arc::clone(handler)),
            Some(ref mut handler_option) => {
                let new_handler = Arc::new(AHC::new(
                    account_id.clone(),
                    Lifetime::Persistent,
                    Arc::clone(&self.context),
                )?);
                new_handler
                    .proxy()
                    .preload()
                    .await
                    .account_manager_error(ApiError::Resource)?
                    .map_err(|err| {
                        AccountManagerError::new(err)
                            .with_cause(format_err!("Error loading existing account"))
                    })?;
                handler_option.replace(Arc::clone(&new_handler));
                self.refresh_inspect();
                Ok(new_handler)
            }
        }
    }

    /// Returns an AccountHandlerConnection for a new account. The
    /// AccountHandler is in the Uninitialized state.
    pub fn new_handler(&self, lifetime: Lifetime) -> Result<Arc<AHC>, AccountManagerError> {
        let mut account_id = LocalAccountId::new(rand::random::<u64>());
        // IDs are 64 bit integers that are meant to be random. Its very unlikely we'll create
        // the same one twice but not impossible.
        while self.accounts.contains_key(&account_id) {
            account_id = LocalAccountId::new(rand::random::<u64>());
        }
        let new_handler = AHC::new(account_id.clone(), lifetime, Arc::clone(&self.context))?;
        Ok(Arc::new(new_handler))
    }

    /// Returns all account ids in the account map.
    // TODO(dnordstrom): In the future, more complex iterators or filters may
    // supercede this method.
    pub fn get_account_ids(&self) -> Vec<LocalAccountId> {
        self.accounts.keys().map(|id| id.clone().into()).collect()
    }

    /// Add an account and its handler to the map.
    ///
    /// Returns: `FailedPrecondition` error if an account with the provided id
    /// already exists.
    pub async fn add_account<'a>(
        &'a mut self,
        handler: Arc<AHC>,
    ) -> Result<(), AccountManagerError> {
        let account_id = handler.get_account_id();
        if self.accounts.contains_key(account_id) {
            let cause = format_err!("Duplicate ID {:?} creating new account", &account_id);
            return Err(AccountManagerError::new(ApiError::FailedPrecondition).with_cause(cause));
        }

        // Write to disk if account is persistent
        if handler.get_lifetime() == &Lifetime::Persistent {
            let mut account_ids = self.get_persistent_account_metadata(None);
            account_ids.push(StoredAccountMetadata::new(account_id.clone()));
            StoredAccountList::new(account_ids).save(&self.data_dir)?;
        }

        // Reflect in the map
        self.accounts.insert(account_id.clone(), Some(handler));

        // Refresh inspect
        self.refresh_inspect();
        Ok(())
    }

    /// Remove an account and its handler from the account map.
    ///
    /// Returns: If an account with the provided id does not exists, an
    /// `NotFound` error is returned.
    pub async fn remove_account<'a>(
        &'a mut self,
        account_id: &'a LocalAccountId,
    ) -> Result<(), AccountManagerError> {
        let is_persistent = match self.accounts.get(account_id) {
            Some(Some(handler)) => handler.get_lifetime() == &Lifetime::Persistent,

            // An ephemeral account always has a handler during its time in the
            // map; hence an account without a handler must be persistent.
            Some(None) => true,
            None => return Err(AccountManagerError::new(ApiError::NotFound)),
        };

        // Persist to disk if persistent
        if is_persistent {
            let account_ids = self.get_persistent_account_metadata(Some(&account_id));
            StoredAccountList::new(account_ids).save(&self.data_dir)?;
        }

        // Remove the handler entry
        self.accounts.remove(account_id);

        // Refresh inspect
        self.refresh_inspect();
        Ok(())
    }

    /// Get a vector of StoredAccountMetadata for all persistent accounts in the account map,
    /// optionally excluding the provided |exclude_account_id|.
    fn get_persistent_account_metadata<'a>(
        &'a self,
        exclude_account_id: Option<&'a LocalAccountId>,
    ) -> Vec<StoredAccountMetadata> {
        self.accounts
            .iter()
            .filter(|(id, handler)| {
                // Filter out `exclude_account_id` if provided
                exclude_account_id.map_or(true, |exclude_id| id != &exclude_id) &&
                // Filter out accounts that are not persistent. Note that all accounts that do not
                // have an open handler are assumed to be persistent due to the semantics of
                // account lifetimes in this module.
                handler.as_ref().map_or(true, |h| h.get_lifetime() == &Lifetime::Persistent)
            })
            .map(|(id, _)| StoredAccountMetadata::new(id.clone()))
            .collect()
    }

    /// Update the inspect values.
    fn refresh_inspect<'a>(&'a self) {
        self.inspect.total.set(self.accounts.len() as u64);
        let active_count = self.accounts.values().filter(|v| v.is_some()).count();
        self.inspect.active.set(active_count as u64);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::fake_account_handler_connection::{
        FakeAccountHandlerConnection, CORRUPT_HANDLER_ACCOUNT_ID, UNKNOWN_ERROR_ACCOUNT_ID,
    };
    use fuchsia_inspect::{assert_inspect_tree, Inspector};
    use lazy_static::lazy_static;
    use std::sync::Arc;
    use tempfile::TempDir;

    type TestAccountMap = AccountMap<FakeAccountHandlerConnection>;

    lazy_static! {
        static ref TEST_ACCOUNT_ID_1: LocalAccountId = LocalAccountId::new(123);
        static ref TEST_ACCOUNT_ID_2: LocalAccountId = LocalAccountId::new(456);
        static ref ACCOUNT_HANDLER_CONTEXT: Arc<AccountHandlerContext> =
            Arc::new(AccountHandlerContext::new(&[], &[]));
    }

    // Run some sanity checks on an empty AccountMap
    #[fuchsia_async::run_until_stalled(test)]
    async fn empty_map() -> Result<(), AccountManagerError> {
        let data_dir = TempDir::new().unwrap();
        let inspector = Inspector::new();
        let mut map = TestAccountMap::load(
            data_dir.path().into(),
            ACCOUNT_HANDLER_CONTEXT.clone(),
            inspector.root(),
        )?;
        assert_eq!(map.get_account_ids(), vec![]);
        assert_eq!(
            map.get_handler(&TEST_ACCOUNT_ID_1).await.unwrap_err().api_error,
            ApiError::NotFound
        );
        assert_eq!(
            map.remove_account(&TEST_ACCOUNT_ID_1).await.unwrap_err().api_error,
            ApiError::NotFound
        );
        Ok(())
    }

    // Add some accounts to an empty AccountMap, re-initialize a new AccountMap
    // from the same disk location, then check that persistent accounts were
    // survived the life cycle and can be loaded. Finally remove an active
    // account and verify that it was persisted.
    #[fuchsia_async::run_until_stalled(test)]
    async fn check_persisted() -> Result<(), AccountManagerError> {
        let data_dir = TempDir::new().unwrap();
        let inspector = Inspector::new();
        let mut map = TestAccountMap::load(
            data_dir.path().into(),
            ACCOUNT_HANDLER_CONTEXT.clone(),
            inspector.root(),
        )?;

        // Regular persistent account
        let conn_test = Arc::new(FakeAccountHandlerConnection::new_with_defaults(
            Lifetime::Persistent,
            TEST_ACCOUNT_ID_1.clone(),
        )?);
        map.add_account(conn_test).await?;

        // An ephemeral account that will not survive a lifecycle
        let conn_ephemeral = Arc::new(FakeAccountHandlerConnection::new_with_defaults(
            Lifetime::Ephemeral,
            TEST_ACCOUNT_ID_2.clone(),
        )?);
        map.add_account(conn_ephemeral).await?;

        // All accounts should be available in this lifecycle
        assert_eq!(
            map.get_handler(&TEST_ACCOUNT_ID_1).await?.get_account_id(),
            &*TEST_ACCOUNT_ID_1
        );
        // Bonus: we can get the same handler multiple times
        for _ in 0..2 {
            assert_eq!(
                map.get_handler(&TEST_ACCOUNT_ID_2).await?.get_account_id(),
                &*TEST_ACCOUNT_ID_2
            );
        }
        assert_inspect_tree!(inspector, root: { accounts: {
            total: 2u64,
            active: 2u64,
        }});

        std::mem::drop(map);

        // New account map loaded from the same directory.
        let inspector = Inspector::new();
        let mut map = TestAccountMap::load(
            data_dir.path().into(),
            ACCOUNT_HANDLER_CONTEXT.clone(),
            inspector.root(),
        )?;
        assert_inspect_tree!(inspector, root: { accounts: {
            total: 1u64,
            active: 0u64,
        }});

        // The ephemeral account did not survive the life cycle
        assert_eq!(
            map.get_handler(&TEST_ACCOUNT_ID_2).await.unwrap_err().api_error,
            ApiError::NotFound
        );

        // The persistent account survived the life cycle
        assert_eq!(
            map.get_handler(&TEST_ACCOUNT_ID_1).await?.get_account_id(),
            &*TEST_ACCOUNT_ID_1
        );
        assert_inspect_tree!(inspector, root: { accounts: {
            total: 1u64,
            active: 1u64,
        }});

        // Remove an active account
        map.remove_account(&TEST_ACCOUNT_ID_1).await?;

        // Fails a second time
        assert_eq!(
            map.remove_account(&TEST_ACCOUNT_ID_1).await.unwrap_err().api_error,
            ApiError::NotFound
        );
        assert_inspect_tree!(inspector, root: { accounts: {
            total: 0u64,
            active: 0u64,
        }});

        std::mem::drop(map);

        let inspector = Inspector::new();
        let map = TestAccountMap::load(
            data_dir.path().into(),
            ACCOUNT_HANDLER_CONTEXT.clone(),
            inspector.root(),
        )?;

        // Check that the removed account was persisted correctly
        assert_inspect_tree!(inspector, root: { accounts: {
            total: 0u64,
            active: 0u64,
        }});

        // Sanity check that there are no remaining accounts
        assert_eq!(map.get_account_ids(), vec![]);

        Ok(())
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn load_errors() -> Result<(), AccountManagerError> {
        let data_dir = TempDir::new().unwrap();
        let inspector = Inspector::new();
        let mut accounts = InnerMap::new();
        accounts.insert(CORRUPT_HANDLER_ACCOUNT_ID.clone(), None);
        accounts.insert(UNKNOWN_ERROR_ACCOUNT_ID.clone(), None);
        let mut map = TestAccountMap::new(
            accounts,
            data_dir.path().into(),
            ACCOUNT_HANDLER_CONTEXT.clone(),
            inspector.root(),
        );
        // Initial state
        assert_inspect_tree!(inspector, root: { accounts: {
            total: 2u64,
            active: 0u64,
        }});

        // The corrupt account is present but the connection cannot be created
        assert_eq!(
            map.get_handler(&CORRUPT_HANDLER_ACCOUNT_ID).await.unwrap_err().api_error,
            ApiError::Resource
        );

        // The account is present but returns an error when loaded. The error code
        // on the AccountHandler init method is preserved in the error returned by
        // AccountMap.get_handler().
        assert_eq!(
            map.get_handler(&UNKNOWN_ERROR_ACCOUNT_ID).await.unwrap_err().api_error,
            ApiError::Unknown
        );

        // Check that no spurious changes were caused
        assert_inspect_tree!(inspector, root: { accounts: {
            total: 2u64,
            active: 0u64,
        }});

        Ok(())
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn add_duplicate() -> Result<(), AccountManagerError> {
        let data_dir = TempDir::new().unwrap();
        let inspector = Inspector::new();
        let mut accounts = InnerMap::new();
        accounts.insert(TEST_ACCOUNT_ID_1.clone(), None);
        let mut map = TestAccountMap::new(
            accounts,
            data_dir.path().into(),
            ACCOUNT_HANDLER_CONTEXT.clone(),
            inspector.root(),
        );
        // Initial state
        assert_inspect_tree!(inspector, root: { accounts: {
            total: 1u64,
            active: 0u64,
        }});
        let conn_test = Arc::new(FakeAccountHandlerConnection::new_with_defaults(
            Lifetime::Persistent,
            TEST_ACCOUNT_ID_1.clone(),
        )?);

        // Cannot add duplicate account, even if it isn't currently loaded
        assert_eq!(
            map.add_account(conn_test).await.unwrap_err().api_error,
            ApiError::FailedPrecondition
        );
        // Check that no spurious changes were caused
        assert_inspect_tree!(inspector, root: { accounts: {
            total: 1u64,
            active: 0u64,
        }});

        Ok(())
    }
}
