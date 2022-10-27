// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        keys::{
            EnrolledKey, Key, KeyEnrollment, KeyEnrollmentError, KeyRetrieval, KeyRetrievalError,
            KEY_LEN,
        },
        scrypt::{ScryptError, ScryptParams},
    },
    async_trait::async_trait,
    fidl_fuchsia_identity_credential::{self as fcred, ManagerProxy},
    fuchsia_zircon as zx,
    hmac::{Hmac, Mac, NewMac},
    lazy_static::lazy_static,
    serde::{Deserialize, Serialize},
    sha2::Sha256,
    tracing::{error, info},
};

// This file implements a key source which combines the computational hardness of scrypt with
// firmware-enforced rate-limiting of guesses.  The overall dataflow is:
//
//  -----------    HMAC-SHA256   -------------                              --------------
// |  password | (w/ fixed msg) | low-entropy |  rate-limited-by-firmware  | high-entropy |
// | from user | -------------> |    secret   | -------------------------> |    secret    |
//  -----------                  -------------                              --------------
//       |                                                                        |
//       | /----------------------------------------------------------------------/
//       | |
//    HMAC-SHA256
//       | |
//       V V
//  ------------                  ---------
// | mix secret |     scrypt     | account |
// |            | -------------> |   key   |
//  ------------                  ---------
//
// In this manner, we achieve the following properties:
//
// * account key cannot be computed without mix secret, and mix secret cannot be computed without
//   both password and high-entropy secret.
// * firmware does not gain direct knowledge of password, so even if firmware is compromised and
//   fails to secure high-entropy-secret, or if the low-entropy secret is pulled off the bus,
//   neither of these alone can compromise the account key

type HmacSha256 = Hmac<Sha256>;

type Label = u64;

/// Computes HMAC-SHA256(password, "password_authenticator") so that the low-entropy secret we
/// pass to credential_manager does not actually contain the user's passphrase.
fn compute_low_entropy_secret(password: &str) -> Key {
    // Create HMAC-SHA256 instance which implements `Mac` trait
    let mut mac =
        HmacSha256::new_from_slice(password.as_bytes()).expect("HMAC can take key of any size");
    mac.update(b"password_authenticator");

    // `result` has type `Output` which is a thin wrapper around array of
    // bytes for providing constant time equality check
    let result = mac.finalize();

    let mut key: Key = [0u8; KEY_LEN];
    key.clone_from_slice(&result.into_bytes());
    key
}

/// Computes mix_secret = HMAC-SHA256(password, high_entropy_secret).
/// This ensures that even if the secure element providing the high-entropy secret is compromised
/// in a way that leaks the key without the attacker needing to know the low-entropy secret, the
/// attacker still can't retrieve the account key without the user's password.
fn compute_mix_secret(password: &str, high_entropy_secret: &Key) -> Key {
    let mut mac =
        HmacSha256::new_from_slice(password.as_bytes()).expect("HMAC can take key of any size");
    mac.update(high_entropy_secret);
    let result = mac.finalize();
    let mut key: Key = [0u8; KEY_LEN];
    key.clone_from_slice(&result.into_bytes());
    key
}

/// Computes the account key, given the mix_secret (which takes both the user's password and the
/// high-entropy secret as input) and a set of ScryptParams.  This ensures the computation still
/// requires some amount of CPU cost, memory cost, and is salted per-user.
fn compute_account_key(mix_secret: &Key, params: &ScryptParams) -> Result<Key, ScryptError> {
    params.scrypt(mix_secret)
}

/// A trait which abstracts over the Credential Manager interface.
#[async_trait]
pub trait CredManager {
    /// Enroll a (low-entropy, high-entropy) key pair with the credential manager, returning a
    /// credential label on success or a KeyEnrollmentError on failure.
    async fn add(&mut self, le_secret: &Key, he_secret: &Key) -> Result<Label, KeyEnrollmentError>;

    /// Retrieve the high-entropy secret associated with the provided low-entropy secret and
    /// credential label returned from a previous call to `add`.  Returns the high-entropy secret
    /// if the low-entropy secret and credential label match.
    async fn retrieve(&self, le_secret: &Key, cred_label: Label) -> Result<Key, KeyRetrievalError>;

    /// Remove any information associated with the credential label `cred_label` and release any
    /// resources.  Returns an error if removing the key pair failed, and unit on success.
    async fn remove(&mut self, cred_label: Label) -> Result<(), KeyEnrollmentError>;
}

lazy_static! {
    // use a delay_sched hardcoded into the password_authenticator binary, matching the
    // ChromeOS Pinweaver delay schedule for attempts 1-8, maxing out at 10 minutes between
    // attempts. Unlike the ChromeOS pinweaver implementation, we do not permanently lock out after
    // any number of attempts, since we are using this for the primary password, not purely for
    // a pin (with a password with no lockout as fallback).
    static ref DELAY_SCHEDULE: Vec<fcred::DelayScheduleEntry> = vec![
        fcred::DelayScheduleEntry {
            attempt_count: 5,
            time_delay: zx::Duration::from_seconds(20).into_nanos(),
        },
        fcred::DelayScheduleEntry {
            attempt_count: 6,
            time_delay: zx::Duration::from_seconds(60).into_nanos(),
        },
        fcred::DelayScheduleEntry {
            attempt_count: 7,
            time_delay: zx::Duration::from_seconds(300).into_nanos(),
        },
        fcred::DelayScheduleEntry {
            attempt_count: 8,
            time_delay: zx::Duration::from_seconds(600).into_nanos(),
        },
    ];
}

#[async_trait]
impl CredManager for ManagerProxy {
    /// Makes a request to the `CredentialManager` server represented by `self` to add the
    /// high-entropy secret `he_secret`, guarded by the low-entropy secret `le_secret`, with a
    /// delay schedule that rate-limits retrieval attempts.
    /// Returns the credential label given back to us by the `CredentialManager` instance on
    /// success. Propagates the FIDL error or the `fuchsia.identity.credential.CredentialError`
    /// given to us by `CredentialManager` on failure.
    async fn add(&mut self, le_secret: &Key, he_secret: &Key) -> Result<Label, KeyEnrollmentError> {
        // call AddCredential with the provided parameters and the hardcoded delay_schedule
        let params = fcred::AddCredentialParams {
            le_secret: Some(le_secret.to_vec()),
            delay_schedule: Some(DELAY_SCHEDULE.to_vec()),
            he_secret: Some(he_secret.to_vec()),
            ..fcred::AddCredentialParams::EMPTY
        };

        self.add_credential(params)
            .await
            .map_err(|err| {
                error!("CredentialManager#AddCredential: couldn't send FIDL request: {:?}", err);
                KeyEnrollmentError::FidlError(err)
            })?
            .map_err(|err| {
                error!("CredentialManager#AddCredential: couldn't add credential: {:?}", err);
                KeyEnrollmentError::CredentialManagerError(err)
            })
    }

    /// Makes a request to the CredentialManager server represented by self to retrieve the
    /// high-entropy secret for the credential identified by `cred_label` by providing `le_secret`.
    /// If caller provided the same `le_secret` used in the `add()` call that returned the
    /// `cred_label` they provided, we should receive the high-entropy secret back and return Ok.
    async fn retrieve(&self, le_secret: &Key, cred_label: Label) -> Result<Key, KeyRetrievalError> {
        let params = fcred::CheckCredentialParams {
            le_secret: Some(le_secret.to_vec()),
            label: Some(cred_label),
            ..fcred::CheckCredentialParams::EMPTY
        };

        self.check_credential(params)
            .await
            .map_err(|err| {
                error!("CredentialManager#CheckCredential: couldn't send FIDL request: {:?}", err);
                KeyRetrievalError::FidlError(err)
            })?
            .map_err(|err| {
                info!(
                    "CredentialManager#CheckCredential: credential manager returned error {:?}",
                    err
                );
                KeyRetrievalError::CredentialManagerError(err)
            })
            .map(|resp| resp.he_secret)?
            .ok_or_else(|| {
                error!(
                    "CredentialManager#CheckCredential: credential manager returned success, \
                       but did not provide an `he_secret` value"
                );
                KeyRetrievalError::InvalidCredentialManagerDataError
            })
            .map(|key| {
                let mut res: [u8; KEY_LEN] = [0; KEY_LEN];
                if key.len() == KEY_LEN {
                    res.clone_from_slice(&key[..]);
                    Ok(res)
                } else {
                    error!(
                        expected_bytes = KEY_LEN,
                        got_bytes = key.len(),
                        "CredentialManager#CheckCredential: credential manager returned key \
                           with invalid length"
                    );
                    Err(KeyRetrievalError::InvalidCredentialManagerDataError)
                }
            })?
    }

    /// Makes a request to the CredentialManager server represented by self to remove the
    /// credential identified by `cred_label`, releasing any storage associated with it and
    /// making it invalid to use with future `retrieve` calls (unless returned as the result of a
    /// new call to `add`).  On failure, propagates the error from the FIDL call.
    async fn remove(&mut self, cred_label: Label) -> Result<(), KeyEnrollmentError> {
        self.remove_credential(cred_label)
            .await
            .map_err(|err| {
                error!("CredentialManager#RemoveCredential: couldn't send FIDL request: {:?}", err);
                KeyEnrollmentError::FidlError(err)
            })?
            .map_err(|err| {
                error!("CredentialManager#RemoveCredential: couldn't remove credential: {:?}", err);
                KeyEnrollmentError::CredentialManagerError(err)
            })
    }
}

/// Key enrollment data for the pinweaver key source.  Provides sufficient context to retrieve a
/// key after enrollment.
#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize, Deserialize)]
pub struct PinweaverParams {
    /// The scrypt difficulty parameters and salt to be used in the final key derivation step.
    pub scrypt_params: ScryptParams,

    /// The credential label returned from the credential manager backing store, which uniquely
    /// identifies the enrolled credential, so we can attempt to retrieve the high-entropy secret.
    pub credential_label: Label,
}

/// An implementation of KeyEnrollment using the pinweaver dataflow, backed by some CredManager
/// implementation.
pub struct PinweaverKeyEnroller<CM>
where
    CM: CredManager,
{
    scrypt_params: ScryptParams,
    credential_manager: CM,
}

impl<CM> PinweaverKeyEnroller<CM>
where
    CM: CredManager,
{
    pub fn new(credential_manager: CM) -> PinweaverKeyEnroller<CM> {
        PinweaverKeyEnroller { scrypt_params: ScryptParams::new(), credential_manager }
    }
}

#[async_trait]
impl<CM> KeyEnrollment<PinweaverParams> for PinweaverKeyEnroller<CM>
where
    CM: CredManager + std::marker::Send + std::marker::Sync,
{
    async fn enroll_key(
        &mut self,
        password: &str,
    ) -> Result<EnrolledKey<PinweaverParams>, KeyEnrollmentError> {
        // compute a 256-bit le_secret as HMAC-SHA256(password, “password_authenticator”);
        let low_entropy_secret = compute_low_entropy_secret(password);

        // generate a 256-bit random he_secret
        let mut high_entropy_secret: [u8; KEY_LEN] = [0; KEY_LEN];
        zx::cprng_draw(&mut high_entropy_secret);

        // Enroll the secret with the credential manager
        let cm = &mut self.credential_manager;
        let label = cm.add(&low_entropy_secret, &high_entropy_secret).await?;

        // compute mix_secret = HMAC-SHA256(password, he_secret)
        let mix_secret = compute_mix_secret(password, &high_entropy_secret);

        // compute account_key = scrypt(mix_secret, salt, N, p, r)
        let account_key: Key = compute_account_key(&mix_secret, &self.scrypt_params)
            .map_err(|_| KeyEnrollmentError::ParamsError)?;

        // return account_key
        Ok(EnrolledKey::<PinweaverParams> {
            key: account_key,
            enrollment_data: PinweaverParams {
                scrypt_params: self.scrypt_params,
                credential_label: label,
            },
        })
    }

    async fn remove_key(
        &mut self,
        enrollment_data: PinweaverParams,
    ) -> Result<(), KeyEnrollmentError> {
        // Tell the credential manager to remove the credential identified by the credential label
        let cm = &mut self.credential_manager;
        cm.remove(enrollment_data.credential_label).await
    }
}

pub struct PinweaverKeyRetriever<CM>
where
    CM: CredManager,
{
    pinweaver_params: PinweaverParams,
    credential_manager: CM,
}

impl<CM> PinweaverKeyRetriever<CM>
where
    CM: CredManager,
{
    pub fn new(
        pinweaver_params: PinweaverParams,
        credential_manager: CM,
    ) -> PinweaverKeyRetriever<CM> {
        PinweaverKeyRetriever { pinweaver_params, credential_manager }
    }
}

#[async_trait]
impl<CM> KeyRetrieval for PinweaverKeyRetriever<CM>
where
    CM: CredManager + std::marker::Send + std::marker::Sync,
{
    async fn retrieve_key(&self, password: &str) -> Result<Key, KeyRetrievalError> {
        let low_entropy_secret = compute_low_entropy_secret(password);
        let high_entropy_secret = self
            .credential_manager
            .retrieve(&low_entropy_secret, self.pinweaver_params.credential_label)
            .await?;

        // compute mix_secret = HMAC-SHA256(password, he_secret)
        let mix_secret = compute_mix_secret(password, &high_entropy_secret);

        // compute account_key = scrypt(mix_secret, salt, N, p, r)
        let account_key: Key =
            compute_account_key(&mix_secret, &self.pinweaver_params.scrypt_params)
                .map_err(|_| KeyRetrievalError::ParamsError)?;

        Ok(account_key)
    }
}

#[cfg(test)]
pub mod test {
    use {
        super::*,
        crate::scrypt::test::{TEST_SCRYPT_PARAMS, TEST_SCRYPT_PASSWORD},
        assert_matches::assert_matches,
        std::{
            collections::HashMap,
            sync::{Arc, Mutex},
        },
    };

    #[derive(Clone, Debug)]
    enum EnrollBehavior {
        AsExpected,
        ExpectLeSecret(Key),
        ForceFailWithEnrollmentError,
    }

    #[derive(Clone, Debug)]
    enum RetrieveBehavior {
        AsExpected,
        ForceFailWithRetrievalError,
    }

    /// An in-memory mock of a CredentialManager implementation which stores all secrets in a
    /// HashMap in memory.
    #[derive(Clone, Debug)]
    pub struct MockCredManager {
        creds: Arc<Mutex<HashMap<Label, (Key, Key)>>>,
        enroll_behavior: EnrollBehavior,
        retrieve_behavior: RetrieveBehavior,
    }

    impl MockCredManager {
        pub fn new_with_creds(creds: Arc<Mutex<HashMap<Label, (Key, Key)>>>) -> MockCredManager {
            MockCredManager {
                creds,
                enroll_behavior: EnrollBehavior::AsExpected,
                retrieve_behavior: RetrieveBehavior::AsExpected,
            }
        }

        pub fn new() -> MockCredManager {
            MockCredManager::new_with_creds(Arc::new(Mutex::new(HashMap::new())))
        }

        fn new_expect_le_secret(le_secret: &Key) -> MockCredManager {
            MockCredManager {
                creds: Arc::new(Mutex::new(HashMap::new())),
                enroll_behavior: EnrollBehavior::ExpectLeSecret(*le_secret),
                retrieve_behavior: RetrieveBehavior::AsExpected,
            }
        }

        fn new_fail_enrollment() -> MockCredManager {
            MockCredManager {
                creds: Arc::new(Mutex::new(HashMap::new())),
                enroll_behavior: EnrollBehavior::ForceFailWithEnrollmentError,
                retrieve_behavior: RetrieveBehavior::AsExpected,
            }
        }

        fn new_fail_retrieval() -> MockCredManager {
            MockCredManager {
                creds: Arc::new(Mutex::new(HashMap::new())),
                enroll_behavior: EnrollBehavior::AsExpected,
                retrieve_behavior: RetrieveBehavior::ForceFailWithRetrievalError,
            }
        }

        pub fn insert_with_next_free_label(
            &mut self,
            le_secret: Key,
            he_secret: Key,
        ) -> (Label, Option<(Key, Key)>) {
            let mut creds = self.creds.lock().unwrap();
            // Select the next label not in use.
            let mut label = 1;
            while creds.contains_key(&label) {
                label += 1;
            }
            let prev_value = creds.insert(label, (le_secret, he_secret));
            (label, prev_value)
        }
    }

    #[async_trait]
    impl CredManager for MockCredManager {
        async fn add(
            &mut self,
            le_secret: &Key,
            he_secret: &Key,
        ) -> Result<Label, KeyEnrollmentError> {
            match &self.enroll_behavior {
                EnrollBehavior::AsExpected => {
                    let (label, _) = self.insert_with_next_free_label(*le_secret, *he_secret);
                    Ok(label)
                }
                EnrollBehavior::ExpectLeSecret(key) => {
                    assert_eq!(key, le_secret);
                    let (label, _) = self.insert_with_next_free_label(*le_secret, *he_secret);
                    Ok(label)
                }
                EnrollBehavior::ForceFailWithEnrollmentError => Err(
                    KeyEnrollmentError::CredentialManagerError(fcred::CredentialError::NoFreeLabel),
                ),
            }
        }

        async fn retrieve(
            &self,
            le_secret: &Key,
            cred_label: Label,
        ) -> Result<Key, KeyRetrievalError> {
            match self.retrieve_behavior {
                RetrieveBehavior::AsExpected => {
                    let creds = self.creds.lock().unwrap();
                    match creds.get(&cred_label) {
                        Some((low_entropy_secret, high_entropy_secret)) => {
                            if le_secret == low_entropy_secret {
                                Ok(*high_entropy_secret)
                            } else {
                                Err(KeyRetrievalError::CredentialManagerError(
                                    fcred::CredentialError::InvalidSecret,
                                ))
                            }
                        }
                        None => Err(KeyRetrievalError::CredentialManagerError(
                            fcred::CredentialError::InvalidLabel,
                        )),
                    }
                }
                RetrieveBehavior::ForceFailWithRetrievalError => {
                    Err(KeyRetrievalError::CredentialManagerError(
                        fcred::CredentialError::InvalidSecret,
                    ))
                }
            }
        }

        async fn remove(&mut self, cred_label: Label) -> Result<(), KeyEnrollmentError> {
            let mut creds = self.creds.lock().unwrap();
            let prev = creds.remove(&cred_label);
            match prev {
                Some(_) => Ok(()),
                None => Err(KeyEnrollmentError::CredentialManagerError(
                    fcred::CredentialError::InvalidLabel,
                )),
            }
        }
    }

    pub const TEST_PINWEAVER_CREDENTIAL_LABEL: Label = 1u64;

    #[fuchsia::test]
    async fn test_enroll_key() {
        let expected_le_secret = compute_low_entropy_secret(TEST_SCRYPT_PASSWORD);
        let mcm = MockCredManager::new_expect_le_secret(&expected_le_secret);
        let mut pw_enroller = PinweaverKeyEnroller::new(mcm);
        let enrolled_key =
            pw_enroller.enroll_key(TEST_SCRYPT_PASSWORD).await.expect("enrollment should succeed");
        assert_matches!(
            enrolled_key.enrollment_data,
            PinweaverParams { scrypt_params: _, credential_label: TEST_PINWEAVER_CREDENTIAL_LABEL }
        );
    }

    #[fuchsia::test]
    async fn test_enroll_key_fail() {
        let mcm = MockCredManager::new_fail_enrollment();
        let mut pw_enroller = PinweaverKeyEnroller::new(mcm);
        let err =
            pw_enroller.enroll_key(TEST_SCRYPT_PASSWORD).await.expect_err("enrollment should fail");
        assert_matches!(
            err,
            KeyEnrollmentError::CredentialManagerError(fcred::CredentialError::NoFreeLabel)
        );
    }

    /// The fixed low-entropy secret derived from TEST_SCRYPT_PASSWORD
    pub const TEST_PINWEAVER_LE_SECRET: [u8; KEY_LEN] = [
        230, 21, 65, 10, 158, 243, 134, 222, 213, 187, 110, 176, 44, 67, 246, 104, 137, 26, 30, 76,
        90, 12, 229, 169, 241, 31, 123, 127, 178, 76, 210, 210,
    ];
    /// A fixed key generated and recorded in source for test usage to enable deterministic testing
    /// of key-derivation computations.  In practice, high-entropy secrets will be randomly
    /// generated at runtime.
    pub const TEST_PINWEAVER_HE_SECRET: [u8; KEY_LEN] = [
        165, 169, 79, 36, 201, 215, 227, 13, 74, 62, 115, 217, 71, 229, 180, 70, 233, 76, 139, 10,
        55, 49, 182, 163, 113, 209, 83, 18, 248, 250, 189, 153,
    ];
    /// Derived from TEST_PINWEAVER_HE_SECRET and TEST_SCRYPT_PASSWORD
    pub const TEST_PINWEAVER_MIX_SECRET: [u8; KEY_LEN] = [
        94, 201, 56, 224, 222, 39, 239, 116, 198, 113, 209, 14, 37, 140, 225, 6, 144, 168, 246,
        212, 239, 145, 233, 119, 229, 0, 91, 138, 142, 22, 10, 195,
    ];
    /// Derived from TEST_PINWEAVER_MIX_SECRET and TEST_SCRYPT_PARAMS
    pub const TEST_PINWEAVER_ACCOUNT_KEY: [u8; KEY_LEN] = [
        228, 50, 47, 112, 78, 137, 56, 116, 50, 180, 30, 230, 55, 132, 33, 117, 119, 187, 221, 250,
        73, 193, 216, 194, 37, 177, 70, 45, 209, 216, 49, 110,
    ];

    #[fuchsia::test]
    async fn test_pinweaver_goldens() {
        let low_entropy_secret = compute_low_entropy_secret(TEST_SCRYPT_PASSWORD);
        assert_eq!(low_entropy_secret, TEST_PINWEAVER_LE_SECRET);

        let mix_secret = compute_mix_secret(TEST_SCRYPT_PASSWORD, &TEST_PINWEAVER_HE_SECRET);
        assert_eq!(mix_secret, TEST_PINWEAVER_MIX_SECRET);

        let account_key =
            compute_account_key(&mix_secret, &TEST_SCRYPT_PARAMS).expect("compute account key");
        assert_eq!(account_key, TEST_PINWEAVER_ACCOUNT_KEY);
    }

    #[fuchsia::test]
    async fn test_retrieve_key() {
        let mut mcm = MockCredManager::new();
        let le_secret = compute_low_entropy_secret(TEST_SCRYPT_PASSWORD);
        let label =
            mcm.add(&le_secret, &TEST_PINWEAVER_HE_SECRET).await.expect("enroll key with mock");
        let pw_retriever = PinweaverKeyRetriever::new(
            PinweaverParams { scrypt_params: TEST_SCRYPT_PARAMS, credential_label: label },
            mcm,
        );
        let account_key_retrieved =
            pw_retriever.retrieve_key(TEST_SCRYPT_PASSWORD).await.expect("key should be found");
        assert_eq!(account_key_retrieved, TEST_PINWEAVER_ACCOUNT_KEY);
    }

    #[fuchsia::test]
    async fn test_retrieve_key_fail() {
        let mcm = MockCredManager::new_fail_retrieval();
        let pw_retriever = PinweaverKeyRetriever::new(
            PinweaverParams {
                scrypt_params: TEST_SCRYPT_PARAMS,
                credential_label: TEST_PINWEAVER_CREDENTIAL_LABEL,
            },
            mcm,
        );
        let err = pw_retriever
            .retrieve_key(TEST_SCRYPT_PASSWORD)
            .await
            .expect_err("retrieval should fail");
        assert_matches!(
            err,
            KeyRetrievalError::CredentialManagerError(fcred::CredentialError::InvalidSecret)
        );
    }

    #[fuchsia::test]
    async fn test_remove_key() {
        let creds = Arc::new(Mutex::new(HashMap::new()));
        let mcm = MockCredManager::new_with_creds(creds.clone());
        let mut pw_enroller = PinweaverKeyEnroller::new(mcm);
        let enrolled_key = pw_enroller.enroll_key(TEST_SCRYPT_PASSWORD).await.expect("enroll");
        let remove_res = pw_enroller.remove_key(enrolled_key.enrollment_data).await;
        assert_matches!(remove_res, Ok(()));
    }

    #[fuchsia::test]
    async fn test_roundtrip() {
        // Enroll a key and get the enrollment data.
        let creds = Arc::new(Mutex::new(HashMap::new()));
        let mcm = MockCredManager::new_with_creds(creds.clone());
        let mut pw_enroller = PinweaverKeyEnroller::new(mcm);
        let enrolled_key = pw_enroller.enroll_key(TEST_SCRYPT_PASSWORD).await.expect("enroll");
        let account_key = enrolled_key.key;
        let enrollment_data = enrolled_key.enrollment_data;

        // Retrieve the key, and verify it matches.
        let mcm2 = MockCredManager::new_with_creds(creds.clone());
        let pw_retriever = PinweaverKeyRetriever::new(enrollment_data, mcm2);
        let key_retrieved =
            pw_retriever.retrieve_key(TEST_SCRYPT_PASSWORD).await.expect("retrieve");
        assert_eq!(key_retrieved, account_key);

        // Remove the key.
        let remove_res = pw_enroller.remove_key(enrollment_data).await;
        assert_matches!(remove_res, Ok(()));

        // Retrieving the key again should fail.
        let retrieve_res = pw_retriever.retrieve_key(TEST_SCRYPT_PASSWORD).await;
        assert_matches!(
            retrieve_res,
            Err(KeyRetrievalError::CredentialManagerError(fcred::CredentialError::InvalidLabel))
        );

        // Removing the key again should fail.
        let remove_res2 = pw_enroller.remove_key(enrollment_data).await;
        assert_matches!(
            remove_res2,
            Err(KeyEnrollmentError::CredentialManagerError(fcred::CredentialError::InvalidLabel))
        );
    }
}
