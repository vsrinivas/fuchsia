// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        diagnostics::{Diagnostics, IncomingMethod},
        hash_tree::{HashTree, HashTreeStorage, LABEL_LENGTH},
        label_generator::Label,
        lookup_table::LookupTable,
        pinweaver::{CredentialMetadata, Hash, Mac, PinWeaverProtocol},
    },
    anyhow::{anyhow, Context, Error},
    fidl_fuchsia_identity_credential::{
        self as fcred, CredentialError, CredentialManagerRequest, CredentialManagerRequestStream,
    },
    fidl_fuchsia_tpm_cr50::TryAuthResponse,
    futures::{lock::Mutex, prelude::*},
    log::error,
    std::cell::{RefCell, RefMut},
    std::sync::Arc,
};

/// The |CredentialManager| is responsible for adding, removing and checking
/// credentials. It communicates over the |PinWeaverProxy| to the `cr50_agent`
/// to seal and unseal credentials. The |CredentialManager| also has internal
/// bookkeeping for the wrapped credential data which it stores in the
/// |lookup_table| (a persistent atomic data store). In addition to this it must
/// maintain a |hash_tree| which contains the merkle tree required for
/// communicating over the PinWeaver protocol. The |hash_tree| is synced to
/// disk after each operation.
pub struct CredentialManager<PW, LT, HS, D>
where
    PW: PinWeaverProtocol,
    LT: LookupTable,
    HS: HashTreeStorage,
    D: Diagnostics,
{
    pinweaver: Mutex<PW>,
    hash_tree: RefCell<HashTree>,
    lookup_table: RefCell<LT>,
    hash_tree_storage: HS,
    diagnostics: Arc<D>,
}

impl<PW, LT, HS, D> CredentialManager<PW, LT, HS, D>
where
    PW: PinWeaverProtocol,
    LT: LookupTable,
    HS: HashTreeStorage,
    D: Diagnostics,
{
    /// Constructs a new |CredentialManager| that communicates with the |PinWeaverProxy|
    /// to add, delete and check credentials storing the relevant data in the |hash_tree|
    /// and |lookup_table|.
    pub async fn new(
        pinweaver: PW,
        hash_tree: HashTree,
        lookup_table: LT,
        hash_tree_storage: HS,
        diagnostics: Arc<D>,
    ) -> Self {
        Self {
            pinweaver: Mutex::new(pinweaver),
            hash_tree: RefCell::new(hash_tree),
            lookup_table: RefCell::new(lookup_table),
            hash_tree_storage,
            diagnostics,
        }
    }

    /// Serially process a stream of incoming CredentialManager FIDL requests.
    pub async fn handle_requests_for_stream(
        &self,
        mut request_stream: CredentialManagerRequestStream,
    ) {
        while let Some(request) = request_stream.try_next().await.expect("read request") {
            self.handle_request(request)
                .unwrap_or_else(|e| {
                    error!("error handling fidl request: {:#}", anyhow!(e));
                })
                .await
        }
    }

    /// Process a single CredentialManager FIDL request and send a reply.
    /// This request can either add, remove or check a credential. It is important
    /// that only one request is processed at a time as the |pinweaver| protocol
    /// can only handle communicating with a single object.
    async fn handle_request(&self, request: CredentialManagerRequest) -> Result<(), Error> {
        match request {
            CredentialManagerRequest::AddCredential { params, responder } => {
                let mut resp = self.add_credential(&params).await;
                responder.send(&mut resp).context("sending AddCredential response")?;
                self.diagnostics.incoming_outcome(IncomingMethod::AddCredential, resp.map(|_| ()));
            }
            CredentialManagerRequest::RemoveCredential { label, responder } => {
                let mut resp = self.remove_credential(label).await;
                responder.send(&mut resp).context("sending RemoveLabel response")?;
                self.diagnostics.incoming_outcome(IncomingMethod::RemoveCredential, resp);
            }
            CredentialManagerRequest::CheckCredential { params, responder } => {
                let mut resp = self.check_credential(&params).await;
                responder.send(&mut resp).context("sending CheckCredential response")?;
                self.diagnostics
                    .incoming_outcome(IncomingMethod::CheckCredential, resp.map(|_| ()));
            }
        }
        Ok(())
    }

    /// AddCredential adds a new credential specified in |params| to the cr50
    /// and the |hash_tree|. After passing the new credential to the cr50
    /// through the |pinweaver| interface the credential is stored in
    /// the |lookup_table| and the |hash_tree| is updated and written back
    /// to disk.
    async fn add_credential(
        &self,
        params: &fcred::AddCredentialParams,
    ) -> Result<u64, CredentialError> {
        let pinweaver = self.pinweaver.lock().await;
        let (label, h_aux) = self.alloc_credential().await?;
        let (mac, cred_metadata) = pinweaver.insert_leaf(&label, h_aux, params).await?;
        self.update_credential(&label, mac, cred_metadata).await?;
        Ok(label.value())
    }

    /// CheckCredential attempts to authenticate a credential. It checks whether
    /// the |le_secret| for a given |label| is correct.
    /// On success the |he_secret| is returned along with the internal
    /// state of the |hash_tree| and |lookup_table| being updated.
    /// On authentication failure the internal state of the |hash_tree| and
    /// |lookup_table| is updated to indicate an attempt.
    /// On timeout failure an error is returned and no state is updated.
    async fn check_credential(
        &self,
        params: &fcred::CheckCredentialParams,
    ) -> Result<fcred::CheckCredentialResponse, CredentialError> {
        let pinweaver = self.pinweaver.lock().await;
        let label =
            Label::leaf_label(params.label.ok_or(CredentialError::InternalError)?, LABEL_LENGTH);
        let (h_aux, stored_cred_metadata) = self.get_credential(&label).await?;
        let le_secret = params.le_secret.as_ref().ok_or(CredentialError::InternalError)?;

        match pinweaver.try_auth(le_secret, h_aux, stored_cred_metadata).await? {
            TryAuthResponse::Success(response) => {
                let mac = response.mac.ok_or(CredentialError::InternalError)?;
                let cred_metadata = response.cred_metadata.ok_or(CredentialError::InternalError)?;
                self.update_credential(&label, mac, cred_metadata).await?;
                Ok(fcred::CheckCredentialResponse {
                    he_secret: response.he_secret,
                    ..fcred::CheckCredentialResponse::EMPTY
                })
            }
            TryAuthResponse::Failed(response) => {
                let mac = response.mac.ok_or(CredentialError::InternalError)?;
                let cred_metadata = response.cred_metadata.ok_or(CredentialError::InternalError)?;
                self.update_credential(&label, mac, cred_metadata).await?;
                Err(CredentialError::InvalidSecret)
            }
            TryAuthResponse::RateLimited(_) => Err(CredentialError::TooManyAttempts),
            _ => Err(CredentialError::InternalError),
        }
    }

    /// Attempts to remove a credential specified by the |label| in both the
    /// cr50 state and in the internal |hash_tree|. Returns nothing on success.
    async fn remove_credential(&self, label: u64) -> Result<(), CredentialError> {
        let pinweaver = self.pinweaver.lock().await;
        let label = Label::leaf_label(label, LABEL_LENGTH);
        let h_aux = self
            .hash_tree()
            .get_auxiliary_hashes_flattened(&label)
            .map_err(|_| CredentialError::InvalidLabel)?;
        let mac = self
            .hash_tree()
            .get_leaf_hash(&label)
            .map_err(|_| CredentialError::InvalidLabel)?
            .clone();
        pinweaver.remove_leaf(&label, mac, h_aux).await?;
        self.lookup_table().delete(&label).await.map_err(|_| CredentialError::InternalError)?;
        self.hash_tree().delete_leaf(&label).map_err(|_| CredentialError::InternalError)?;
        self.hash_tree_storage
            .store(&self.hash_tree.borrow())
            .map_err(|_| CredentialError::InternalError)?;
        Ok(())
    }

    /// Allocates a new empty credential in the |hash_tree| returning the
    /// leaf |label| and the auxiliary hashes |h_aux| from the leaf through to
    /// the root of the tree.
    async fn alloc_credential(&self) -> Result<(Label, Vec<Hash>), CredentialError> {
        let label =
            self.hash_tree().get_free_leaf_label().map_err(|_| CredentialError::NoFreeLabel)?;
        let h_aux = self
            .hash_tree()
            .get_auxiliary_hashes_flattened(&label)
            .map_err(|_| CredentialError::InternalError)?;
        Ok((label, h_aux))
    }

    /// Attempts to retrieve the auxiliary hashes |h_aux| and the
    /// |credential_metadata| associated with the |label| from the |hash_tree|
    /// and |lookup_table| respectively.
    async fn get_credential(
        &self,
        label: &Label,
    ) -> Result<(Vec<Hash>, CredentialMetadata), CredentialError> {
        let h_aux = self
            .hash_tree()
            .get_auxiliary_hashes_flattened(&label)
            .map_err(|_| CredentialError::InvalidLabel)?;
        let stored_cred_metadata = self
            .lookup_table()
            .read(&label)
            .await
            .map_err(|_| CredentialError::InternalError)?
            .bytes;
        Ok((h_aux, stored_cred_metadata))
    }

    /// Updates an already existing credential in both the |hash_tree| by
    /// updating its new |mac| value and inside the |lookup_table| by updating
    /// the credential metadata.
    async fn update_credential(
        &self,
        label: &Label,
        mac: Mac,
        cred_metadata: CredentialMetadata,
    ) -> Result<(), CredentialError> {
        self.hash_tree()
            .update_leaf_hash(&label, mac)
            .map_err(|_| CredentialError::CorruptedMetadata)?;
        self.lookup_table()
            .write(&label, cred_metadata)
            .await
            .map_err(|_| CredentialError::InternalError)?;
        self.hash_tree_storage
            .store(&self.hash_tree.borrow())
            .map_err(|_| CredentialError::InternalError)?;
        Ok(())
    }

    /// Convenience function that returns a RefMut to the |hash_tree|.
    fn hash_tree(&self) -> RefMut<'_, HashTree> {
        self.hash_tree.borrow_mut()
    }

    /// Convenience function that returns a Refmut to the |lookup_table|.
    fn lookup_table(&self) -> RefMut<'_, LT> {
        self.lookup_table.borrow_mut()
    }
}

#[cfg(test)]
mod test {
    use {
        super::*,
        crate::{
            diagnostics::{Event, FakeDiagnostics},
            hash_tree::{HashTreeStorageCbor, CHILDREN_PER_NODE, TREE_HEIGHT},
            lookup_table::{LookupTableError, MockLookupTable, ReadResult},
            pinweaver::MockPinWeaverProtocol,
        },
        assert_matches::assert_matches,
        fidl::endpoints::create_proxy_and_stream,
        fidl_fuchsia_identity_credential::CredentialManagerMarker,
        fidl_fuchsia_tpm_cr50::{TryAuthFailed, TryAuthRateLimited, TryAuthSuccess},
        fuchsia_async as fasync,
        tempfile::TempDir,
    };

    struct TestParams {
        pub lookup_table: MockLookupTable,
        pub pinweaver: MockPinWeaverProtocol,
        pub hash_tree: HashTree,
        pub dir: TempDir,
    }

    impl TestParams {
        fn default() -> Self {
            let dir = TempDir::new().unwrap();
            let lookup_table = MockLookupTable::new();
            let pinweaver = MockPinWeaverProtocol::new();
            let hash_tree = HashTree::new(TREE_HEIGHT, CHILDREN_PER_NODE).unwrap();
            Self { lookup_table, pinweaver, hash_tree, dir }
        }
    }

    struct TestHarness {
        cm: CredentialManager<
            MockPinWeaverProtocol,
            MockLookupTable,
            HashTreeStorageCbor,
            FakeDiagnostics,
        >,
        diag: Arc<FakeDiagnostics>,
        _dir: TempDir,
    }

    impl TestHarness {
        async fn create(params: TestParams) -> Self {
            let path = params.dir.path().join("hash_tree");
            let diag = Arc::new(FakeDiagnostics::new());
            let hash_tree_storage = HashTreeStorageCbor::new(path.to_str().unwrap());
            let cm = CredentialManager::new(
                params.pinweaver,
                params.hash_tree,
                params.lookup_table,
                hash_tree_storage,
                Arc::clone(&diag),
            )
            .await;
            Self { cm, diag, _dir: params.dir }
        }
    }

    #[fuchsia::test]
    async fn test_create_credential_manager() {
        let params = TestParams::default();
        TestHarness::create(params).await;
    }

    #[fuchsia::test]
    async fn test_add_credential() {
        let mut params = TestParams::default();
        params
            .pinweaver
            .expect_insert_leaf()
            .times(1)
            .returning(|_, _, _| Ok((Mac::default(), CredentialMetadata::default())));
        params.lookup_table.expect_write().times(1).returning(|_, _| Ok(()));
        let test = TestHarness::create(params).await;
        test.cm
            .add_credential(&fcred::AddCredentialParams {
                le_secret: Some(vec![1; 32]),
                he_secret: Some(vec![2; 32]),
                reset_secret: Some(vec![3; 32]),
                delay_schedule: Some(vec![fcred::DelayScheduleEntry {
                    attempt_count: 20,
                    time_delay: 64,
                }]),
                ..fcred::AddCredentialParams::EMPTY
            })
            .await
            .expect("added credential");
    }

    #[fuchsia::test]
    async fn test_add_credential_write_fail() {
        let mut params = TestParams::default();
        params
            .pinweaver
            .expect_insert_leaf()
            .times(1)
            .returning(|_, _, _| Ok((Mac::default(), CredentialMetadata::default())));
        params
            .lookup_table
            .expect_write()
            .times(1)
            .returning(|_, _| Err(LookupTableError::Unknown));
        let test = TestHarness::create(params).await;
        let result = test
            .cm
            .add_credential(&fcred::AddCredentialParams {
                le_secret: Some(vec![1; 32]),
                he_secret: Some(vec![2; 32]),
                reset_secret: Some(vec![3; 32]),
                delay_schedule: Some(vec![fcred::DelayScheduleEntry {
                    attempt_count: 20,
                    time_delay: 64,
                }]),
                ..fcred::AddCredentialParams::EMPTY
            })
            .await;
        assert_matches!(result, Err(CredentialError::InternalError));
    }

    #[fuchsia::test]
    async fn test_add_credential_pinweaver_failed() {
        let mut params = TestParams::default();
        params
            .pinweaver
            .expect_insert_leaf()
            .times(1)
            .returning(|_, _, _| Err(CredentialError::InternalError));
        let test = TestHarness::create(params).await;
        let result = test
            .cm
            .add_credential(&fcred::AddCredentialParams {
                le_secret: Some(vec![1; 32]),
                he_secret: Some(vec![2; 32]),
                reset_secret: Some(vec![3; 32]),
                delay_schedule: Some(vec![fcred::DelayScheduleEntry {
                    attempt_count: 20,
                    time_delay: 64,
                }]),
                ..fcred::AddCredentialParams::EMPTY
            })
            .await;
        assert_matches!(result, Err(CredentialError::InternalError));
    }

    #[fuchsia::test]
    async fn test_check_credential() {
        let mut params = TestParams::default();
        params
            .pinweaver
            .expect_insert_leaf()
            .times(1)
            .returning(|_, _, _| Ok((Mac::default(), CredentialMetadata::default())));
        params.pinweaver.expect_try_auth().times(1).returning(|_, _, _| {
            Ok(TryAuthResponse::Success(TryAuthSuccess {
                root_hash: Some(Hash::default()),
                he_secret: Some(vec![2; 32]),
                reset_secret: Some(vec![3; 32]),
                cred_metadata: Some(vec![4; 32]),
                mac: Some(Hash::default()),
                ..TryAuthSuccess::EMPTY
            }))
        });

        params.lookup_table.expect_write().times(2).returning(|_, _| Ok(()));
        params
            .lookup_table
            .expect_read()
            .times(1)
            .returning(|_| Ok(ReadResult { bytes: vec![2; 32], version: 1 }));
        let test = TestHarness::create(params).await;
        let label = test
            .cm
            .add_credential(&fcred::AddCredentialParams {
                le_secret: Some(vec![1; 32]),
                he_secret: Some(vec![2; 32]),
                reset_secret: Some(vec![3; 32]),
                delay_schedule: Some(vec![fcred::DelayScheduleEntry {
                    attempt_count: 20,
                    time_delay: 64,
                }]),
                ..fcred::AddCredentialParams::EMPTY
            })
            .await
            .expect("added credential");
        test.cm
            .check_credential(&fcred::CheckCredentialParams {
                label: Some(label),
                le_secret: Some(vec![1; 32]),
                ..fcred::CheckCredentialParams::EMPTY
            })
            .await
            .expect("check credential");
    }

    #[fuchsia::test]
    async fn test_check_credential_failed_rate_limited() {
        let mut params = TestParams::default();
        params
            .pinweaver
            .expect_insert_leaf()
            .times(1)
            .returning(|_, _, _| Ok((Mac::default(), CredentialMetadata::default())));
        params.pinweaver.expect_try_auth().times(1).returning(|_, _, _| {
            Ok(TryAuthResponse::RateLimited(TryAuthRateLimited {
                time_to_wait: Some(32),
                ..TryAuthRateLimited::EMPTY
            }))
        });

        params.lookup_table.expect_write().times(1).returning(|_, _| Ok(()));
        params
            .lookup_table
            .expect_read()
            .times(1)
            .returning(|_| Ok(ReadResult { bytes: vec![2; 32], version: 1 }));
        let test = TestHarness::create(params).await;
        let label = test
            .cm
            .add_credential(&fcred::AddCredentialParams {
                le_secret: Some(vec![1; 32]),
                he_secret: Some(vec![2; 32]),
                reset_secret: Some(vec![3; 32]),
                delay_schedule: Some(vec![fcred::DelayScheduleEntry {
                    attempt_count: 20,
                    time_delay: 64,
                }]),
                ..fcred::AddCredentialParams::EMPTY
            })
            .await
            .expect("added credential");
        let result = test
            .cm
            .check_credential(&fcred::CheckCredentialParams {
                label: Some(label),
                le_secret: Some(vec![1; 32]),
                ..fcred::CheckCredentialParams::EMPTY
            })
            .await;
        assert_matches!(result, Err(CredentialError::TooManyAttempts));
    }

    #[fuchsia::test]
    async fn test_check_credential_invalid_secret() {
        let mut params = TestParams::default();
        params
            .pinweaver
            .expect_insert_leaf()
            .times(1)
            .returning(|_, _, _| Ok((Mac::default(), CredentialMetadata::default())));
        params.pinweaver.expect_try_auth().times(1).returning(|_, _, _| {
            Ok(TryAuthResponse::Failed(TryAuthFailed {
                root_hash: Some(Hash::default()),
                cred_metadata: Some(vec![4; 32]),
                mac: Some(Hash::default()),
                ..TryAuthFailed::EMPTY
            }))
        });

        params.lookup_table.expect_write().times(2).returning(|_, _| Ok(()));
        params
            .lookup_table
            .expect_read()
            .times(1)
            .returning(|_| Ok(ReadResult { bytes: vec![2; 32], version: 1 }));
        let test = TestHarness::create(params).await;
        let label = test
            .cm
            .add_credential(&fcred::AddCredentialParams {
                le_secret: Some(vec![1; 32]),
                he_secret: Some(vec![2; 32]),
                reset_secret: Some(vec![3; 32]),
                delay_schedule: Some(vec![fcred::DelayScheduleEntry {
                    attempt_count: 20,
                    time_delay: 64,
                }]),
                ..fcred::AddCredentialParams::EMPTY
            })
            .await
            .expect("added credential");
        let result = test
            .cm
            .check_credential(&fcred::CheckCredentialParams {
                label: Some(label),
                le_secret: Some(vec![1; 32]),
                ..fcred::CheckCredentialParams::EMPTY
            })
            .await;
        assert_matches!(result, Err(CredentialError::InvalidSecret));
    }

    #[fuchsia::test]
    async fn test_remove_credential() {
        let mut params = TestParams::default();
        params
            .pinweaver
            .expect_insert_leaf()
            .times(1)
            .returning(|_, _, _| Ok((Mac::default(), CredentialMetadata::default())));
        params.lookup_table.expect_write().times(1).returning(|_, _| Ok(()));
        params.lookup_table.expect_delete().times(1).returning(|_| Ok(()));
        params.pinweaver.expect_remove_leaf().times(1).returning(|_, _, _| Ok(()));
        let test = TestHarness::create(params).await;
        let label = test
            .cm
            .add_credential(&fcred::AddCredentialParams {
                le_secret: Some(vec![1; 32]),
                he_secret: Some(vec![2; 32]),
                reset_secret: Some(vec![3, 32]),
                delay_schedule: Some(vec![fcred::DelayScheduleEntry {
                    attempt_count: 20,
                    time_delay: 64,
                }]),
                ..fcred::AddCredentialParams::EMPTY
            })
            .await
            .expect("added credential");
        let result = test.cm.remove_credential(label).await;
        assert_matches!(result, Ok(()));
    }

    #[fuchsia::test]
    async fn test_remove_credential_failed() {
        let mut params = TestParams::default();
        params
            .pinweaver
            .expect_insert_leaf()
            .times(1)
            .returning(|_, _, _| Ok((Mac::default(), CredentialMetadata::default())));
        params.lookup_table.expect_write().times(1).returning(|_, _| Ok(()));
        params.lookup_table.expect_delete().times(0).returning(|_| Ok(()));
        params
            .pinweaver
            .expect_remove_leaf()
            .times(1)
            .returning(|_, _, _| Err(CredentialError::InternalError));
        let test = TestHarness::create(params).await;
        let label = test
            .cm
            .add_credential(&fcred::AddCredentialParams {
                le_secret: Some(vec![1; 32]),
                he_secret: Some(vec![2; 32]),
                reset_secret: Some(vec![3, 32]),
                delay_schedule: Some(vec![fcred::DelayScheduleEntry {
                    attempt_count: 20,
                    time_delay: 64,
                }]),
                ..fcred::AddCredentialParams::EMPTY
            })
            .await
            .expect("added credential");
        let result = test.cm.remove_credential(label).await;
        assert_matches!(result, Err(CredentialError::InternalError));
    }

    #[fuchsia::test]
    async fn test_request_stream_handling() {
        let mut params = TestParams::default();
        params
            .pinweaver
            .expect_insert_leaf()
            .times(1)
            .returning(|_, _, _| Ok((Mac::default(), CredentialMetadata::default())));
        params.lookup_table.expect_write().times(1).returning(|_, _| Ok(()));
        let test = TestHarness::create(params).await;

        let (proxy, request_stream) = create_proxy_and_stream::<CredentialManagerMarker>().unwrap();
        fasync::Task::spawn(async move {
            proxy
                .add_credential(fcred::AddCredentialParams {
                    le_secret: Some(vec![1; 32]),
                    he_secret: Some(vec![2; 32]),
                    reset_secret: Some(vec![3; 32]),
                    delay_schedule: Some(vec![fcred::DelayScheduleEntry {
                        attempt_count: 20,
                        time_delay: 64,
                    }]),
                    ..fcred::AddCredentialParams::EMPTY
                })
                .await
                .expect("send add_credential should succeed")
                .expect("add_credential should return Ok");
            proxy
                .remove_credential(777)
                .await
                .expect("send remove_credential should succeed")
                .expect_err("remove_credential should return Err");
        })
        .detach();

        test.cm.handle_requests_for_stream(request_stream).await;
        test.diag.assert_events(&[
            Event::IncomingOutcome(IncomingMethod::AddCredential, Ok(())),
            Event::IncomingOutcome(
                IncomingMethod::RemoveCredential,
                Err(CredentialError::InvalidLabel),
            ),
        ]);
    }
}
