// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        diagnostics::{Diagnostics, IncomingManagerMethod, IncomingResetMethod},
        error::ServiceError,
        hash_tree::{HashTree, HashTreeStorage, BITS_PER_LEVEL, LABEL_LENGTH, TREE_HEIGHT},
        label::Label,
        lookup_table::LookupTable,
        pinweaver::{CredentialMetadata, Hash, Mac, PinWeaverProtocol},
    },
    anyhow::{Context, Error},
    fidl_fuchsia_identity_credential::{
        self as fcred, CredentialError, ManagerRequest, ManagerRequestStream, ResetError,
        ResetterRequest, ResetterRequestStream,
    },
    fidl_fuchsia_tpm_cr50::TryAuthResponse,
    fuchsia_async::{self as fasync, DurationExt},
    fuchsia_zircon as zx,
    futures::{
        lock::{Mutex, MutexGuard},
        prelude::*,
    },
    std::{
        cell::{RefCell, RefMut},
        collections::VecDeque,
        sync::Arc,
    },
    tracing::{error, info, warn},
};

/// Retry threshold for fast retrys for failed |CommitOperation|.
/// After this threshold is reached the retry timeout will be extended
/// from |COMMIT_RETRY_MIN_DELAY_MS| to |COMMIT_RETRY_MAX_DELAY_MS|.
/// Note the first retry is always tried instantly.
const COMMIT_FAILURE_FAST_RETRY_THRESHOLD: u64 = 3;
/// Minimum delay between retry attempts for |CommitOperation| after
/// one instant retry.
const COMMIT_RETRY_MIN_DELAY_MS: zx::Duration = zx::Duration::from_millis(1000);
/// Maximum delay between retry attempts for |CommitOperation| after
/// |COMMIT_FAILURE_FAST_RETRY_THRESHOLD| is reached. This means in the
/// case of a persistent disk failure CredentialManager will retry the
/// last |CommitOperation| once every 5 seconds.
const COMMIT_RETRY_MAX_DELAY_MS: zx::Duration = zx::Duration::from_millis(5000);

/// There are a small finite set of distinct disk write operations
/// the CredentialManager can perform for any given PinWeaver operation.
/// This defines all possible mutable operations that can be performed.
#[derive(Debug)]
enum CommitOperation {
    /// Delete |cred_metadata| at a given |label|
    DeleteMetadata { label: Label },
    /// Write |cred_metadata| to disk with a given |label|
    WriteMetadata { label: Label, cred_metadata: CredentialMetadata },
    /// Sync the state of the |hash_tree| to disk.
    WriteHashTree,
}

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
    // Only accessible when guarded by the pinweaver lock.
    hash_tree: RefCell<HashTree>,
    // Only accessible when guarded by the pinweaver lock.
    lookup_table: RefCell<LT>,
    hash_tree_storage: HS,
    diagnostics: Arc<D>,
    // Acquiring the `pending_commits` mutex first requires acquiring the `pinweaver`
    // lock before adding items to the commit queue.
    pending_commits: Mutex<VecDeque<CommitOperation>>,
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
            pending_commits: Mutex::new(VecDeque::new()),
        }
    }

    /// Serially process a stream of incoming CredentialManager FIDL requests.
    pub async fn handle_requests_for_stream(&self, mut request_stream: ManagerRequestStream) {
        while let Some(request) = request_stream.try_next().await.expect("read request") {
            self.handle_request(request)
                .unwrap_or_else(|e| {
                    error!("error handling fidl request: {:?}", e);
                })
                .await
        }
    }

    /// Handles the special Resetter FIDL requests which reset the state of
    /// CredentialManager both on-disk and on-chip.
    pub async fn handle_requests_for_reset_stream(
        &self,
        mut request_stream: ResetterRequestStream,
    ) {
        while let Some(request) = request_stream.try_next().await.expect("read request") {
            self.handle_reset_request(request)
                .unwrap_or_else(|e| {
                    error!("error handling fidl request: {:?}", e);
                })
                .await
        }
    }

    /// Process a single CredentialManager FIDL request and send a reply.
    /// This request can either add, remove or check a credential. It is important
    /// that only one request is processed at a time as the |pinweaver| protocol
    /// can only handle communicating with a single object.
    async fn handle_request(&self, request: ManagerRequest) -> Result<(), Error> {
        match request {
            ManagerRequest::AddCredential { params, responder } => {
                info!("AddCredential: Request Received");
                let result = self.add_credential(&params).await;
                if let Err(e) = &result {
                    warn!("AddCredential: Failed: {:?}", e);
                } else {
                    info!("AddCredential: Succeeded");
                }
                let mut response = result.map_err(ServiceError::into);
                responder.send(&mut response).context("sending AddCredential response")?;
                self.diagnostics.incoming_manager_outcome(
                    IncomingManagerMethod::AddCredential,
                    response.map(|_| ()),
                );
            }
            ManagerRequest::RemoveCredential { label, responder } => {
                info!("RemoveCredential: Request Received");
                let result = self.remove_credential(label).await;
                if let Err(e) = &result {
                    warn!("RemoveCredential: Failed: {:?}", e);
                } else {
                    info!("RemoveCredential: Succeeded");
                }
                let mut response = result.map_err(ServiceError::into);
                responder.send(&mut response).context("sending RemoveLabel response")?;
                self.diagnostics.incoming_manager_outcome(
                    IncomingManagerMethod::RemoveCredential,
                    response.map(|_| ()),
                );
            }
            ManagerRequest::CheckCredential { params, responder } => {
                info!("CheckCredential: Request Received");
                let result = self.check_credential(&params).await;
                if let Err(e) = &result {
                    warn!("CheckCredential: Failed: {:?}", e);
                } else {
                    info!("CheckCredential: Succeeded");
                }
                let mut response = result.map_err(ServiceError::into);
                responder.send(&mut response).context("sending CheckCredential response")?;
                self.diagnostics.incoming_manager_outcome(
                    IncomingManagerMethod::CheckCredential,
                    response.map(|_| ()),
                );
            }
        }
        self.drain_pending_commits().await;
        Ok(())
    }

    /// Process a single Resetter FIDL request and send a reply. This request can
    /// only be the reset method.
    async fn handle_reset_request(&self, request: ResetterRequest) -> Result<(), Error> {
        match request {
            ResetterRequest::Reset { responder } => {
                info!("Reset: Request Received");
                let mut resp = self.reset().await;
                if let Err(e) = resp {
                    warn!("Reset: Failed: {:?}", e);
                } else {
                    info!("Reset: Succeeded");
                }
                responder.send(&mut resp).context("sending Reset response")?;
                self.diagnostics.incoming_reset_outcome(IncomingResetMethod::Reset, resp);
            }
        }
        Ok(())
    }

    /// PinWeaver operations follow the following state mutating pattern:
    /// 1. Disk state is read.
    /// 2. Chip state is mutated.
    /// 3. Disk state is mutated.
    /// Chip state and disk state must remain consistent. If 2 completes
    /// successfully but 3 fails, pinweaver includes a log replay mechanism
    /// that allows the disk state to be resynchonized to the chip state,
    /// but only for a single operation. To ensure state can be resynchronized
    /// we must only allow a single operation that has been written to the chip
    /// but not yet written to disk.
    async fn drain_pending_commits(&self) -> MutexGuard<'_, VecDeque<CommitOperation>> {
        let mut pending_commits = self.pending_commits.lock().await;
        while let Some(next_commit) = pending_commits.pop_front() {
            let mut retry_count: u64 = 0;
            while let Err(err) = self.attempt_commit(&next_commit).await {
                // Limit log spamming on retries.
                if retry_count < COMMIT_FAILURE_FAST_RETRY_THRESHOLD {
                    warn!(
                        ?err, %retry_count,
                        "Failed to commit disk operation: {:?}",
                        next_commit,
                    );
                }
                if retry_count >= 1 {
                    if retry_count < COMMIT_FAILURE_FAST_RETRY_THRESHOLD {
                        fasync::Timer::new(COMMIT_RETRY_MIN_DELAY_MS.after_now()).await;
                    } else {
                        fasync::Timer::new(COMMIT_RETRY_MAX_DELAY_MS.after_now()).await;
                    }
                }
                retry_count += 1;
            }
            if retry_count >= 1 {
                info!(
                    "Commit disk operation: {:?} eventually succeeded after: {} retries.",
                    next_commit, retry_count
                );
            }
        }
        pending_commits
    }

    /// Attempts to execute a pending commit operation. On failure
    /// returns an appropriate CredentialError.
    async fn attempt_commit(&self, commit_operation: &CommitOperation) -> Result<(), ServiceError> {
        match commit_operation {
            CommitOperation::DeleteMetadata { label } => {
                self.lookup_table().delete(label).await?;
            }
            CommitOperation::WriteMetadata { label, cred_metadata } => {
                self.lookup_table().write(label, cred_metadata.clone()).await?
            }
            CommitOperation::WriteHashTree => {
                self.hash_tree_storage.store(&self.hash_tree.borrow())?;
            }
        };
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
    ) -> Result<u64, ServiceError> {
        let pinweaver = self.pinweaver.lock().await;
        let pending_commits = self.drain_pending_commits().await;
        let (label, h_aux) = self.alloc_credential().await?;
        let (mac, cred_metadata) = pinweaver.insert_leaf(&label, h_aux, params).await?;
        self.update_credential(&label, mac, cred_metadata, pending_commits).await?;
        self.diagnostics.credential_count(self.hash_tree().populated_size());
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
    ) -> Result<fcred::CheckCredentialResponse, ServiceError> {
        let pinweaver = self.pinweaver.lock().await;
        let pending_commits = self.drain_pending_commits().await;
        let label =
            Label::leaf_label(params.label.ok_or(CredentialError::InternalError)?, LABEL_LENGTH);
        let (h_aux, stored_cred_metadata) = self.get_credential(&label).await?;
        let le_secret = params.le_secret.as_ref().ok_or(CredentialError::InternalError)?;

        match pinweaver.try_auth(le_secret, h_aux, stored_cred_metadata).await? {
            TryAuthResponse::Success(response) => {
                let mac = response.mac.ok_or(CredentialError::InternalError)?;
                let cred_metadata = response.cred_metadata.ok_or(CredentialError::InternalError)?;
                self.update_credential(&label, mac, cred_metadata, pending_commits).await?;
                Ok(fcred::CheckCredentialResponse {
                    he_secret: response.he_secret,
                    ..fcred::CheckCredentialResponse::EMPTY
                })
            }
            TryAuthResponse::Failed(response) => {
                let mac = response.mac.ok_or(CredentialError::InternalError)?;
                let cred_metadata = response.cred_metadata.ok_or(CredentialError::InternalError)?;
                self.update_credential(&label, mac, cred_metadata, pending_commits).await?;
                Err(CredentialError::InvalidSecret.into())
            }
            TryAuthResponse::RateLimited(_) => Err(CredentialError::TooManyAttempts.into()),
            _ => Err(CredentialError::InternalError.into()),
        }
    }

    /// Attempts to remove a credential specified by the |label| in both the
    /// cr50 state and in the internal |hash_tree|. Returns nothing on success.
    async fn remove_credential(&self, label: u64) -> Result<(), ServiceError> {
        let pinweaver = self.pinweaver.lock().await;
        let mut pending_commits = self.drain_pending_commits().await;
        let label = Label::leaf_label(label, LABEL_LENGTH);
        let h_aux = self.hash_tree().get_auxiliary_hashes_flattened(&label)?;
        let mac = *self.hash_tree().get_leaf_hash(&label)?;
        pinweaver.remove_leaf(&label, mac, h_aux).await?;
        pending_commits.push_back(CommitOperation::DeleteMetadata { label });
        self.hash_tree().delete_leaf(&label)?;
        pending_commits.push_back(CommitOperation::WriteHashTree);
        // Note: For consistency with `add_credential` we record the new credential count after the
        // store event, and even if the store event failed
        self.diagnostics.credential_count(self.hash_tree().populated_size());
        Ok(())
    }

    /// Reset resets the state of the credential manager calling ResetTree and
    /// purging all of the on-disk state. This is intended to be called during
    /// FactoryDeviceReset.
    async fn reset(&self) -> Result<(), ResetError> {
        let pinweaver = self.pinweaver.lock().await;
        pinweaver
            .reset_tree(BITS_PER_LEVEL, TREE_HEIGHT)
            .await
            .map_err(|_| ResetError::ChipStateFailedToClear)?;
        self.hash_tree().reset().map_err(|_| ResetError::DiskStateFailedToClear)?;
        self.lookup_table().reset().await.map_err(|_| ResetError::DiskStateFailedToClear)?;
        self.hash_tree_storage
            .store(&self.hash_tree.borrow())
            .map_err(|_| ResetError::DiskStateFailedToClear)?;
        Ok(())
    }

    /// Allocates a new empty credential in the |hash_tree| returning the
    /// leaf |label| and the auxiliary hashes |h_aux| from the leaf through to
    /// the root of the tree.
    async fn alloc_credential(&self) -> Result<(Label, Vec<Hash>), ServiceError> {
        let label = self.hash_tree().get_free_leaf_label()?;
        let h_aux = self.hash_tree().get_auxiliary_hashes_flattened(&label)?;
        Ok((label, h_aux))
    }

    /// Attempts to retrieve the auxiliary hashes |h_aux| and the
    /// |credential_metadata| associated with the |label| from the |hash_tree|
    /// and |lookup_table| respectively.
    async fn get_credential(
        &self,
        label: &Label,
    ) -> Result<(Vec<Hash>, CredentialMetadata), ServiceError> {
        let h_aux = self.hash_tree().get_auxiliary_hashes_flattened(label)?;
        let stored_cred_metadata = self.lookup_table().read(label).await?.bytes;
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
        mut pending_commits: MutexGuard<'_, VecDeque<CommitOperation>>,
    ) -> Result<(), ServiceError> {
        self.hash_tree().update_leaf_hash(label, mac)?;
        pending_commits.push_back(CommitOperation::WriteMetadata { label: *label, cred_metadata });
        pending_commits.push_back(CommitOperation::WriteHashTree);
        Ok(())
    }

    /// Convenience function that returns a RefMut to the |hash_tree|.
    fn hash_tree(&self) -> RefMut<'_, HashTree> {
        self.hash_tree.borrow_mut()
    }

    /// Convenience function that returns a RefMut to the |lookup_table|.
    fn lookup_table(&self) -> RefMut<'_, LT> {
        self.lookup_table.borrow_mut()
    }
}

#[cfg(test)]
mod test {
    use {
        super::*,
        crate::{
            diagnostics::{Event, FakeDiagnostics, HashTreeOperation},
            error::CredentialErrorWrapper,
            hash_tree::{HashTreeStorageCbor, CHILDREN_PER_NODE, TREE_HEIGHT},
            lookup_table::{LookupTableError, MockLookupTable, ReadResult},
            pinweaver::{MockPinWeaverProtocol, PinWeaverErrorCode, PinWeaverProtocolError},
        },
        assert_matches::assert_matches,
        fidl::endpoints::create_proxy_and_stream,
        fidl_fuchsia_identity_credential::{CredentialError as CE, ManagerMarker},
        fidl_fuchsia_tpm_cr50::{
            PinWeaverError, TryAuthFailed, TryAuthRateLimited, TryAuthSuccess,
        },
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
            HashTreeStorageCbor<FakeDiagnostics>,
            FakeDiagnostics,
        >,
        diag: Arc<FakeDiagnostics>,
        _dir: TempDir,
    }

    impl TestHarness {
        async fn create(params: TestParams) -> Self {
            let path = params.dir.path().join("hash_tree");
            let diag = Arc::new(FakeDiagnostics::new());
            let hash_tree_storage =
                HashTreeStorageCbor::new(path.to_str().unwrap(), Arc::clone(&diag));
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
        test.cm.drain_pending_commits().await;
        test.diag.assert_events(&[
            Event::CredentialCount(1),
            Event::HashTreeOutcome(HashTreeOperation::Store, Ok(())),
        ]);
    }

    #[fuchsia::test]
    async fn test_add_credential_lookup_table_write_fail_succeeds_on_retry() {
        let mut params = TestParams::default();
        params
            .pinweaver
            .expect_insert_leaf()
            .times(1)
            .returning(|_, _, _| Ok((Mac::default(), CredentialMetadata::default())));
        let mut call_count = 0;
        params.lookup_table.expect_write().times(2).returning(move |_, _| {
            if call_count == 0 {
                call_count += 1;
                Err(LookupTableError::Unknown)
            } else {
                Ok(())
            }
        });
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
        assert_matches!(result, Ok(_));
        test.cm.drain_pending_commits().await;
        test.diag.assert_events(&[
            Event::CredentialCount(1),
            Event::HashTreeOutcome(HashTreeOperation::Store, Ok(())),
        ]);
    }

    #[fuchsia::test]
    async fn test_add_credential_pinweaver_failed() {
        let mut params = TestParams::default();
        params
            .pinweaver
            .expect_insert_leaf()
            .times(1)
            .returning(|_, _, _| Err(PinWeaverProtocolError::from(PinWeaverError::LabelInvalid)));
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
        assert_matches!(
            result,
            Err(ServiceError::PinWeaver(PinWeaverProtocolError::PinWeaverErrorCode(
                PinWeaverErrorCode(PinWeaverError::LabelInvalid)
            )))
        );
        test.diag.assert_events(&[]);
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
        test.cm.drain_pending_commits().await;
        test.diag.assert_events(&[
            Event::CredentialCount(1),
            Event::HashTreeOutcome(HashTreeOperation::Store, Ok(())),
            Event::HashTreeOutcome(HashTreeOperation::Store, Ok(())),
        ]);
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
        assert_matches!(
            result,
            Err(ServiceError::Credential(CredentialErrorWrapper(CredentialError::TooManyAttempts)))
        );
        test.cm.drain_pending_commits().await;
        test.diag.assert_events(&[
            Event::CredentialCount(1),
            Event::HashTreeOutcome(HashTreeOperation::Store, Ok(())),
        ]);
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
        assert_matches!(
            result,
            Err(ServiceError::Credential(CredentialErrorWrapper(CredentialError::InvalidSecret)))
        );
        test.cm.drain_pending_commits().await;
        test.diag.assert_events(&[
            Event::CredentialCount(1),
            Event::HashTreeOutcome(HashTreeOperation::Store, Ok(())),
            Event::HashTreeOutcome(HashTreeOperation::Store, Ok(())),
        ]);
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
        test.cm.drain_pending_commits().await;
        test.diag.assert_events(&[
            Event::CredentialCount(1),
            Event::HashTreeOutcome(HashTreeOperation::Store, Ok(())),
            Event::CredentialCount(0),
            Event::HashTreeOutcome(HashTreeOperation::Store, Ok(())),
        ]);
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
            .returning(|_, _, _| Err(PinWeaverProtocolError::from(PinWeaverError::LabelInvalid)));
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
        test.cm.drain_pending_commits().await;
        let result = test.cm.remove_credential(label).await;
        test.cm.drain_pending_commits().await;
        assert_matches!(
            result,
            Err(ServiceError::PinWeaver(PinWeaverProtocolError::PinWeaverErrorCode(
                PinWeaverErrorCode(PinWeaverError::LabelInvalid)
            )))
        );
        test.cm.drain_pending_commits().await;
        test.diag.assert_events(&[
            Event::CredentialCount(1),
            Event::HashTreeOutcome(HashTreeOperation::Store, Ok(())),
        ]);
    }

    #[fuchsia::test]
    async fn test_reset() {
        let mut params = TestParams::default();
        params.pinweaver.expect_reset_tree().times(1).returning(|_, _| Ok([0; 32]));
        params.lookup_table.expect_reset().times(1).returning(|| Ok(()));
        let test = TestHarness::create(params).await;
        let result = test.cm.reset().await;
        assert_matches!(result, Ok(()));
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

        let (proxy, request_stream) = create_proxy_and_stream::<ManagerMarker>().unwrap();
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
            Event::CredentialCount(1),
            Event::IncomingManagerOutcome(IncomingManagerMethod::AddCredential, Ok(())),
            Event::HashTreeOutcome(HashTreeOperation::Store, Ok(())),
            Event::IncomingManagerOutcome(
                IncomingManagerMethod::RemoveCredential,
                Err(CE::InvalidLabel),
            ),
        ]);
    }
}
