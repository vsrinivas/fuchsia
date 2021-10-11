//! Cryptographic structures and functions.

use data_encoding::{BASE64URL, HEXLOWER};
use derp::{self, Der, Tag};
use ring::digest::{self, SHA256, SHA512};
use ring::rand::SystemRandom;
use ring::signature::{
    Ed25519KeyPair, KeyPair, RsaKeyPair, ED25519, RSA_PSS_2048_8192_SHA256,
    RSA_PSS_2048_8192_SHA512, RSA_PSS_SHA256, RSA_PSS_SHA512,
};
use serde::de::{Deserialize, Deserializer, Error as DeserializeError};
use serde::ser::{Error as SerializeError, Serialize, Serializer};
use serde_derive::{Deserialize, Serialize};
use std::cmp::Ordering;
use std::collections::HashMap;
use std::fmt::{self, Debug, Display};
use std::hash;
use std::io::{Read, Write};
use std::process::{Command, Stdio};
use std::str::FromStr;
use std::sync::Arc;
use untrusted::Input;

use crate::error::Error;
use crate::interchange::cjson::shims;
use crate::Result;

const HASH_ALG_PREFS: &[HashAlgorithm] = &[HashAlgorithm::Sha512, HashAlgorithm::Sha256];

/// 1.2.840.113549.1.1.1 rsaEncryption(PKCS #1)
const RSA_SPKI_OID: &[u8] = &[0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x01];

/// 1.3.101.112 curveEd25519(EdDSA 25519 signature algorithm)
const ED25519_SPKI_OID: &[u8] = &[0x2b, 0x65, 0x70];

/// The length of an ed25519 private key in bytes
const ED25519_PRIVATE_KEY_LENGTH: usize = 32;

/// The length of an ed25519 public key in bytes
const ED25519_PUBLIC_KEY_LENGTH: usize = 32;

/// The length of an ed25519 keypair in bytes
const ED25519_KEYPAIR_LENGTH: usize = ED25519_PRIVATE_KEY_LENGTH + ED25519_PUBLIC_KEY_LENGTH;

fn python_tuf_compatibility_keyid_hash_algorithms() -> Option<Vec<String>> {
    Some(vec!["sha256".to_string(), "sha512".to_string()])
}

/// Given a map of hash algorithms and their values, get the prefered algorithm and the hash
/// calculated by it. Returns an `Err` if there is no match.
///
/// ```
/// use std::collections::HashMap;
/// use tuf::crypto::{hash_preference, HashValue, HashAlgorithm};
///
/// let mut map = HashMap::new();
/// assert!(hash_preference(&map).is_err());
///
/// let _ = map.insert(HashAlgorithm::Sha512, HashValue::new(vec![0x00, 0x01]));
/// assert_eq!(hash_preference(&map).unwrap().0, &HashAlgorithm::Sha512);
///
/// let _ = map.insert(HashAlgorithm::Sha256, HashValue::new(vec![0x02, 0x03]));
/// assert_eq!(hash_preference(&map).unwrap().0, &HashAlgorithm::Sha512);
/// ```
pub fn hash_preference<'a>(
    hashes: &'a HashMap<HashAlgorithm, HashValue>,
) -> Result<(&'static HashAlgorithm, &'a HashValue)> {
    for alg in HASH_ALG_PREFS {
        match hashes.get(alg) {
            Some(v) => return Ok((alg, v)),
            None => continue,
        }
    }
    Err(Error::NoSupportedHashAlgorithm)
}

#[cfg(test)]
pub(crate) fn calculate_hash(data: &[u8], hash_alg: HashAlgorithm) -> HashValue {
    let mut context = hash_alg.digest_context().unwrap();
    context.update(data);
    HashValue::new(context.finish().as_ref().to_vec())
}

/// Calculate the size and hash digest from a given `Read`.
pub fn calculate_hashes<R: Read>(
    mut read: R,
    hash_algs: &[HashAlgorithm],
) -> Result<(u64, HashMap<HashAlgorithm, HashValue>)> {
    if hash_algs.is_empty() {
        return Err(Error::IllegalArgument(
            "Cannot provide empty set of hash algorithms".into(),
        ));
    }

    let mut size = 0;
    let mut hashes = HashMap::new();
    for alg in hash_algs {
        let _ = hashes.insert(alg, alg.digest_context()?);
    }

    let mut buf = vec![0; 1024];
    loop {
        match read.read(&mut buf) {
            Ok(read_bytes) => {
                if read_bytes == 0 {
                    break;
                }

                size += read_bytes as u64;

                for context in hashes.values_mut() {
                    context.update(&buf[0..read_bytes]);
                }
            }
            e @ Err(_) => e.map(|_| ())?,
        }
    }

    let hashes = hashes
        .drain()
        .map(|(k, v)| (k.clone(), HashValue::new(v.finish().as_ref().to_vec())))
        .collect();
    Ok((size, hashes))
}

fn shim_public_key(
    key_type: &KeyType,
    signature_scheme: &SignatureScheme,
    keyid_hash_algorithms: &Option<Vec<String>>,
    public_key: &[u8],
) -> ::std::result::Result<shims::PublicKey, derp::Error> {
    let key = match key_type {
        KeyType::Ed25519 => HEXLOWER.encode(public_key),
        KeyType::Rsa | KeyType::Unknown(_) => {
            let bytes = write_spki(public_key, &key_type)?;
            BASE64URL.encode(&bytes)
        }
    };

    Ok(shims::PublicKey::new(
        key_type.clone(),
        signature_scheme.clone(),
        keyid_hash_algorithms.clone(),
        key,
    ))
}

fn calculate_key_id(
    key_type: &KeyType,
    signature_scheme: &SignatureScheme,
    keyid_hash_algorithms: &Option<Vec<String>>,
    public_key: &[u8],
) -> Result<KeyId> {
    use crate::interchange::{DataInterchange, Json};

    let public_key = shim_public_key(
        key_type,
        signature_scheme,
        keyid_hash_algorithms,
        public_key,
    )?;
    let public_key = Json::canonicalize(&Json::serialize(&public_key)?)?;
    let mut context = digest::Context::new(&SHA256);
    context.update(&public_key);

    let key_id = HEXLOWER.encode(context.finish().as_ref());

    Ok(KeyId(key_id))
}

/// Wrapper type for public key's ID.
///
/// # Calculating
/// A `KeyId` is calculated as the hex digest of the SHA-256 hash of the canonical form of the
/// public key, or `hexdigest(sha256(cjson(public_key)))`.
#[derive(Clone, Debug, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct KeyId(String);

impl FromStr for KeyId {
    type Err = Error;

    /// Parse a key ID from a string.
    fn from_str(string: &str) -> Result<Self> {
        if string.len() != 64 {
            return Err(Error::IllegalArgument(
                "key ID must be 64 characters long".into(),
            ));
        }
        Ok(KeyId(string.to_owned()))
    }
}

impl Serialize for KeyId {
    fn serialize<S>(&self, ser: S) -> ::std::result::Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        self.0.serialize(ser)
    }
}

impl<'de> Deserialize<'de> for KeyId {
    fn deserialize<D: Deserializer<'de>>(de: D) -> ::std::result::Result<Self, D::Error> {
        let string: String = Deserialize::deserialize(de)?;
        KeyId::from_str(&string).map_err(|e| DeserializeError::custom(format!("{:?}", e)))
    }
}

/// Cryptographic signature schemes.
#[derive(Debug, Clone, PartialEq, Eq, Hash, Serialize, Deserialize)]
pub enum SignatureScheme {
    /// [Ed25519](https://ed25519.cr.yp.to/)
    #[serde(rename = "ed25519")]
    Ed25519,
    /// [RSASSA-PSS](https://tools.ietf.org/html/rfc5756) calculated over SHA256
    #[serde(rename = "rsassa-pss-sha256")]
    RsaSsaPssSha256,
    /// [RSASSA-PSS](https://tools.ietf.org/html/rfc5756) calculated over SHA512
    #[serde(rename = "rsassa-pss-sha512")]
    RsaSsaPssSha512,
    /// Placeholder for an unknown scheme.
    Unknown(String),
}

/// Wrapper type for the value of a cryptographic signature.
#[derive(Clone, PartialEq, Serialize, Deserialize)]
pub struct SignatureValue(#[serde(with = "crate::format_hex")] Vec<u8>);

impl SignatureValue {
    /// Create a new `SignatureValue` from the given bytes.
    ///
    /// Note: It is unlikely that you ever want to do this manually.
    pub fn new(bytes: Vec<u8>) -> Self {
        SignatureValue(bytes)
    }

    /// Create a new `SignatureValue` from the given hex string.
    ///
    /// Note: It is unlikely that you ever want to do this manually.
    pub fn from_hex(string: &str) -> Result<Self> {
        Ok(SignatureValue(HEXLOWER.decode(string.as_bytes())?))
    }

    /// Return the signature as bytes.
    pub fn as_bytes(&self) -> &[u8] {
        &self.0
    }
}

impl Debug for SignatureValue {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        f.debug_tuple("SignatureValue")
            .field(&HEXLOWER.encode(&self.0))
            .finish()
    }
}

/// Types of public keys.
#[derive(Clone, PartialEq, Debug, Eq, Hash)]
pub enum KeyType {
    /// [Ed25519](https://ed25519.cr.yp.to/)
    Ed25519,
    /// [RSA](https://en.wikipedia.org/wiki/RSA_%28cryptosystem%29)
    Rsa,
    /// Placeholder for an unknown key type.
    Unknown(String),
}

impl KeyType {
    fn from_oid(oid: &[u8]) -> Result<Self> {
        match oid {
            x if x == RSA_SPKI_OID => Ok(KeyType::Rsa),
            x if x == ED25519_SPKI_OID => Ok(KeyType::Ed25519),
            x => Err(Error::Encoding(format!(
                "Unknown OID: {}",
                x.iter().map(|b| format!("{:x}", b)).collect::<String>()
            ))),
        }
    }

    fn as_oid(&self) -> Result<&'static [u8]> {
        match *self {
            KeyType::Rsa => Ok(RSA_SPKI_OID),
            KeyType::Ed25519 => Ok(ED25519_SPKI_OID),
            KeyType::Unknown(ref s) => Err(Error::UnknownKeyType(s.clone())),
        }
    }
}

impl FromStr for KeyType {
    type Err = Error;

    fn from_str(s: &str) -> ::std::result::Result<Self, Self::Err> {
        match s {
            "ed25519" => Ok(KeyType::Ed25519),
            "rsa" => Ok(KeyType::Rsa),
            typ => Err(Error::Encoding(typ.into())),
        }
    }
}

impl ToString for KeyType {
    fn to_string(&self) -> String {
        match *self {
            KeyType::Ed25519 => "ed25519".to_string(),
            KeyType::Rsa => "rsa".to_string(),
            KeyType::Unknown(ref s) => s.to_string(),
        }
    }
}

impl Serialize for KeyType {
    fn serialize<S>(&self, ser: S) -> ::std::result::Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        ser.serialize_str(&self.to_string())
    }
}

impl<'de> Deserialize<'de> for KeyType {
    fn deserialize<D: Deserializer<'de>>(de: D) -> ::std::result::Result<Self, D::Error> {
        let string: String = Deserialize::deserialize(de)?;
        string
            .parse()
            .map_err(|e| DeserializeError::custom(format!("{:?}", e)))
    }
}

/// A structure containing information about a private key.
pub trait PrivateKey {
    /// Sign a message.
    fn sign(&self, msg: &[u8]) -> Result<Signature>;

    /// Return the public component of the key.
    fn public(&self) -> &PublicKey;
}

/// A structure containing information about an Ed25519 private key.
pub struct Ed25519PrivateKey {
    private: Ed25519KeyPair,
    public: PublicKey,
}

impl Ed25519PrivateKey {
    /// Generate Ed25519 key bytes in pkcs8 format.
    pub fn pkcs8() -> Result<Vec<u8>> {
        Ed25519KeyPair::generate_pkcs8(&SystemRandom::new())
            .map(|bytes| bytes.as_ref().to_vec())
            .map_err(|_| Error::Opaque("Failed to generate Ed25519 key".into()))
    }

    /// Create a new `PrivateKey` from an ed25519 keypair, a 64 byte slice, where the first 32
    /// bytes are the ed25519 seed, and the second 32 bytes are the public key.
    pub fn from_ed25519(key: &[u8]) -> Result<Self> {
        Self::from_ed25519_with_keyid_hash_algorithms(key, None)
    }

    fn from_ed25519_with_keyid_hash_algorithms(
        key: &[u8],
        keyid_hash_algorithms: Option<Vec<String>>,
    ) -> Result<Self> {
        if key.len() != ED25519_KEYPAIR_LENGTH {
            return Err(Error::Encoding(
                "ed25519 private keys must be 64 bytes long".into(),
            ));
        }

        let private_key_bytes = &key[..ED25519_PRIVATE_KEY_LENGTH];
        let public_key_bytes = &key[ED25519_PUBLIC_KEY_LENGTH..];

        let private = Ed25519KeyPair::from_seed_and_public_key(private_key_bytes, public_key_bytes)
            .map_err(|err| Error::Encoding(err.to_string()))?;
        Self::from_keypair_with_keyid_hash_algorithms(private, keyid_hash_algorithms)
    }

    /// Create a private key from PKCS#8v2 DER bytes.
    ///
    /// # Generating Keys
    ///
    /// ```bash
    /// $ touch ed25519-private-key.pk8
    /// $ chmod 0600 ed25519-private-key.pk8
    /// ```
    ///
    /// ```no_run
    /// # use ring::rand::SystemRandom;
    /// # use ring::signature::Ed25519KeyPair;
    /// # use std::fs::File;
    /// # use std::io::Write;
    /// #
    /// let mut file = File::open("ed25519-private-key.pk8").unwrap();
    /// let key = Ed25519KeyPair::generate_pkcs8(&SystemRandom::new()).unwrap();
    /// file.write_all(key.as_ref()).unwrap()
    /// ```
    pub fn from_pkcs8(der_key: &[u8]) -> Result<Self> {
        Self::from_pkcs8_with_keyid_hash_algorithms(
            der_key,
            python_tuf_compatibility_keyid_hash_algorithms(),
        )
    }

    fn from_pkcs8_with_keyid_hash_algorithms(
        der_key: &[u8],
        keyid_hash_algorithms: Option<Vec<String>>,
    ) -> Result<Self> {
        Self::from_keypair_with_keyid_hash_algorithms(
            Ed25519KeyPair::from_pkcs8(der_key)
                .map_err(|_| Error::Encoding("Could not parse key as PKCS#8v2".into()))?,
            keyid_hash_algorithms,
        )
    }

    fn from_keypair_with_keyid_hash_algorithms(
        private: Ed25519KeyPair,
        keyid_hash_algorithms: Option<Vec<String>>,
    ) -> Result<Self> {
        let public = PublicKey::new(
            KeyType::Ed25519,
            SignatureScheme::Ed25519,
            keyid_hash_algorithms,
            private.public_key().as_ref().to_vec(),
        )?;

        Ok(Ed25519PrivateKey { private, public })
    }
}

impl PrivateKey for Ed25519PrivateKey {
    fn sign(&self, msg: &[u8]) -> Result<Signature> {
        debug_assert!(self.public.scheme == SignatureScheme::Ed25519);

        let value = SignatureValue(self.private.sign(msg).as_ref().into());
        Ok(Signature {
            key_id: self.public.key_id().clone(),
            value,
        })
    }

    fn public(&self) -> &PublicKey {
        &self.public
    }
}

/// A structure containing information about an Rsa private key.
pub struct RsaPrivateKey {
    private: Arc<RsaKeyPair>,
    public: PublicKey,
}

impl RsaPrivateKey {
    /// Generate RSA key bytes in pkcs8 format.
    ///
    /// Note: `openssl` needs to the on the `$PATH`.
    pub fn pkcs8() -> Result<Vec<u8>> {
        let gen = Command::new("openssl")
            .args(&[
                "genpkey",
                "-algorithm",
                "RSA",
                "-pkeyopt",
                "rsa_keygen_bits:4096",
                "-pkeyopt",
                "rsa_keygen_pubexp:65537",
                "-outform",
                "der",
            ])
            .output()?;

        let mut pk8 = Command::new("openssl")
            .args(&[
                "pkcs8", "-inform", "der", "-topk8", "-nocrypt", "-outform", "der",
            ])
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .spawn()?;

        match pk8.stdin {
            Some(ref mut stdin) => stdin.write_all(&gen.stdout)?,
            None => return Err(Error::Opaque("openssl has no stdin".into())),
        };

        Ok(pk8.wait_with_output()?.stdout)
    }

    /// Create a private key from PKCS#8v2 DER bytes.
    ///
    /// # Generating Keys
    ///
    /// ```bash
    /// $ umask 077
    /// $ openssl genpkey -algorithm RSA \
    ///     -pkeyopt rsa_keygen_bits:4096 \
    ///     -pkeyopt rsa_keygen_pubexp:65537 | \
    ///     openssl pkcs8 -topk8 -nocrypt -outform der > rsa-4096-private-key.pk8
    /// ```
    pub fn from_pkcs8(der_key: &[u8], scheme: SignatureScheme) -> Result<Self> {
        match scheme {
            SignatureScheme::RsaSsaPssSha256 | SignatureScheme::RsaSsaPssSha512 => (),
            _ => Err(Error::IllegalArgument(format!(
                "RSA keys do not support the signing scheme {:?}",
                scheme
            )))?,
        }

        let key = RsaKeyPair::from_pkcs8(der_key)
            .map_err(|_| Error::Encoding("Could not parse key as PKCS#8v2".into()))?;

        if key.public_modulus_len() < 256 {
            return Err(Error::IllegalArgument(format!(
                "RSA public modulus must be 2048 or greater. Found {}",
                key.public_modulus_len() * 8
            )));
        }

        let pub_key = extract_rsa_pub_from_pkcs8(der_key)?;

        let public = PublicKey::new(
            KeyType::Rsa,
            scheme,
            python_tuf_compatibility_keyid_hash_algorithms(),
            pub_key,
        )?;
        let private = Arc::new(key);

        Ok(RsaPrivateKey { private, public })
    }
}

impl PrivateKey for RsaPrivateKey {
    fn sign(&self, msg: &[u8]) -> Result<Signature> {
        let rng = SystemRandom::new();
        let mut buf = vec![0; self.private.public_modulus_len()];
        let scheme = match &self.public.scheme {
            SignatureScheme::RsaSsaPssSha256 => &RSA_PSS_SHA256,
            SignatureScheme::RsaSsaPssSha512 => &RSA_PSS_SHA512,
            s => unreachable!("Key {:?} can't be used with scheme {:?}", self.private, s),
        };

        self.private
            .sign(scheme, &rng, msg, &mut buf)
            .map_err(|_| Error::Opaque("Failed to sign message.".into()))?;
        let value = SignatureValue(buf);

        Ok(Signature {
            key_id: self.public.key_id().clone(),
            value,
        })
    }

    fn public(&self) -> &PublicKey {
        &self.public
    }
}

/// A structure containing information about a public key.
#[derive(Clone, Debug)]
pub struct PublicKey {
    typ: KeyType,
    key_id: KeyId,
    scheme: SignatureScheme,
    keyid_hash_algorithms: Option<Vec<String>>,
    value: PublicKeyValue,
}

impl PublicKey {
    fn new(
        typ: KeyType,
        scheme: SignatureScheme,
        keyid_hash_algorithms: Option<Vec<String>>,
        value: Vec<u8>,
    ) -> Result<Self> {
        let key_id = calculate_key_id(&typ, &scheme, &keyid_hash_algorithms, &value)?;
        let value = PublicKeyValue(value);
        Ok(PublicKey {
            typ,
            key_id,
            scheme,
            keyid_hash_algorithms,
            value,
        })
    }

    /// Parse DER bytes as an SPKI key.
    ///
    /// See the documentation on `KeyValue` for more information on SPKI.
    pub fn from_spki(der_bytes: &[u8], scheme: SignatureScheme) -> Result<Self> {
        Self::from_spki_with_keyid_hash_algorithms(
            der_bytes,
            scheme,
            python_tuf_compatibility_keyid_hash_algorithms(),
        )
    }

    /// Parse DER bytes as an SPKI key and the `keyid_hash_algorithms`.
    ///
    /// See the documentation on `KeyValue` for more information on SPKI.
    fn from_spki_with_keyid_hash_algorithms(
        der_bytes: &[u8],
        scheme: SignatureScheme,
        keyid_hash_algorithms: Option<Vec<String>>,
    ) -> Result<Self> {
        let input = Input::from(der_bytes);

        let (typ, value) = input.read_all(derp::Error::Read, |input| {
            derp::nested(input, Tag::Sequence, |input| {
                let typ = derp::nested(input, Tag::Sequence, |input| {
                    let typ = derp::expect_tag_and_get_value(input, Tag::Oid)?;

                    let typ = KeyType::from_oid(typ.as_slice_less_safe())
                        .map_err(|_| derp::Error::WrongValue)?;

                    // for RSA / ed25519 this is null, so don't both parsing it
                    derp::read_null(input)?;
                    Ok(typ)
                })?;
                let value = derp::bit_string_with_no_unused_bits(input)?;
                Ok((typ, value.as_slice_less_safe().to_vec()))
            })
        })?;

        Self::new(typ, scheme, keyid_hash_algorithms, value)
    }

    /// Parse ED25519 bytes as a public key.
    pub fn from_ed25519<T: Into<Vec<u8>>>(bytes: T) -> Result<Self> {
        Self::from_ed25519_with_keyid_hash_algorithms(bytes, None)
    }

    /// Parse ED25519 bytes as a public key with a custom `keyid_hash_algorithms`.
    pub fn from_ed25519_with_keyid_hash_algorithms<T: Into<Vec<u8>>>(
        bytes: T,
        keyid_hash_algorithms: Option<Vec<String>>,
    ) -> Result<Self> {
        let bytes = bytes.into();
        if bytes.len() != 32 {
            return Err(Error::IllegalArgument(
                "ed25519 keys must be 32 bytes long".into(),
            ));
        }

        Self::new(
            KeyType::Ed25519,
            SignatureScheme::Ed25519,
            keyid_hash_algorithms,
            bytes,
        )
    }

    /// Write the public key as SPKI DER bytes.
    ///
    /// See the documentation on `KeyValue` for more information on SPKI.
    pub fn as_spki(&self) -> Result<Vec<u8>> {
        Ok(write_spki(&self.value.0, &self.typ)?)
    }

    /// An immutable reference to the key's type.
    pub fn typ(&self) -> &KeyType {
        &self.typ
    }

    /// An immutable referece to the key's authorized signing scheme.
    pub fn scheme(&self) -> &SignatureScheme {
        &self.scheme
    }

    /// An immutable reference to the key's ID.
    pub fn key_id(&self) -> &KeyId {
        &self.key_id
    }

    /// Return the public key as bytes.
    pub fn as_bytes(&self) -> &[u8] {
        &self.value.0
    }

    /// Use this key to verify a message with a signature.
    pub fn verify(&self, msg: &[u8], sig: &Signature) -> Result<()> {
        let alg: &dyn ring::signature::VerificationAlgorithm = match self.scheme {
            SignatureScheme::Ed25519 => &ED25519,
            SignatureScheme::RsaSsaPssSha256 => &RSA_PSS_2048_8192_SHA256,
            SignatureScheme::RsaSsaPssSha512 => &RSA_PSS_2048_8192_SHA512,
            SignatureScheme::Unknown(ref s) => {
                return Err(Error::IllegalArgument(format!(
                    "Unknown signature scheme: {}",
                    s
                )));
            }
        };

        let key = ring::signature::UnparsedPublicKey::new(alg, &self.value.0);
        key.verify(msg, &sig.value.0)
            .map_err(|_| Error::BadSignature)
    }
}

impl PartialEq for PublicKey {
    fn eq(&self, other: &Self) -> bool {
        // key_id is derived from these fields, so we ignore it.
        self.typ == other.typ
            && self.scheme == other.scheme
            && self.keyid_hash_algorithms == other.keyid_hash_algorithms
            && self.value == other.value
    }
}

impl Eq for PublicKey {}

impl Ord for PublicKey {
    fn cmp(&self, other: &Self) -> Ordering {
        self.key_id.cmp(&other.key_id)
    }
}

impl PartialOrd for PublicKey {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.key_id.cmp(&other.key_id))
    }
}

impl hash::Hash for PublicKey {
    fn hash<H: hash::Hasher>(&self, state: &mut H) {
        // key_id is derived from these fields, so we ignore it.
        self.typ.hash(state);
        self.scheme.hash(state);
        self.keyid_hash_algorithms.hash(state);
        self.value.hash(state);
    }
}

impl Serialize for PublicKey {
    fn serialize<S>(&self, ser: S) -> ::std::result::Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        let key = shim_public_key(
            &self.typ,
            &self.scheme,
            &self.keyid_hash_algorithms,
            &self.value.0,
        )
        .map_err(|e| SerializeError::custom(format!("Couldn't write key as SPKI: {:?}", e)))?;
        key.serialize(ser)
    }
}

impl<'de> Deserialize<'de> for PublicKey {
    fn deserialize<D: Deserializer<'de>>(de: D) -> ::std::result::Result<Self, D::Error> {
        let intermediate: shims::PublicKey = Deserialize::deserialize(de)?;

        let key = match intermediate.keytype() {
            KeyType::Ed25519 => {
                if intermediate.scheme() != &SignatureScheme::Ed25519 {
                    return Err(DeserializeError::custom(format!(
                        "ed25519 key type must be used with the ed25519 signature scheme, not {:?}",
                        intermediate.scheme()
                    )));
                }

                let bytes = HEXLOWER
                    .decode(intermediate.public_key().as_bytes())
                    .map_err(|e| {
                        DeserializeError::custom(format!("Couldn't parse key as HEX: {:?}", e))
                    })?;

                PublicKey::from_ed25519_with_keyid_hash_algorithms(
                    bytes,
                    intermediate.keyid_hash_algorithms().clone(),
                )
                .map_err(|e| {
                    DeserializeError::custom(format!("Couldn't parse key as ed25519: {:?}", e))
                })?
            }
            KeyType::Rsa | KeyType::Unknown(_) => {
                let bytes = BASE64URL
                    .decode(intermediate.public_key().as_bytes())
                    .map_err(|e| DeserializeError::custom(format!("{:?}", e)))?;

                PublicKey::from_spki_with_keyid_hash_algorithms(
                    &bytes,
                    intermediate.scheme().clone(),
                    intermediate.keyid_hash_algorithms().clone(),
                )
                .map_err(|e| {
                    DeserializeError::custom(format!("Couldn't parse key as SPKI: {:?}", e))
                })?
            }
        };

        if intermediate.keytype() != &key.typ {
            return Err(DeserializeError::custom(format!(
                "Key type listed in the metadata did not match the type extrated \
                 from the key. {:?} vs. {:?}",
                intermediate.keytype(),
                key.typ,
            )));
        }

        Ok(key)
    }
}

#[derive(Clone, PartialEq, Hash, Eq)]
struct PublicKeyValue(Vec<u8>);

impl Debug for PublicKeyValue {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        f.debug_tuple("PublicKeyValue")
            .field(&HEXLOWER.encode(&self.0))
            .finish()
    }
}

/// A structure that contains a `Signature` and associated data for verifying it.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct Signature {
    #[serde(rename = "keyid")]
    key_id: KeyId,
    #[serde(rename = "sig")]
    value: SignatureValue,
}

impl Signature {
    /// An immutable reference to the `KeyId` of the key that produced the signature.
    pub fn key_id(&self) -> &KeyId {
        &self.key_id
    }

    /// An immutable reference to the `SignatureValue`.
    pub fn value(&self) -> &SignatureValue {
        &self.value
    }
}

/// The available hash algorithms.
#[derive(Debug, Clone, Hash, PartialEq, Eq, PartialOrd, Ord, Serialize, Deserialize)]
pub enum HashAlgorithm {
    /// SHA256 as describe in [RFC-6234](https://tools.ietf.org/html/rfc6234)
    #[serde(rename = "sha256")]
    Sha256,
    /// SHA512 as describe in [RFC-6234](https://tools.ietf.org/html/rfc6234)
    #[serde(rename = "sha512")]
    Sha512,
    /// Placeholder for an unknown hash algorithm.
    Unknown(String),
}

impl HashAlgorithm {
    /// Create a new `digest::Context` suitable for computing the hash of some data using this hash
    /// algorithm.
    pub(crate) fn digest_context(&self) -> Result<digest::Context> {
        match self {
            HashAlgorithm::Sha256 => Ok(digest::Context::new(&SHA256)),
            HashAlgorithm::Sha512 => Ok(digest::Context::new(&SHA512)),
            HashAlgorithm::Unknown(ref s) => Err(Error::IllegalArgument(format!(
                "Unknown hash algorithm: {}",
                s
            ))),
        }
    }
}

/// Wrapper for the value of a hash digest.
#[derive(Clone, Eq, PartialEq, Hash, Serialize, Deserialize)]
pub struct HashValue(#[serde(with = "crate::format_hex")] Vec<u8>);

impl HashValue {
    /// Create a new `HashValue` from the given digest bytes.
    pub fn new(bytes: Vec<u8>) -> Self {
        HashValue(bytes)
    }

    /// An immutable reference to the bytes of the hash value.
    pub fn value(&self) -> &[u8] {
        &self.0
    }
}

impl Debug for HashValue {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        f.debug_tuple("HashValue")
            .field(&HEXLOWER.encode(&self.0))
            .finish()
    }
}

impl Display for HashValue {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "{}", HEXLOWER.encode(&self.0))
    }
}

fn write_spki(public: &[u8], key_type: &KeyType) -> ::std::result::Result<Vec<u8>, derp::Error> {
    let mut output = Vec::new();
    {
        let mut der = Der::new(&mut output);
        der.sequence(|der| {
            der.sequence(|der| match key_type.as_oid().ok() {
                Some(tag) => {
                    der.element(Tag::Oid, tag)?;
                    der.null()
                }
                None => Err(derp::Error::WrongValue),
            })?;
            der.bit_string(0, public)
        })?;
    }

    Ok(output)
}

fn extract_rsa_pub_from_pkcs8(der_key: &[u8]) -> ::std::result::Result<Vec<u8>, derp::Error> {
    let input = Input::from(der_key);
    input.read_all(derp::Error::Read, |input| {
        derp::nested(input, Tag::Sequence, |input| {
            if derp::small_nonnegative_integer(input)? != 0 {
                return Err(derp::Error::WrongValue);
            }

            derp::nested(input, Tag::Sequence, |input| {
                let actual_alg_id = derp::expect_tag_and_get_value(input, Tag::Oid)?;
                if actual_alg_id.as_slice_less_safe() != RSA_SPKI_OID {
                    return Err(derp::Error::WrongValue);
                }
                let _ = derp::expect_tag_and_get_value(input, Tag::Null)?;
                Ok(())
            })?;

            derp::nested(input, Tag::OctetString, |input| {
                derp::nested(input, Tag::Sequence, |input| {
                    if derp::small_nonnegative_integer(input)? != 0 {
                        return Err(derp::Error::WrongValue);
                    }

                    let n = derp::positive_integer(input)?;
                    let e = derp::positive_integer(input)?;
                    let _ = input.skip_to_end();
                    write_pkcs1(n.as_slice_less_safe(), e.as_slice_less_safe())
                })
            })
        })
    })
}

fn write_pkcs1(n: &[u8], e: &[u8]) -> ::std::result::Result<Vec<u8>, derp::Error> {
    let mut output = Vec::new();
    {
        let mut der = Der::new(&mut output);
        der.sequence(|der| {
            der.positive_integer(n)?;
            der.positive_integer(e)
        })?;
    }

    Ok(output)
}

#[cfg(test)]
mod test {
    use super::*;
    use matches::assert_matches;
    use pretty_assertions::assert_eq;
    use serde_json::{self, json};

    const RSA_2048_PK8: &'static [u8] = include_bytes!("../tests/rsa/rsa-2048.pk8.der");
    const RSA_2048_SPKI: &'static [u8] = include_bytes!("../tests/rsa/rsa-2048.spki.der");
    const RSA_2048_PKCS1: &'static [u8] = include_bytes!("../tests/rsa/rsa-2048.pkcs1.der");

    const RSA_4096_PK8: &'static [u8] = include_bytes!("../tests/rsa/rsa-4096.pk8.der");
    const RSA_4096_SPKI: &'static [u8] = include_bytes!("../tests/rsa/rsa-4096.spki.der");
    const RSA_4096_PKCS1: &'static [u8] = include_bytes!("../tests/rsa/rsa-4096.pkcs1.der");

    const ED25519_1_PRIVATE_KEY: &'static [u8] = include_bytes!("../tests/ed25519/ed25519-1");
    const ED25519_1_PUBLIC_KEY: &'static [u8] = include_bytes!("../tests/ed25519/ed25519-1.pub");
    const ED25519_1_PK8: &'static [u8] = include_bytes!("../tests/ed25519/ed25519-1.pk8.der");
    const ED25519_1_SPKI: &'static [u8] = include_bytes!("../tests/ed25519/ed25519-1.spki.der");
    const ED25519_2_PK8: &'static [u8] = include_bytes!("../tests/ed25519/ed25519-2.pk8.der");

    #[test]
    fn parse_public_rsa_2048_spki() {
        let key = PublicKey::from_spki(RSA_2048_SPKI, SignatureScheme::RsaSsaPssSha256).unwrap();
        assert_eq!(key.typ, KeyType::Rsa);
        assert_eq!(key.scheme, SignatureScheme::RsaSsaPssSha256);
    }

    #[test]
    fn parse_public_rsa_4096_spki() {
        let key = PublicKey::from_spki(RSA_4096_SPKI, SignatureScheme::RsaSsaPssSha256).unwrap();
        assert_eq!(key.typ, KeyType::Rsa);
        assert_eq!(key.scheme, SignatureScheme::RsaSsaPssSha256);
    }

    #[test]
    fn parse_public_ed25519_spki() {
        let key = PublicKey::from_spki(ED25519_1_SPKI, SignatureScheme::Ed25519).unwrap();
        assert_eq!(key.typ, KeyType::Ed25519);
        assert_eq!(key.scheme, SignatureScheme::Ed25519);
    }

    #[test]
    fn parse_public_ed25519() {
        let key = PublicKey::from_ed25519(ED25519_1_PUBLIC_KEY).unwrap();
        assert_eq!(
            key.key_id(),
            &KeyId::from_str("e0294a3f17cc8563c3ed5fceb3bd8d3f6bfeeaca499b5c9572729ae015566554")
                .unwrap()
        );
        assert_eq!(key.typ, KeyType::Ed25519);
        assert_eq!(key.scheme, SignatureScheme::Ed25519);
    }

    #[test]
    fn parse_public_ed25519_without_keyid_hash_algo() {
        let key =
            PublicKey::from_ed25519_with_keyid_hash_algorithms(ED25519_1_PUBLIC_KEY, None).unwrap();
        assert_eq!(
            key.key_id(),
            &KeyId::from_str("e0294a3f17cc8563c3ed5fceb3bd8d3f6bfeeaca499b5c9572729ae015566554")
                .unwrap()
        );
        assert_eq!(key.typ, KeyType::Ed25519);
        assert_eq!(key.scheme, SignatureScheme::Ed25519);
    }

    #[test]
    fn parse_public_ed25519_with_keyid_hash_algo() {
        let key = PublicKey::from_ed25519_with_keyid_hash_algorithms(
            ED25519_1_PUBLIC_KEY,
            python_tuf_compatibility_keyid_hash_algorithms(),
        )
        .unwrap();
        assert_eq!(
            key.key_id(),
            &KeyId::from_str("a9f3ebc9b138762563a9c27b6edd439959e559709babd123e8d449ba2c18c61a")
                .unwrap(),
        );
        assert_eq!(key.typ, KeyType::Ed25519);
        assert_eq!(key.scheme, SignatureScheme::Ed25519);
    }

    #[test]
    fn rsa_2048_read_pkcs8_and_sign() {
        let msg = b"test";

        let key =
            RsaPrivateKey::from_pkcs8(RSA_2048_PK8, SignatureScheme::RsaSsaPssSha256).unwrap();
        let sig = key.sign(msg).unwrap();
        key.public.verify(msg, &sig).unwrap();

        let key =
            RsaPrivateKey::from_pkcs8(RSA_2048_PK8, SignatureScheme::RsaSsaPssSha512).unwrap();
        let sig = key.sign(msg).unwrap();
        key.public.verify(msg, &sig).unwrap();
    }

    #[test]
    fn rsa_4096_read_pkcs8_and_sign() {
        let msg = b"test";

        let key =
            RsaPrivateKey::from_pkcs8(RSA_4096_PK8, SignatureScheme::RsaSsaPssSha256).unwrap();
        let sig = key.sign(msg).unwrap();
        key.public.verify(msg, &sig).unwrap();

        let key =
            RsaPrivateKey::from_pkcs8(RSA_4096_PK8, SignatureScheme::RsaSsaPssSha512).unwrap();
        let sig = key.sign(msg).unwrap();
        key.public.verify(msg, &sig).unwrap();
    }

    #[test]
    fn extract_pkcs1_from_rsa_2048_pkcs8() {
        let res = extract_rsa_pub_from_pkcs8(RSA_2048_PK8).unwrap();
        assert_eq!(res.as_slice(), RSA_2048_PKCS1);
    }

    #[test]
    fn extract_pkcs1_from_rsa_4096_pkcs8() {
        let res = extract_rsa_pub_from_pkcs8(RSA_4096_PK8).unwrap();
        assert_eq!(res.as_slice(), RSA_4096_PKCS1);
    }

    #[test]
    fn ed25519_read_pkcs8_and_sign() {
        let key = Ed25519PrivateKey::from_pkcs8(ED25519_1_PK8).unwrap();
        let msg = b"test";

        let sig = key.sign(msg).unwrap();

        let pub_key =
            PublicKey::from_spki(&key.public.as_spki().unwrap(), SignatureScheme::Ed25519).unwrap();

        assert_matches!(pub_key.verify(msg, &sig), Ok(()));

        // Make sure we match what ring expects.
        let ring_key = ring::signature::Ed25519KeyPair::from_pkcs8(ED25519_1_PK8).unwrap();
        assert_eq!(key.public().as_bytes(), ring_key.public_key().as_ref());
        assert_eq!(sig.value().as_bytes(), ring_key.sign(msg).as_ref());

        // Make sure verification fails with the wrong key.
        let bad_pub_key = Ed25519PrivateKey::from_pkcs8(ED25519_2_PK8)
            .unwrap()
            .public()
            .clone();

        assert_matches!(bad_pub_key.verify(msg, &sig), Err(Error::BadSignature));
    }

    #[test]
    fn ed25519_read_keypair_and_sign() {
        let key = Ed25519PrivateKey::from_ed25519(ED25519_1_PRIVATE_KEY).unwrap();
        let pub_key = PublicKey::from_ed25519(ED25519_1_PUBLIC_KEY).unwrap();
        assert_eq!(key.public(), &pub_key);

        let msg = b"test";
        let sig = key.sign(msg).unwrap();
        assert_matches!(pub_key.verify(msg, &sig), Ok(()));

        // Make sure we match what ring expects.
        let ring_key = ring::signature::Ed25519KeyPair::from_pkcs8(ED25519_1_PK8).unwrap();
        assert_eq!(key.public().as_bytes(), ring_key.public_key().as_ref());
        assert_eq!(sig.value().as_bytes(), ring_key.sign(msg).as_ref());

        // Make sure verification fails with the wrong key.
        let bad_pub_key = Ed25519PrivateKey::from_pkcs8(ED25519_2_PK8)
            .unwrap()
            .public()
            .clone();

        assert_matches!(bad_pub_key.verify(msg, &sig), Err(Error::BadSignature));
    }

    #[test]
    fn ed25519_read_keypair_and_sign_with_keyid_hash_algorithms() {
        let key = Ed25519PrivateKey::from_ed25519_with_keyid_hash_algorithms(
            ED25519_1_PRIVATE_KEY,
            python_tuf_compatibility_keyid_hash_algorithms(),
        )
        .unwrap();
        let pub_key = PublicKey::from_ed25519_with_keyid_hash_algorithms(
            ED25519_1_PUBLIC_KEY,
            python_tuf_compatibility_keyid_hash_algorithms(),
        )
        .unwrap();
        assert_eq!(key.public(), &pub_key);

        let msg = b"test";
        let sig = key.sign(msg).unwrap();
        assert_matches!(pub_key.verify(msg, &sig), Ok(()));

        // Make sure we match what ring expects.
        let ring_key = ring::signature::Ed25519KeyPair::from_pkcs8(ED25519_1_PK8).unwrap();
        assert_eq!(key.public().as_bytes(), ring_key.public_key().as_ref());
        assert_eq!(sig.value().as_bytes(), ring_key.sign(msg).as_ref());

        // Make sure verification fails with the wrong key.
        let bad_pub_key = Ed25519PrivateKey::from_pkcs8(ED25519_2_PK8)
            .unwrap()
            .public()
            .clone();

        assert_matches!(bad_pub_key.verify(msg, &sig), Err(Error::BadSignature));
    }

    #[test]
    fn serde_key_id() {
        let s = "4750eaf6878740780d6f97b12dbad079fb012bec88c78de2c380add56d3f51db";
        let jsn = json!(s);
        let parsed: KeyId = serde_json::from_str(&format!("\"{}\"", s)).unwrap();
        assert_eq!(parsed, KeyId::from_str(s).unwrap());
        let encoded = serde_json::to_value(&parsed).unwrap();
        assert_eq!(encoded, jsn);
    }

    #[test]
    fn serde_signature_value() {
        let s = "4750eaf6878740780d6f97b12dbad079fb012bec88c78de2c380add56d3f51db";
        let jsn = json!(s);
        let parsed: SignatureValue = serde_json::from_str(&format!("\"{}\"", s)).unwrap();
        assert_eq!(parsed, SignatureValue::from_hex(s).unwrap());
        let encoded = serde_json::to_value(&parsed).unwrap();
        assert_eq!(encoded, jsn);
    }

    #[test]
    fn serde_rsa_public_key() {
        let der = RSA_2048_SPKI;
        let pub_key = PublicKey::from_spki(der, SignatureScheme::RsaSsaPssSha256).unwrap();
        let encoded = serde_json::to_value(&pub_key).unwrap();
        let jsn = json!({
            "keytype": "rsa",
            "scheme": "rsassa-pss-sha256",
            "keyid_hash_algorithms": ["sha256", "sha512"],
            "keyval": {
                "public": BASE64URL.encode(der),
            }
        });
        assert_eq!(encoded, jsn);
        let decoded: PublicKey = serde_json::from_value(encoded).unwrap();
        assert_eq!(decoded, pub_key);
    }

    #[test]
    fn de_ser_rsa_public_key_with_keyid_hash_algo() {
        let original = json!({
            "keytype": "rsa",
            "scheme": "rsassa-pss-sha256",
            "keyid_hash_algorithms": ["sha256", "sha512"],
            "keyval": {
                "public": BASE64URL.encode(RSA_2048_SPKI),
            }
        });

        let decoded: PublicKey = serde_json::from_value(original.clone()).unwrap();
        let encoded = serde_json::to_value(&decoded).unwrap();

        assert_eq!(original, encoded);
    }

    #[test]
    fn de_ser_rsa_public_key_without_keyid_hash_algo() {
        let original = json!({
            "keytype": "rsa",
            "scheme": "rsassa-pss-sha256",
            "keyval": {
                "public": BASE64URL.encode(RSA_2048_SPKI),
            }
        });

        let decoded: PublicKey = serde_json::from_value(original.clone()).unwrap();
        let encoded = serde_json::to_value(&decoded).unwrap();

        assert_eq!(original, encoded);
    }

    #[test]
    fn serde_ed25519_public_key() {
        let pub_key = Ed25519PrivateKey::from_pkcs8(ED25519_1_PK8)
            .unwrap()
            .public()
            .clone();

        let pub_key = PublicKey::from_ed25519_with_keyid_hash_algorithms(
            pub_key.as_bytes().to_vec(),
            python_tuf_compatibility_keyid_hash_algorithms(),
        )
        .unwrap();
        let encoded = serde_json::to_value(&pub_key).unwrap();
        let jsn = json!({
            "keytype": "ed25519",
            "scheme": "ed25519",
            "keyid_hash_algorithms": ["sha256", "sha512"],
            "keyval": {
                "public": HEXLOWER.encode(pub_key.as_bytes()),
            }
        });
        assert_eq!(encoded, jsn);
        let decoded: PublicKey = serde_json::from_value(encoded).unwrap();
        assert_eq!(decoded, pub_key);
    }

    #[test]
    fn de_ser_ed25519_public_key_with_keyid_hash_algo() {
        let pub_key = Ed25519PrivateKey::from_pkcs8(ED25519_1_PK8)
            .unwrap()
            .public()
            .clone();
        let pub_key = PublicKey::from_ed25519_with_keyid_hash_algorithms(
            pub_key.as_bytes().to_vec(),
            python_tuf_compatibility_keyid_hash_algorithms(),
        )
        .unwrap();
        let original = json!({
            "keytype": "ed25519",
            "scheme": "ed25519",
            "keyid_hash_algorithms": ["sha256", "sha512"],
            "keyval": {
                "public": HEXLOWER.encode(pub_key.as_bytes()),
            }
        });

        let encoded: PublicKey = serde_json::from_value(original.clone()).unwrap();
        let decoded = serde_json::to_value(&encoded).unwrap();

        assert_eq!(original, decoded);
    }

    #[test]
    fn de_ser_ed25519_public_key_without_keyid_hash_algo() {
        let pub_key = Ed25519PrivateKey::from_pkcs8(ED25519_1_PK8)
            .unwrap()
            .public()
            .clone();
        let pub_key =
            PublicKey::from_ed25519_with_keyid_hash_algorithms(pub_key.as_bytes().to_vec(), None)
                .unwrap();
        let original = json!({
            "keytype": "ed25519",
            "scheme": "ed25519",
            "keyval": {
                "public": HEXLOWER.encode(pub_key.as_bytes()),
            }
        });

        let encoded: PublicKey = serde_json::from_value(original.clone()).unwrap();
        let decoded = serde_json::to_value(&encoded).unwrap();

        assert_eq!(original, decoded);
    }

    #[test]
    fn serde_signature() {
        let key = Ed25519PrivateKey::from_pkcs8(ED25519_1_PK8).unwrap();
        let msg = b"test";
        let sig = key.sign(msg).unwrap();
        let encoded = serde_json::to_value(&sig).unwrap();
        let jsn = json!({
            "keyid": "a9f3ebc9b138762563a9c27b6edd439959e559709babd123e8d449ba2c18c61a",
            "sig": "fe4d13b2a73c033a1de7f5107b205fc7ba0e1566cb95b92349cae6aa453\
                8956013bfe0f7bf977cb072bb65e8782b5f33a0573fe78816299a017ca5ba55\
                9e390c",
        });
        assert_eq!(encoded, jsn);

        let decoded: Signature = serde_json::from_value(encoded).unwrap();
        assert_eq!(decoded, sig);
    }

    #[test]
    fn serde_signature_without_keyid_hash_algo() {
        let key =
            Ed25519PrivateKey::from_pkcs8_with_keyid_hash_algorithms(ED25519_1_PK8, None).unwrap();
        let msg = b"test";
        let sig = key.sign(msg).unwrap();
        let encoded = serde_json::to_value(&sig).unwrap();
        let jsn = json!({
            "keyid": "e0294a3f17cc8563c3ed5fceb3bd8d3f6bfeeaca499b5c9572729ae015566554",
            "sig": "fe4d13b2a73c033a1de7f5107b205fc7ba0e1566cb95b92349cae6aa453\
                    8956013bfe0f7bf977cb072bb65e8782b5f33a0573fe78816299a017ca5ba55\
                    9e390c",
        });
        assert_eq!(encoded, jsn);

        let decoded: Signature = serde_json::from_value(encoded).unwrap();
        assert_eq!(decoded, sig);
    }

    #[test]
    #[cfg(not(any(target_os = "fuchsia", windows)))]
    fn new_rsa_key() {
        let bytes = RsaPrivateKey::pkcs8().unwrap();
        let _ = RsaPrivateKey::from_pkcs8(&bytes, SignatureScheme::RsaSsaPssSha256).unwrap();
    }

    #[test]
    fn new_ed25519_key() {
        let bytes = Ed25519PrivateKey::pkcs8().unwrap();
        let _ = Ed25519PrivateKey::from_pkcs8(&bytes).unwrap();
    }

    #[test]
    fn test_public_key_eq() {
        let key256 = PublicKey::from_spki(RSA_2048_SPKI, SignatureScheme::RsaSsaPssSha256).unwrap();
        let key512 = PublicKey::from_spki(RSA_2048_SPKI, SignatureScheme::RsaSsaPssSha512).unwrap();
        assert_eq!(key256, key256);
        assert_ne!(key256, key512);
    }

    #[test]
    fn test_public_key_hash() {
        use std::hash::{BuildHasher, Hash, Hasher};

        let key256 = PublicKey::from_spki(RSA_2048_SPKI, SignatureScheme::RsaSsaPssSha256).unwrap();
        let key512 = PublicKey::from_spki(RSA_2048_SPKI, SignatureScheme::RsaSsaPssSha512).unwrap();

        let state = std::collections::hash_map::RandomState::new();
        let mut hasher256 = state.build_hasher();
        key256.hash(&mut hasher256);

        let mut hasher512 = state.build_hasher();
        key512.hash(&mut hasher512);

        assert_ne!(hasher256.finish(), hasher512.finish());
    }
}
