// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use boringssl_sys::{
    BN_bin2bn, BN_bn2bin, BN_dup, BN_free, BN_new, BN_num_bytes, CBB_cleanup, CBB_finish, CBB_init,
    CBS_init, EC_GROUP_new_by_curve_name, EC_KEY_free, EC_KEY_get0_group, EC_KEY_get0_private_key,
    EC_KEY_get0_public_key, EC_KEY_new_by_curve_name, EC_KEY_parse_private_key,
    EC_KEY_set_public_key_affine_coordinates, EC_POINT_get_affine_coordinates_GFp, EVP_PKEY_free,
    EVP_PKEY_new, EVP_PKEY_set1_EC_KEY, EVP_PKEY_set1_RSA, EVP_marshal_public_key, OPENSSL_free,
    RSA_free, RSA_get0_key, RSA_new, RSA_parse_private_key, RSA_set0_key, BIGNUM as BN, CBB, CBS,
    EC_KEY, EVP_PKEY, RSA,
};
use std::convert::{TryFrom, TryInto};
use std::mem;
use std::ptr;
use std::slice;

/// Wrapper around openssl BIGNUM.
struct BigNum<'a> {
    bignum: &'a mut BN,
}

impl<'a> BigNum<'a> {
    /// Creates an new empty big number object.
    pub fn new() -> Option<Self> {
        unsafe { BN_new().as_mut().map(|bignum| BigNum { bignum }) }
    }

    /// Parses the binary data into a big number object.
    pub fn parse(buffer: &[u8]) -> Option<Self> {
        // Don't panic while unsafe.
        let buffer_len = buffer.len().try_into().unwrap();
        unsafe {
            BN_bin2bn(buffer.as_ptr(), buffer_len, ptr::null_mut())
                .as_mut()
                .map(|bignum| BigNum { bignum })
        }
    }

    /// Turns the big number object to binary representation.
    pub fn to_vec(&self) -> Option<Vec<u8>> {
        Self::bn_to_vec(self.bignum as *const BN)
    }

    /// Static function to turn an openssl BIGNUM structure to binary.
    ///
    /// This should be used by caller which does not own a BIGNUM.
    pub fn bn_to_vec(bignum: *const BN) -> Option<Vec<u8>> {
        let num_bytes = unsafe { BN_num_bytes(bignum) };
        let mut bignum_binary: Vec<u8> = vec![0; num_bytes as usize];
        let actual = unsafe { BN_bn2bin(bignum, bignum_binary.as_mut_ptr()) };
        if usize::try_from(num_bytes).unwrap() != usize::try_from(actual).unwrap() {
            return None;
        }
        Some(bignum_binary)
    }

    /// Gets an internal mutable pointer to the wrapped object.
    pub fn get_mut_ptr(&mut self) -> *mut BN {
        self.bignum as *mut BN
    }

    /// Duplicate the BigNum and gets a mutable pointer to the duplicated openssl BigNumber object.
    /// Caller is responsible to free the returned object.
    pub fn get_dup_mut_ptr(&self) -> *mut BN {
        unsafe { BN_dup(self.bignum as *const BN) }
    }
}

impl<'a> Drop for BigNum<'a> {
    fn drop(&mut self) {
        unsafe {
            BN_free(self.bignum as *mut BN);
        }
    }
}

/// Wrapper around openssl CBB.
struct Cbb {
    cbb: CBB,
}

impl Cbb {
    /// Creates a new CBB with initial size.
    pub fn new(size: usize) -> Option<Self> {
        let mut cbb: CBB = unsafe { mem::zeroed() };
        // Don't panic while unsafe.
        let cbb_len = size.try_into().unwrap();
        let result = unsafe { CBB_init(&mut cbb, cbb_len) };
        if result == 0 {
            return None;
        }
        Some(Cbb { cbb })
    }

    /// Gets an internal mutable pointer to the wrapped object.
    pub fn get_mut_ptr(&mut self) -> *mut CBB {
        &mut self.cbb
    }

    /// Turns a CBB structure into an AllocatedBuffer.
    pub fn finish(&mut self) -> Option<Vec<u8>> {
        let mut output_bytes: *mut u8 = ptr::null_mut();
        let mut output_size = 0;
        let result = unsafe { CBB_finish(&mut self.cbb, &mut output_bytes, &mut output_size) };
        if result == 0 {
            return None;
        }
        let output_size = output_size.try_into().unwrap();
        let v = unsafe { slice::from_raw_parts(output_bytes, output_size).to_vec() };
        unsafe { OPENSSL_free(output_bytes as *mut std::ffi::c_void) };
        Some(v)
    }
}

impl Drop for Cbb {
    fn drop(&mut self) {
        unsafe {
            CBB_cleanup(&mut self.cbb);
        }
    }
}

/// Wrapper around openssl EVP_PKEY.
struct EvpPkey<'a> {
    pkey: &'a mut EVP_PKEY,
}

impl<'a> EvpPkey<'a> {
    /// Creates a new EVP_PKEY.
    pub fn new() -> Option<Self> {
        unsafe { EVP_PKEY_new().as_mut().map(|pkey| EvpPkey { pkey }) }
    }

    /// Gets an internal mutable pointer to the wrapped object.
    pub fn get_mut_ptr(&mut self) -> *mut EVP_PKEY {
        self.pkey as *mut EVP_PKEY
    }

    pub fn marshal_public_key(&self) -> Option<Vec<u8>> {
        let mut cbb = Cbb::new(64)?;
        let result =
            unsafe { EVP_marshal_public_key(cbb.get_mut_ptr(), self.pkey as *const EVP_PKEY) };
        if result == 0 {
            return None;
        }
        cbb.finish()
    }
}

impl<'a> Drop for EvpPkey<'a> {
    fn drop(&mut self) {
        unsafe {
            EVP_PKEY_free(self.pkey as *mut EVP_PKEY);
        }
    }
}

/// An RSA public key object wrapping openssl RSA.
pub struct RsaPublicKey<'a> {
    rsa: &'a mut RSA,
}

impl<'a> RsaPublicKey<'a> {
    /// Creates a new public key using the modulus and public exponent.
    pub fn new(modulus: &[u8], public_exponent: &[u8]) -> Result<Self, &'static str> {
        let modulus_bn = BigNum::parse(modulus).ok_or("Modulus is not a valid BigNum!")?;
        let public_exponent_bn =
            BigNum::parse(public_exponent).ok_or("Public exponent is not a valid BigNum!")?;
        let rsa = unsafe { RSA_new().as_mut().ok_or("Failed to initialize Rsa!") }?;
        let result = unsafe {
            RSA_set0_key(
                rsa as *mut RSA,
                // We need to duplicate the BigNumber here because this function expects to obtain
                // the BIGNUM arguments.
                modulus_bn.get_dup_mut_ptr(),
                public_exponent_bn.get_dup_mut_ptr(),
                ptr::null_mut(),
            )
        };
        if result == 0 {
            // Return 1 on success or 0 on failure.
            return Err("Failed to set rsa public key!");
        }
        Ok(RsaPublicKey { rsa })
    }

    /// Marshals the public key into DER format.
    pub fn marshal_public_key(&mut self) -> Result<Vec<u8>, &'static str> {
        let mut evp_pkey = EvpPkey::new().ok_or("Failed to initialize EvpPkey!")?;
        let result = unsafe { EVP_PKEY_set1_RSA(evp_pkey.get_mut_ptr(), self.rsa as *mut RSA) };
        if result == 0 {
            // Return 1 on success or 0 on failure.
            return Err("Failed to parse EVP key to RSA key!");
        }
        evp_pkey.marshal_public_key().ok_or("Failed to marshal public key!")
    }
}

impl<'a> Drop for RsaPublicKey<'a> {
    fn drop(&mut self) {
        unsafe {
            RSA_free(self.rsa as *mut RSA);
        }
    }
}

/// An RSA private key object wrapping openssl RSA.
pub struct RsaPrivateKey<'a> {
    rsa: &'a mut RSA,
}

impl<'a> RsaPrivateKey<'a> {
    /// Creates a new RSA private key using the DER format key data.
    pub fn new(key_data: &[u8]) -> Result<Self, &'static str> {
        // Don't panic while unsafe.
        let key_data_len = key_data.len().try_into().unwrap();
        let rsa = unsafe {
            let mut cbs: CBS = mem::zeroed();
            CBS_init(&mut cbs, key_data.as_ptr(), key_data_len);
            RSA_parse_private_key(&mut cbs).as_mut().ok_or("Failed to parse RSA Key!")
        }?;
        Ok(RsaPrivateKey { rsa })
    }

    /// Gets the modulus for this key.
    pub fn get_modulus(&self) -> Result<Vec<u8>, &'static str> {
        // Note that modulus here is not owned by us, so we could not use BigNum wrapper.
        let mut modulus: *const BN = ptr::null_mut();
        unsafe {
            RSA_get0_key(self.rsa as *const RSA, &mut modulus, ptr::null_mut(), ptr::null_mut())
        };
        BigNum::bn_to_vec(modulus).ok_or("Failed to convert BigNum to vector!")
    }

    /// Gets the public exponent for this key.
    pub fn get_public_exponent(&self) -> Result<Vec<u8>, &'static str> {
        let mut public_exponent: *const BN = ptr::null_mut();
        unsafe {
            RSA_get0_key(
                self.rsa as *const RSA,
                ptr::null_mut(),
                &mut public_exponent,
                ptr::null_mut(),
            )
        };
        BigNum::bn_to_vec(public_exponent).ok_or("Failed to convert BigNum to vector!")
    }

    /// Gets the private exponent for this key.
    pub fn get_private_exponent(&self) -> Result<Vec<u8>, &'static str> {
        let mut private_exponent: *const BN = ptr::null_mut();
        unsafe {
            RSA_get0_key(
                self.rsa as *const RSA,
                ptr::null_mut(),
                ptr::null_mut(),
                &mut private_exponent,
            )
        };
        BigNum::bn_to_vec(private_exponent).ok_or("Failed to convert BigNum to vector!")
    }
}

impl<'a> Drop for RsaPrivateKey<'a> {
    fn drop(&mut self) {
        unsafe {
            RSA_free(self.rsa as *mut RSA);
        }
    }
}

/// An EC public key object wrapping openssl EC_KEY.
pub struct EcPublicKey<'a> {
    ec_key: &'a mut EC_KEY,
}

impl<'a> EcPublicKey<'a> {
    /// Creates a new EC key on the specific curve using public_x and public_y.
    pub fn new(nid: i32, x: &[u8], y: &[u8]) -> Result<Self, &'static str> {
        let ec_key = unsafe {
            EC_KEY_new_by_curve_name(nid).as_mut().ok_or("Failed to initialize EcPublicKey!")
        }?;
        let mut x_bn = BigNum::parse(x).ok_or("Public value x is not a valid BigNum!")?;
        let mut y_bn = BigNum::parse(y).ok_or("Public value y is not a valid BigNum!")?;
        let result = unsafe {
            EC_KEY_set_public_key_affine_coordinates(
                ec_key as *mut EC_KEY,
                x_bn.get_mut_ptr(),
                y_bn.get_mut_ptr(),
            )
        };
        if result == 0 {
            // Return 1 on success or 0 on failure.
            return Err("Failed to set EC public key!");
        }
        Ok(EcPublicKey { ec_key })
    }

    /// Marshals the EC key to DER format.
    pub fn marshal_public_key(&mut self) -> Result<Vec<u8>, &'static str> {
        let mut evp_pkey = EvpPkey::new().ok_or("Failed to initialize EvpPkey!")?;
        let result =
            unsafe { EVP_PKEY_set1_EC_KEY(evp_pkey.get_mut_ptr(), self.ec_key as *mut EC_KEY) };
        if result == 0 {
            return Err("Failed to set ec key!");
        }
        evp_pkey.marshal_public_key().ok_or("Failed to marshal public key!")
    }
}

impl<'a> Drop for EcPublicKey<'a> {
    fn drop(&mut self) {
        unsafe {
            EC_KEY_free(self.ec_key as *mut EC_KEY);
        }
    }
}

/// An EC private key object wrapping openssl EC_KEY.
pub struct EcPrivateKey<'a> {
    ec_key: &'a mut EC_KEY,
}

impl<'a> EcPrivateKey<'a> {
    /// Creates a new EC private key using the DER format key data on a specific curve.
    pub fn new(nid: i32, key_data: &[u8]) -> Result<Self, &'static str> {
        let ec_group = unsafe { EC_GROUP_new_by_curve_name(nid) };
        if ec_group.is_null() {
            return Err("Invalid ec curve type!");
        }
        // Don't panic while unsafe.
        let key_data_len = key_data.len().try_into().unwrap();
        let ec_key = unsafe {
            let mut cbs = mem::zeroed();
            CBS_init(&mut cbs, key_data.as_ptr(), key_data_len);
            EC_KEY_parse_private_key(&mut cbs, ec_group).as_mut().ok_or("Failed to parse EC Key!")
        }?;
        Ok(EcPrivateKey { ec_key })
    }

    /// Gets the private key for this EC key.
    pub fn get_private_key(&self) -> Result<Vec<u8>, &'static str> {
        let bignum = unsafe { EC_KEY_get0_private_key(self.ec_key as *const EC_KEY) };
        // We can't use BigNum here because BigNum would take responsibility and here bignum is
        // just a reference.
        BigNum::bn_to_vec(bignum as *const BN).ok_or("Failed to covert BigNum to vector!")
    }

    /// Gets the public key x for this EC key.
    pub fn get_public_key_x(&self) -> Result<Vec<u8>, &'static str> {
        let ec_point = unsafe { EC_KEY_get0_public_key(self.ec_key as *const EC_KEY) };
        let mut public_x_bignum = BigNum::new().ok_or("Failed to initialize BigNum!")?;
        let ec_group = unsafe { EC_KEY_get0_group(self.ec_key as *const EC_KEY) };
        let result = unsafe {
            EC_POINT_get_affine_coordinates_GFp(
                ec_group,
                ec_point,
                public_x_bignum.get_mut_ptr(),
                ptr::null_mut(),
                ptr::null_mut(),
            )
        };
        if result == 0 {
            return Err("Failed to convert point to x and y value!");
        }
        public_x_bignum.to_vec().ok_or("Failed to covert BigNum to vector!")
    }

    /// Gets the public key y for this EC key.
    pub fn get_public_key_y(&self) -> Result<Vec<u8>, &'static str> {
        let ec_point = unsafe { EC_KEY_get0_public_key(self.ec_key as *const EC_KEY) };
        let mut public_y_bignum = BigNum::new().ok_or("Failed to initialize BigNum!")?;
        let ec_group = unsafe { EC_KEY_get0_group(self.ec_key as *const EC_KEY) };
        let result = unsafe {
            EC_POINT_get_affine_coordinates_GFp(
                ec_group,
                ec_point,
                ptr::null_mut(),
                public_y_bignum.get_mut_ptr(),
                ptr::null_mut(),
            )
        };
        if result == 0 {
            return Err("Failed to convert point to x and y value!");
        }
        public_y_bignum.to_vec().ok_or("Failed to covert BigNum to vector!")
    }
}

impl<'a> Drop for EcPrivateKey<'a> {
    fn drop(&mut self) {
        unsafe {
            EC_KEY_free(self.ec_key as *mut EC_KEY);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use boringssl_sys::{NID_X9_62_prime256v1, NID_secp384r1, NID_secp521r1};
    use mundane::public::ec::*;
    use mundane::public::rsa::*;
    use mundane::public::*;

    #[test]
    fn test_ec_key() {
        // Right now only these algorithms are supported.
        test_ec_key_alg::<P256>(NID_X9_62_prime256v1 as i32);
        test_ec_key_alg::<P384>(NID_secp384r1 as i32);
        test_ec_key_alg::<P521>(NID_secp521r1 as i32);
    }

    fn test_ec_key_alg<C: PCurve>(nid: i32) {
        let ec_key = EcPrivKey::<C>::generate().unwrap();
        let ec_key_data = ec_key.marshal_to_der();
        let ec_private_key = EcPrivateKey::new(nid, &ec_key_data).unwrap();
        let _private_key_data = ec_private_key.get_private_key().unwrap();
        let public_key_x = ec_private_key.get_public_key_x().unwrap();
        let public_key_y = ec_private_key.get_public_key_y().unwrap();
        let mut ec_public_key = EcPublicKey::new(nid, &public_key_x, &public_key_y).unwrap();
        let public_key_data = ec_public_key.marshal_public_key().unwrap();
        assert_eq!(ec_key.public().marshal_to_der(), public_key_data);
    }

    #[test]
    fn test_rsa_key() {
        // Right now only these algorithms are supported.
        test_rsa_key_alg::<B2048>();
        test_rsa_key_alg::<B3072>();
        test_rsa_key_alg::<B4096>();
    }

    fn test_rsa_key_alg<B: RsaKeyBits>() {
        let rsa_key = RsaPrivKey::<B>::generate().unwrap();
        let rsa_key_data = rsa_key.marshal_to_der();
        let rsa_private_key = RsaPrivateKey::new(&rsa_key_data).unwrap();
        let _private_exponent = rsa_private_key.get_private_exponent().unwrap();
        let modulus = rsa_private_key.get_modulus().unwrap();
        let public_exponent = rsa_private_key.get_public_exponent().unwrap();
        let mut rsa_public_key = RsaPublicKey::new(&modulus, &public_exponent).unwrap();
        let public_key_data = rsa_public_key.marshal_public_key().unwrap();
        assert_eq!(rsa_key.public().marshal_to_der(), public_key_data);
    }
}
