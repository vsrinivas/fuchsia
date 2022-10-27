// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! AccountMap defines the set of accounts on the current Fuchsia device.
//! It caches AccountHandlerConnectionImpls for accounts for repeat access.

use {
    crate::{
        account_handler_connection::AccountHandlerConnection,
        inspect,
        stored_account_list::{AccountMetadata, StoredAccountList},
    },
    account_common::{AccountId, AccountManagerError, ResultExt},
    anyhow::format_err,
    fidl_fuchsia_identity_account::{Error as ApiError, Lifetime},
    fuchsia_inspect::{Node, Property},
    std::{collections::BTreeMap, path::PathBuf, sync::Arc},
};

/// Type alias for the inner map type used in AccountMap.
// TODO(dnordstrom): Replace `Option` with something more flexible, perhaps
// a custom type which can cache data such as account lifetime, eliminating
// the need to open a connection.
type InnerMap<AHC> = BTreeMap<AccountId, Option<Arc<AHC>>>;

/// The AccountMap maintains adding and removing accounts, as well as opening
/// connections to their respective handlers.
pub struct AccountMap<AHC: AccountHandlerConnection> {
    /// The actual map representing the provisioned accounts on the device.
    accounts: InnerMap<AHC>,

    /// The account list and their corresponding data which is persisted on
    /// the disk.
    stored_account_list: StoredAccountList,

    /// An inspect node which reports information about the accounts on the
    /// device and whether they are currently active.
    inspect: inspect::Accounts,
}

impl<AHC: AccountHandlerConnection> AccountMap<AHC> {
    /// Load an account map from disk by providing the data directory. If the
    /// metadata file does not exist, it will be created.
    ///
    /// `data_dir`          The data directory.
    /// `inspect_parent`    An inspect node which will be the parent of the
    ///                     `Accounts` node which the account map will populate.
    pub fn load(data_dir: PathBuf, inspect_parent: &Node) -> Result<Self, AccountManagerError> {
        let mut accounts = InnerMap::new();
        let stored_account_list = StoredAccountList::load(&data_dir)?;
        for stored_account in &stored_account_list {
            accounts.insert(stored_account.account_id().clone(), None);
        }
        let inspect = inspect::Accounts::new(inspect_parent);
        let account_map = Self { accounts, stored_account_list, inspect };
        account_map.refresh_inspect();
        Ok(account_map)
    }

    /// Get an account handler connection if one exists for the account, either
    /// by returning a previously added or cached connection, or by pre-loading
    /// an account from disk and returning its connection.
    ///
    /// Returns: A reference to the account handler connection or `NotFound`
    /// error if the account does not exist.
    pub async fn get_handler<'a>(
        &'a mut self,
        account_id: &'a AccountId,
    ) -> Result<Arc<AHC>, AccountManagerError> {
        match self.accounts.get_mut(account_id) {
            None => Err(AccountManagerError::new(ApiError::NotFound)),
            Some(Some(handler)) => Ok(Arc::clone(handler)),
            Some(ref mut handler_option) => {
                let new_handler =
                    Arc::new(AHC::new(account_id.clone(), Lifetime::Persistent).await?);
                let pre_auth_state =
                    self.stored_account_list.get_account_pre_auth_state(account_id)?;
                new_handler
                    .proxy()
                    .preload(pre_auth_state)
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

    // Returns the persisted metadata for the specified account.
    pub fn get_metadata<'a>(
        &'a self,
        account_id: &'a AccountId,
    ) -> Result<&'a AccountMetadata, AccountManagerError> {
        match self.accounts.get(account_id) {
            None => Err(AccountManagerError::new(ApiError::NotFound)),
            _ => self.stored_account_list.get_metadata(account_id),
        }
    }

    /// Returns an AccountHandlerConnection for a new account. The
    /// AccountHandler is in the Uninitialized state.
    pub async fn new_handler(&self, lifetime: Lifetime) -> Result<Arc<AHC>, AccountManagerError> {
        let mut account_id = AccountId::new(rand::random::<u64>());
        // IDs are 64 bit integers that are meant to be random. Its very unlikely we'll create
        // the same one twice but not impossible.
        while self.accounts.contains_key(&account_id) {
            account_id = AccountId::new(rand::random::<u64>());
        }
        let new_handler = AHC::new(account_id.clone(), lifetime).await?;
        Ok(Arc::new(new_handler))
    }

    /// Returns all account ids in the account map.
    // TODO(dnordstrom): In the future, more complex iterators or filters may
    // supercede this method.
    pub fn get_account_ids(&self) -> Vec<AccountId> {
        self.accounts.keys().cloned().collect()
    }

    /// Add an account and its handler to the map.
    /// Also adds its `PreAuthState` to the `StoredAccountList`.
    ///
    /// Returns: `FailedPrecondition` error if an account with the provided id
    /// already exists.
    pub async fn add_account(
        &'_ mut self,
        handler: Arc<AHC>,
        pre_auth_state: Vec<u8>,
        metadata: AccountMetadata,
    ) -> Result<(), AccountManagerError> {
        let account_id = handler.get_account_id();
        if self.accounts.contains_key(account_id) {
            let cause = format_err!("Duplicate ID {:?} creating new account", &account_id);
            return Err(AccountManagerError::new(ApiError::FailedPrecondition).with_cause(cause));
        }

        // TODO(apsbhatia): Handle metadata storage for Ephemeral accounts.
        // Write to disk if account is persistent
        if handler.get_lifetime() == &Lifetime::Persistent {
            self.stored_account_list.add_account(account_id, pre_auth_state, metadata)?;
        }

        // Reflect in the map
        self.accounts.insert(account_id.clone(), Some(handler));

        // Refresh inspect
        self.refresh_inspect();
        Ok(())
    }

    /// Update the pre_auth_state for the specified account_id.
    pub async fn update_account<'a>(
        &'a mut self,
        account_id: &'a AccountId,
        pre_auth_state: Vec<u8>,
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
            self.stored_account_list.update_account(account_id, pre_auth_state)?;
        }

        Ok(())
    }

    /// Remove an account and its handler from the account map.
    ///
    /// Returns: If an account with the provided id does not exists, an
    /// `NotFound` error is returned.
    pub async fn remove_account<'a>(
        &'a mut self,
        account_id: &'a AccountId,
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
            self.stored_account_list.remove_account(account_id)?;
        }

        // Remove the handler entry
        self.accounts.remove(account_id);

        // Refresh inspect
        self.refresh_inspect();
        Ok(())
    }

    /// Update the inspect values.
    fn refresh_inspect(&self) {
        self.inspect.total.set(self.accounts.len() as u64);
        let active_count = self.accounts.values().filter(|v| v.is_some()).count();
        self.inspect.active.set(active_count as u64);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{
        fake_account_handler_connection::{
            FakeAccountHandlerConnection, CORRUPT_HANDLER_ACCOUNT_ID, UNKNOWN_ERROR_ACCOUNT_ID,
        },
        stored_account_list::StoredAccount,
    };
    use fuchsia_inspect::{assert_data_tree, Inspector};
    use lazy_static::lazy_static;
    use std::sync::Arc;
    use tempfile::TempDir;

    type TestAccountMap = AccountMap<FakeAccountHandlerConnection>;

    lazy_static! {
        static ref TEST_ACCOUNT_ID_1: AccountId = AccountId::new(123);
        static ref TEST_ACCOUNT_ID_2: AccountId = AccountId::new(456);
        static ref ACCOUNT_PRE_AUTH_STATE: Vec<u8> = vec![1, 3, 4];
        static ref ACCOUNT_METADATA: AccountMetadata = AccountMetadata::new("test".to_string());
    }

    impl<AHC: AccountHandlerConnection> AccountMap<AHC> {
        fn new(accounts: InnerMap<AHC>, data_dir: PathBuf, inspect_parent: &Node) -> Self {
            let inspect = inspect::Accounts::new(inspect_parent);
            let stored_accounts = accounts
                .keys()
                .map(|id| {
                    StoredAccount::new(
                        id.clone(),
                        ACCOUNT_PRE_AUTH_STATE.to_vec(),
                        ACCOUNT_METADATA.clone(),
                    )
                })
                .collect();
            let stored_account_list = StoredAccountList::new(&data_dir, stored_accounts);
            let account_map = Self { accounts, stored_account_list, inspect };
            account_map.refresh_inspect();
            account_map
        }

        // Only used to unit test PreAuthState persistence.
        pub async fn get_account_pre_auth_state<'a>(
            &'a self,
            account_id: &'a AccountId,
        ) -> Result<&'a Vec<u8>, AccountManagerError> {
            match self.accounts.get(account_id) {
                None => Err(AccountManagerError::new(ApiError::NotFound)),
                _ => self.stored_account_list.get_account_pre_auth_state(account_id),
            }
        }
    }

    // Run some sanity checks on an empty AccountMap
    #[fuchsia_async::run_until_stalled(test)]
    async fn empty_map() -> Result<(), AccountManagerError> {
        let data_dir = TempDir::new().unwrap();
        let inspector = Inspector::new();
        let mut map = TestAccountMap::load(data_dir.path().into(), inspector.root())?;
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
        let mut map = TestAccountMap::load(data_dir.path().into(), inspector.root())?;

        // Regular persistent account
        let conn_test = Arc::new(
            FakeAccountHandlerConnection::new_with_defaults(
                Lifetime::Persistent,
                TEST_ACCOUNT_ID_1.clone(),
            )
            .await?,
        );
        map.add_account(conn_test, ACCOUNT_PRE_AUTH_STATE.to_vec(), ACCOUNT_METADATA.clone())
            .await?;

        // An ephemeral account that will not survive a lifecycle
        let conn_ephemeral = Arc::new(
            FakeAccountHandlerConnection::new_with_defaults(
                Lifetime::Ephemeral,
                TEST_ACCOUNT_ID_2.clone(),
            )
            .await?,
        );
        map.add_account(conn_ephemeral, vec![], ACCOUNT_METADATA.clone()).await?;

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
        assert_data_tree!(inspector, root: { accounts: {
            total: 2u64,
            active: 2u64,
        }});

        std::mem::drop(map);

        // New account map loaded from the same directory.
        let inspector = Inspector::new();
        let mut map = TestAccountMap::load(data_dir.path().into(), inspector.root())?;
        assert_data_tree!(inspector, root: { accounts: {
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
        assert_eq!(
            map.get_account_pre_auth_state(&TEST_ACCOUNT_ID_1).await?,
            &*ACCOUNT_PRE_AUTH_STATE
        );
        assert_eq!(map.get_metadata(&TEST_ACCOUNT_ID_1)?, &*ACCOUNT_METADATA);
        assert_data_tree!(inspector, root: { accounts: {
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
        assert_data_tree!(inspector, root: { accounts: {
            total: 0u64,
            active: 0u64,
        }});

        std::mem::drop(map);

        let inspector = Inspector::new();
        let map = TestAccountMap::load(data_dir.path().into(), inspector.root())?;

        // Check that the removed account was persisted correctly
        assert_data_tree!(inspector, root: { accounts: {
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
        let mut map = TestAccountMap::new(accounts, data_dir.path().into(), inspector.root());
        // Initial state
        assert_data_tree!(inspector, root: { accounts: {
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
        assert_data_tree!(inspector, root: { accounts: {
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
        let mut map = TestAccountMap::new(accounts, data_dir.path().into(), inspector.root());
        // Initial state
        assert_data_tree!(inspector, root: { accounts: {
            total: 1u64,
            active: 0u64,
        }});
        let conn_test = Arc::new(
            FakeAccountHandlerConnection::new_with_defaults(
                Lifetime::Persistent,
                TEST_ACCOUNT_ID_1.clone(),
            )
            .await?,
        );

        // Cannot add duplicate account, even if it isn't currently loaded
        assert_eq!(
            map.add_account(conn_test, ACCOUNT_PRE_AUTH_STATE.to_vec(), ACCOUNT_METADATA.clone())
                .await
                .unwrap_err()
                .api_error,
            ApiError::FailedPrecondition
        );
        // Check that no spurious changes were caused
        assert_data_tree!(inspector, root: { accounts: {
            total: 1u64,
            active: 0u64,
        }});

        Ok(())
    }
}
