// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::keys::{EnrolledKey, Key, KeyEnrollment, KeyError, KeyRetrieval, KEY_LEN},
    async_trait::async_trait,
    fuchsia_zircon as zx,
    serde::{Deserialize, Serialize},
};

/// Parameters used with the scrypt key-derivation function.  These match the parameters
/// described in https://datatracker.ietf.org/doc/html/rfc7914
#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize, Deserialize)]
pub struct ScryptParams {
    salt: [u8; 16], // 16 byte random string
    log_n: u8,
    r: u32,
    p: u32,
}

impl ScryptParams {
    pub fn new() -> Self {
        // Generate a new random salt
        let mut salt = [0u8; 16];
        zx::cprng_draw(&mut salt);
        ScryptParams { salt, log_n: 15, r: 8, p: 1 }
    }
}

pub struct ScryptKeySource {
    scrypt_params: ScryptParams,
}

impl ScryptKeySource {
    pub fn new() -> ScryptKeySource {
        ScryptKeySource { scrypt_params: ScryptParams::new() }
    }

    pub fn new_with_params(scrypt_params: ScryptParams) -> ScryptKeySource {
        ScryptKeySource { scrypt_params }
    }
}

#[async_trait]
impl KeyEnrollment<ScryptParams> for ScryptKeySource {
    async fn enroll_key(&self, password: &str) -> Result<EnrolledKey<ScryptParams>, KeyError> {
        let s = self.scrypt_params;
        let params =
            scrypt::Params::new(s.log_n, s.r, s.p).map_err(|_| KeyError::KeyEnrollmentError)?;
        let mut output = [0u8; KEY_LEN];
        scrypt::scrypt(password.as_bytes(), &s.salt, &params, &mut output)
            .map_err(|_| KeyError::KeyEnrollmentError)?;
        Ok(EnrolledKey { key: output, enrollment_data: self.scrypt_params.clone() })
    }
}

#[async_trait]
impl KeyRetrieval for ScryptKeySource {
    async fn retrieve_key(&self, password: &str) -> Result<Key, KeyError> {
        let s = self.scrypt_params;
        let params =
            scrypt::Params::new(s.log_n, s.r, s.p).map_err(|_| KeyError::KeyRetrievalError)?;
        let mut output = [0u8; KEY_LEN];
        scrypt::scrypt(password.as_bytes(), &s.salt, &params, &mut output)
            .map_err(|_| KeyError::KeyRetrievalError)?;
        Ok(output)
    }
}

#[cfg(test)]
pub mod test {
    use {super::*, assert_matches::assert_matches};

    // A well-known set of (weak) params & salt, password, and corresponding key for tests, to avoid
    // spending excessive CPU time doing expensive key derivations.
    pub const TEST_SCRYPT_SALT: [u8; 16] =
        [202, 26, 165, 102, 212, 113, 114, 60, 106, 121, 183, 133, 36, 166, 127, 146];
    pub const TEST_SCRYPT_PARAMS: ScryptParams =
        ScryptParams { salt: TEST_SCRYPT_SALT, log_n: 8, r: 8, p: 1 };
    pub const TEST_SCRYPT_PASSWORD: &str = "test password";

    // We have precomputed the key produced by the above fixed salt and params so that each test
    // that wants to use one doesn't need to perform an additional key derivation every single time.
    // A test below ensures that we verify our constant is correct.
    pub const TEST_SCRYPT_KEY: [u8; KEY_LEN] = [
        88, 91, 129, 123, 173, 34, 21, 1, 23, 147, 87, 189, 56, 149, 89, 132, 210, 235, 150, 102,
        129, 93, 202, 53, 115, 170, 162, 217, 254, 115, 216, 181,
    ];

    #[fuchsia::test]
    async fn test_enroll_key() {
        let ks = ScryptKeySource::new_with_params(TEST_SCRYPT_PARAMS);
        let enrolled_key = ks.enroll_key(TEST_SCRYPT_PASSWORD).await.expect("enroll scrypt");
        assert_eq!(enrolled_key.key, TEST_SCRYPT_KEY);
        assert_matches!(enrolled_key.enrollment_data, TEST_SCRYPT_PARAMS);
    }

    #[fuchsia::test]
    async fn test_retrieve_key_weak_for_tests() {
        let ks = ScryptKeySource::new_with_params(TEST_SCRYPT_PARAMS);
        let key = ks.retrieve_key(TEST_SCRYPT_PASSWORD).await.expect("retrieve_key");
        assert_eq!(key, TEST_SCRYPT_KEY);
    }

    const FULL_STRENGTH_SCRYPT_SALT: [u8; 16] =
        [198, 228, 57, 32, 90, 251, 238, 12, 194, 62, 68, 106, 218, 187, 24, 246];
    pub const FULL_STRENGTH_SCRYPT_PARAMS: ScryptParams =
        ScryptParams { salt: FULL_STRENGTH_SCRYPT_SALT, log_n: 15, r: 8, p: 1 };
    const GOLDEN_SCRYPT_PASSWORD: &str = "test password";
    const GOLDEN_SCRYPT_KEY: [u8; KEY_LEN] = [
        27, 250, 228, 96, 145, 67, 194, 114, 144, 240, 92, 150, 43, 136, 128, 51, 223, 120, 56,
        118, 124, 122, 106, 185, 159, 111, 178, 50, 86, 243, 227, 175,
    ];

    #[fuchsia::test]
    async fn test_retrieve_key_full_strength() {
        // Tests the full-strength key derivation against separately-verified constants.
        let ks = ScryptKeySource::new_with_params(FULL_STRENGTH_SCRYPT_PARAMS);
        let key = ks.retrieve_key(GOLDEN_SCRYPT_PASSWORD).await.expect("retrieve_key");
        assert_eq!(key, GOLDEN_SCRYPT_KEY);
    }
}
