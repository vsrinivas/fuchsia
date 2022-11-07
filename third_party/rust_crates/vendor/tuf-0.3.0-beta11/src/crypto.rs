//! Cryptographic structures and functions.

use {
    data_encoding::HEXLOWER,
    derp::{self, Der, Tag},
    futures_io::AsyncRead,
    futures_util::AsyncReadExt as _,
    ring::{
        digest::{self, SHA256, SHA512},
        rand::SystemRandom,
        signature::{Ed25519KeyPair, KeyPair, ED25519},
    },
    serde::{
        de::{Deserialize, Deserializer, Error as DeserializeError},
        ser::{Error as SerializeError, Serialize, Serializer},
    },
    serde_derive::{Deserialize, Serialize},
    std::{
        cmp::Ordering,
        collections::HashMap,
        fmt::{self, Debug, Display},
        hash,
        str::FromStr,
    },
    untrusted::Input,
};

#[cfg(feature = "unstable_rsa")]
use {
    data_encoding::BASE64URL,
    ring::signature::{
        RsaKeyPair, RSA_PSS_2048_8192_SHA256, RSA_PSS_2048_8192_SHA512, RSA_PSS_SHA256,
        RSA_PSS_SHA512,
    },
    std::{
        io::Write,
        process::{Command, Stdio},
        sync::Arc,
    },
};

use crate::error::{derp_error_to_error, Error, Result};
use crate::metadata::MetadataPath;
use crate::pouf::pouf1::shims;

const HASH_ALG_PREFS: &[HashAlgorithm] = &[HashAlgorithm::Sha512, HashAlgorithm::Sha256];

/// 1.2.840.113549.1.1.1 rsaEncryption(PKCS #1)
#[cfg(feature = "unstable_rsa")]
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

/// Given a map of hash algorithms and their values and retains the supported
/// hashes. Returns an `Err` if there is no match.
///
/// ```
/// use std::collections::HashMap;
/// use tuf::crypto::{retain_supported_hashes, HashValue, HashAlgorithm};
///
/// let mut map = HashMap::new();
/// assert!(retain_supported_hashes(&map).is_empty());
///
/// let sha512_value = HashValue::new(vec![0x00, 0x01]);
/// let _ = map.insert(HashAlgorithm::Sha512, sha512_value.clone());
/// assert_eq!(
///     retain_supported_hashes(&map),
///     vec![
///         (&HashAlgorithm::Sha512, sha512_value.clone()),
///     ],
/// );
///
/// let sha256_value = HashValue::new(vec![0x02, 0x03]);
/// let _ = map.insert(HashAlgorithm::Sha256, sha256_value.clone());
/// assert_eq!(
///     retain_supported_hashes(&map),
///     vec![
///         (&HashAlgorithm::Sha512, sha512_value.clone()),
///         (&HashAlgorithm::Sha256, sha256_value.clone()),
///     ],
/// );
///
/// let md5_value = HashValue::new(vec![0x04, 0x05]);
/// let _ = map.insert(HashAlgorithm::Unknown("md5".into()), md5_value);
/// assert_eq!(
///     retain_supported_hashes(&map),
///     vec![
///         (&HashAlgorithm::Sha512, sha512_value),
///         (&HashAlgorithm::Sha256, sha256_value),
///     ],
/// );
/// ```
pub fn retain_supported_hashes<'a>(
    hashes: &'a HashMap<HashAlgorithm, HashValue>,
) -> Vec<(&'static HashAlgorithm, HashValue)> {
    let mut data = vec![];
    for alg in HASH_ALG_PREFS {
        if let Some(value) = hashes.get(alg) {
            data.push((alg, value.clone()));
        }
    }

    data
}

#[cfg(test)]
pub(crate) fn calculate_hash(data: &[u8], hash_alg: &HashAlgorithm) -> HashValue {
    let mut context = hash_alg.digest_context().unwrap();
    context.update(data);
    HashValue::new(context.finish().as_ref().to_vec())
}

/// Calculate the size and hash digest from a given `AsyncRead`.
pub fn calculate_hashes_from_slice(
    buf: &[u8],
    hash_algs: &[HashAlgorithm],
) -> Result<HashMap<HashAlgorithm, HashValue>> {
    if hash_algs.is_empty() {
        return Err(Error::IllegalArgument(
            "Cannot provide empty set of hash algorithms".into(),
        ));
    }

    let mut hashes = HashMap::new();
    for alg in hash_algs {
        let mut context = alg.digest_context()?;
        context.update(buf);

        hashes.insert(
            alg.clone(),
            HashValue::new(context.finish().as_ref().to_vec()),
        );
    }

    Ok(hashes)
}

/// Calculate the size and hash digest from a given `AsyncRead`.
pub async fn calculate_hashes_from_reader<R>(
    mut read: R,
    hash_algs: &[HashAlgorithm],
) -> Result<(u64, HashMap<HashAlgorithm, HashValue>)>
where
    R: AsyncRead + Unpin,
{
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
        match read.read(&mut buf).await {
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
) -> Result<shims::PublicKey> {
    let key = match (key_type, signature_scheme) {
        (KeyType::Ed25519, SignatureScheme::Ed25519) => HEXLOWER.encode(public_key),
        #[cfg(feature = "unstable_rsa")]
        (KeyType::Rsa, SignatureScheme::RsaSsaPssSha256)
        | (KeyType::Rsa, SignatureScheme::RsaSsaPssSha512) => {
            let bytes = write_spki(public_key, key_type).map_err(derp_error_to_error)?;
            BASE64URL.encode(&bytes)
        }
        (_, _) => {
            // We don't understand this key type and/or signature scheme, so we left it as a UTF-8 string.
            std::str::from_utf8(public_key)
                .map_err(|err| {
                    Error::Encoding(format!(
                        "error converting public key value {:?} with key \
                        type {:?} and signature scheme {:?} to a string: {:?}",
                        public_key, key_type, signature_scheme, err
                    ))
                })?
                .to_string()
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
    use crate::pouf::{Pouf, Pouf1};

    let public_key = shim_public_key(
        key_type,
        signature_scheme,
        keyid_hash_algorithms,
        public_key,
    )?;
    let public_key = Pouf1::canonicalize(&Pouf1::serialize(&public_key)?)?;
    let mut context = digest::Context::new(&SHA256);
    context.update(&public_key);

    let key_id = HEXLOWER.encode(context.finish().as_ref());

    Ok(KeyId(key_id))
}

/// Wrapper type for public key's ID.
///
/// # Calculating
///
/// A `KeyId` is calculated as the hex digest of the SHA-256 hash of the
/// canonical form of the public key, or `hexdigest(sha256(cjson(public_key)))`.
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

impl fmt::Display for KeyId {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(&self.0)
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
#[non_exhaustive]
#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub enum SignatureScheme {
    /// [Ed25519](https://ed25519.cr.yp.to/)
    Ed25519,

    /// [RSASSA-PSS](https://tools.ietf.org/html/rfc5756) calculated over SHA256
    #[cfg(feature = "unstable_rsa")]
    RsaSsaPssSha256,

    /// [RSASSA-PSS](https://tools.ietf.org/html/rfc5756) calculated over SHA512
    #[cfg(feature = "unstable_rsa")]
    RsaSsaPssSha512,

    /// Placeholder for an unknown scheme.
    Unknown(String),
}

impl SignatureScheme {
    /// Construct a signature scheme from a `&str`.
    pub fn new(name: &str) -> Self {
        match name {
            "ed25519" => SignatureScheme::Ed25519,
            #[cfg(feature = "unstable_rsa")]
            "rsassa-pss-sha256" => SignatureScheme::RsaSsaPssSha256,
            #[cfg(feature = "unstable_rsa")]
            "rsassa-pss-sha512" => SignatureScheme::RsaSsaPssSha512,
            scheme => SignatureScheme::Unknown(scheme.to_string()),
        }
    }

    /// Return the signature scheme as a `&str`.
    pub fn as_str(&self) -> &str {
        match *self {
            SignatureScheme::Ed25519 => "ed25519",
            #[cfg(feature = "unstable_rsa")]
            SignatureScheme::RsaSsaPssSha256 => "rsassa-pss-sha256",
            #[cfg(feature = "unstable_rsa")]
            SignatureScheme::RsaSsaPssSha512 => "rsassa-pss-sha512",
            SignatureScheme::Unknown(ref s) => s,
        }
    }
}

impl Display for SignatureScheme {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(self.as_str())
    }
}

impl Serialize for SignatureScheme {
    fn serialize<S>(&self, ser: S) -> ::std::result::Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        ser.serialize_str(self.as_str())
    }
}

impl<'de> Deserialize<'de> for SignatureScheme {
    fn deserialize<D: Deserializer<'de>>(de: D) -> ::std::result::Result<Self, D::Error> {
        let string: String = Deserialize::deserialize(de)?;
        Ok(Self::new(&string))
    }
}

/// Wrapper type for the value of a cryptographic signature.
#[derive(Clone, PartialEq, Eq, PartialOrd, Ord, Serialize, Deserialize)]
pub struct SignatureValue(#[serde(with = "crate::format_hex")] Vec<u8>);

impl SignatureValue {
    /// Create a new `SignatureValue` from the given bytes.
    ///
    /// Note: It is unlikely that you ever want to do this manually.
    pub fn new(bytes: Vec<u8>) -> Self {
        SignatureValue(bytes)
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
#[non_exhaustive]
#[derive(Clone, PartialEq, Debug, Eq, Hash)]
pub enum KeyType {
    /// [Ed25519](https://ed25519.cr.yp.to/)
    Ed25519,

    /// [RSA](https://en.wikipedia.org/wiki/RSA_%28cryptosystem%29)
    #[cfg(feature = "unstable_rsa")]
    Rsa,

    /// Placeholder for an unknown key type.
    Unknown(String),
}

impl KeyType {
    /// Construct a key type from a `&str`.
    pub fn new(name: &str) -> Self {
        match name {
            "ed25519" => KeyType::Ed25519,
            #[cfg(feature = "unstable_rsa")]
            "rsa" => KeyType::Rsa,
            keytype => KeyType::Unknown(keytype.to_string()),
        }
    }

    /// Return the key type as a `&str`.
    pub fn as_str(&self) -> &str {
        match *self {
            KeyType::Ed25519 => "ed25519",
            #[cfg(feature = "unstable_rsa")]
            KeyType::Rsa => "rsa",
            KeyType::Unknown(ref s) => s,
        }
    }

    fn from_oid(oid: &[u8]) -> Result<Self> {
        match oid {
            #[cfg(feature = "unstable_rsa")]
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
            KeyType::Ed25519 => Ok(ED25519_SPKI_OID),
            #[cfg(feature = "unstable_rsa")]
            KeyType::Rsa => Ok(RSA_SPKI_OID),
            KeyType::Unknown(ref s) => Err(Error::UnknownKeyType(s.clone())),
        }
    }
}

impl Display for KeyType {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(self.as_str())
    }
}

impl Serialize for KeyType {
    fn serialize<S>(&self, ser: S) -> ::std::result::Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        ser.serialize_str(self.as_str())
    }
}

impl<'de> Deserialize<'de> for KeyType {
    fn deserialize<D: Deserializer<'de>>(de: D) -> ::std::result::Result<Self, D::Error> {
        let string: String = Deserialize::deserialize(de)?;
        Ok(Self::new(&string))
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

    /// Create a new `PrivateKey` from an ed25519 keypair. The keypair is a 64 byte slice, where the
    /// first 32 bytes are the ed25519 seed, and the second 32 bytes are the public key.
    pub fn from_ed25519(key: &[u8]) -> Result<Self> {
        Self::from_ed25519_with_keyid_hash_algorithms(key, None)
    }

    /// Create a new `PrivateKey` from an ed25519 keypair with a custom `keyid_hash_algorithms`. The
    /// keypair is a 64 byte slice, where the first 32 bytes are the ed25519 seed, and the second 32
    /// bytes are the public key.
    pub fn from_ed25519_with_keyid_hash_algorithms(
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
#[cfg(feature = "unstable_rsa")]
pub struct RsaPrivateKey {
    private: Arc<RsaKeyPair>,
    public: PublicKey,
}

#[cfg(feature = "unstable_rsa")]
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
            _ => {
                return Err(Error::IllegalArgument(format!(
                    "RSA keys do not support the signing scheme {:?}",
                    scheme
                )))
            }
        }

        let key = RsaKeyPair::from_pkcs8(der_key)
            .map_err(|_| Error::Encoding("Could not parse key as PKCS#8v2".into()))?;

        if key.public_modulus_len() < 256 {
            return Err(Error::IllegalArgument(format!(
                "RSA public modulus must be 2048 or greater. Found {}",
                key.public_modulus_len() * 8
            )));
        }

        let pub_key = extract_rsa_pub_from_pkcs8(der_key).map_err(derp_error_to_error)?;

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

#[cfg(feature = "unstable_rsa")]
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

        let (typ, value) = input
            .read_all(derp::Error::Read, |input| {
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
            })
            .map_err(derp_error_to_error)?;

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
        write_spki(&self.value.0, &self.typ).map_err(derp_error_to_error)
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
    pub fn verify(&self, role: &MetadataPath, msg: &[u8], sig: &Signature) -> Result<()> {
        let alg: &dyn ring::signature::VerificationAlgorithm = match self.scheme {
            SignatureScheme::Ed25519 => &ED25519,
            #[cfg(feature = "unstable_rsa")]
            SignatureScheme::RsaSsaPssSha256 => &RSA_PSS_2048_8192_SHA256,
            #[cfg(feature = "unstable_rsa")]
            SignatureScheme::RsaSsaPssSha512 => &RSA_PSS_2048_8192_SHA512,
            SignatureScheme::Unknown(ref s) => {
                return Err(Error::UnknownSignatureScheme(s.to_string()));
            }
        };

        let key = ring::signature::UnparsedPublicKey::new(alg, &self.value.0);
        key.verify(msg, &sig.value.0)
            .map_err(|_| Error::BadSignature(role.clone()))
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
            #[cfg(feature = "unstable_rsa")]
            KeyType::Rsa => {
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
            KeyType::Unknown(_) => {
                // We don't know this key type, so just leave it as a UTF-8 string.
                PublicKey::new(
                    intermediate.keytype().clone(),
                    intermediate.scheme().clone(),
                    intermediate.keyid_hash_algorithms().clone(),
                    intermediate.public_key().as_bytes().to_vec(),
                )
                .map_err(|e| DeserializeError::custom(format!("Couldn't parse key: {:?}", e)))?
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
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
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

impl PartialOrd for Signature {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        (&self.key_id, &self.value).partial_cmp(&(&other.key_id, &other.value))
    }
}

impl Ord for Signature {
    fn cmp(&self, other: &Self) -> Ordering {
        (&self.key_id, &self.value).cmp(&(&other.key_id, &other.value))
    }
}

/// The available hash algorithms.
#[non_exhaustive]
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

#[cfg(feature = "unstable_rsa")]
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

#[cfg(feature = "unstable_rsa")]
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
    use assert_matches::assert_matches;
    use pretty_assertions::assert_eq;
    use serde_json::{self, json};

    #[cfg(feature = "unstable_rsa")]
    mod rsa {
        pub(super) const PK8_2048: &[u8] = include_bytes!("../tests/rsa/rsa-2048.pk8.der");
        pub(super) const SPKI_2048: &[u8] = include_bytes!("../tests/rsa/rsa-2048.spki.der");
        pub(super) const PCKS1_2048: &[u8] = include_bytes!("../tests/rsa/rsa-2048.pkcs1.der");

        pub(super) const PK8_4096: &[u8] = include_bytes!("../tests/rsa/rsa-4096.pk8.der");
        pub(super) const SPKI_4096: &[u8] = include_bytes!("../tests/rsa/rsa-4096.spki.der");
        pub(super) const PCKS1_4096: &[u8] = include_bytes!("../tests/rsa/rsa-4096.pkcs1.der");
    }

    mod ed25519 {
        pub(super) const PRIVATE_KEY: &[u8] = include_bytes!("../tests/ed25519/ed25519-1");
        pub(super) const PUBLIC_KEY: &[u8] = include_bytes!("../tests/ed25519/ed25519-1.pub");
        pub(super) const PK8_1: &[u8] = include_bytes!("../tests/ed25519/ed25519-1.pk8.der");
        pub(super) const SPKI_1: &[u8] = include_bytes!("../tests/ed25519/ed25519-1.spki.der");
        pub(super) const PK8_2: &[u8] = include_bytes!("../tests/ed25519/ed25519-2.pk8.der");
    }

    #[cfg(feature = "unstable_rsa")]
    #[test]
    fn parse_public_rsa_2048_spki() {
        let key = PublicKey::from_spki(rsa::SPKI_2048, SignatureScheme::RsaSsaPssSha256).unwrap();
        assert_eq!(key.typ, KeyType::Rsa);
        assert_eq!(key.scheme, SignatureScheme::RsaSsaPssSha256);
    }

    #[cfg(feature = "unstable_rsa")]
    #[test]
    fn parse_public_rsa_4096_spki() {
        let key = PublicKey::from_spki(rsa::SPKI_4096, SignatureScheme::RsaSsaPssSha256).unwrap();
        assert_eq!(key.typ, KeyType::Rsa);
        assert_eq!(key.scheme, SignatureScheme::RsaSsaPssSha256);
    }

    #[test]
    fn parse_public_ed25519_spki() {
        let key = PublicKey::from_spki(ed25519::SPKI_1, SignatureScheme::Ed25519).unwrap();
        assert_eq!(key.typ, KeyType::Ed25519);
        assert_eq!(key.scheme, SignatureScheme::Ed25519);
    }

    #[test]
    fn parse_public_ed25519() {
        let key = PublicKey::from_ed25519(ed25519::PUBLIC_KEY).unwrap();
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
            PublicKey::from_ed25519_with_keyid_hash_algorithms(ed25519::PUBLIC_KEY, None).unwrap();
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
            ed25519::PUBLIC_KEY,
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

    #[cfg(feature = "unstable_rsa")]
    #[test]
    fn rsa_2048_read_pkcs8_and_sign() {
        let msg = b"test";

        let key =
            RsaPrivateKey::from_pkcs8(rsa::PK8_2048, SignatureScheme::RsaSsaPssSha256).unwrap();
        let sig = key.sign(msg).unwrap();
        key.public.verify(&MetadataPath::root(), msg, &sig).unwrap();

        let key =
            RsaPrivateKey::from_pkcs8(rsa::PK8_2048, SignatureScheme::RsaSsaPssSha512).unwrap();
        let sig = key.sign(msg).unwrap();
        key.public.verify(&MetadataPath::root(), msg, &sig).unwrap();
    }

    #[cfg(feature = "unstable_rsa")]
    #[test]
    fn rsa_4096_read_pkcs8_and_sign() {
        let msg = b"test";

        let key =
            RsaPrivateKey::from_pkcs8(rsa::PK8_4096, SignatureScheme::RsaSsaPssSha256).unwrap();
        let sig = key.sign(msg).unwrap();
        key.public.verify(&MetadataPath::root(), msg, &sig).unwrap();

        let key =
            RsaPrivateKey::from_pkcs8(rsa::PK8_4096, SignatureScheme::RsaSsaPssSha512).unwrap();
        let sig = key.sign(msg).unwrap();
        key.public.verify(&MetadataPath::root(), msg, &sig).unwrap();
    }

    #[cfg(feature = "unstable_rsa")]
    #[test]
    fn extract_pkcs1_from_rsa_2048_pkcs8() {
        let res = extract_rsa_pub_from_pkcs8(rsa::PK8_2048).unwrap();
        assert_eq!(res.as_slice(), rsa::PCKS1_2048);
    }

    #[cfg(feature = "unstable_rsa")]
    #[test]
    fn extract_pkcs1_from_rsa_4096_pkcs8() {
        let res = extract_rsa_pub_from_pkcs8(rsa::PK8_4096).unwrap();
        assert_eq!(res.as_slice(), rsa::PCKS1_4096);
    }

    #[test]
    fn ed25519_read_pkcs8_and_sign() {
        let key = Ed25519PrivateKey::from_pkcs8(ed25519::PK8_1).unwrap();
        let msg = b"test";

        let sig = key.sign(msg).unwrap();

        let pub_key =
            PublicKey::from_spki(&key.public.as_spki().unwrap(), SignatureScheme::Ed25519).unwrap();

        let role = MetadataPath::root();
        assert_matches!(pub_key.verify(&role, msg, &sig), Ok(()));

        // Make sure we match what ring expects.
        let ring_key = ring::signature::Ed25519KeyPair::from_pkcs8(ed25519::PK8_1).unwrap();
        assert_eq!(key.public().as_bytes(), ring_key.public_key().as_ref());
        assert_eq!(sig.value().as_bytes(), ring_key.sign(msg).as_ref());

        // Make sure verification fails with the wrong key.
        let bad_pub_key = Ed25519PrivateKey::from_pkcs8(ed25519::PK8_2)
            .unwrap()
            .public()
            .clone();

        assert_matches!(
            bad_pub_key.verify(&role, msg, &sig),
            Err(Error::BadSignature(r))
            if r == role
        );
    }

    #[test]
    fn ed25519_read_keypair_and_sign() {
        let key = Ed25519PrivateKey::from_ed25519(ed25519::PRIVATE_KEY).unwrap();
        let pub_key = PublicKey::from_ed25519(ed25519::PUBLIC_KEY).unwrap();
        assert_eq!(key.public(), &pub_key);

        let role = MetadataPath::root();
        let msg = b"test";
        let sig = key.sign(msg).unwrap();
        assert_matches!(pub_key.verify(&role, msg, &sig), Ok(()));

        // Make sure we match what ring expects.
        let ring_key = ring::signature::Ed25519KeyPair::from_pkcs8(ed25519::PK8_1).unwrap();
        assert_eq!(key.public().as_bytes(), ring_key.public_key().as_ref());
        assert_eq!(sig.value().as_bytes(), ring_key.sign(msg).as_ref());

        // Make sure verification fails with the wrong key.
        let bad_pub_key = Ed25519PrivateKey::from_pkcs8(ed25519::PK8_2)
            .unwrap()
            .public()
            .clone();

        assert_matches!(
            bad_pub_key.verify(&role, msg, &sig),
            Err(Error::BadSignature(r))
            if r == role
        );
    }

    #[test]
    fn ed25519_read_keypair_and_sign_with_keyid_hash_algorithms() {
        let key = Ed25519PrivateKey::from_ed25519_with_keyid_hash_algorithms(
            ed25519::PRIVATE_KEY,
            python_tuf_compatibility_keyid_hash_algorithms(),
        )
        .unwrap();
        let pub_key = PublicKey::from_ed25519_with_keyid_hash_algorithms(
            ed25519::PUBLIC_KEY,
            python_tuf_compatibility_keyid_hash_algorithms(),
        )
        .unwrap();
        assert_eq!(key.public(), &pub_key);

        let role = MetadataPath::root();
        let msg = b"test";
        let sig = key.sign(msg).unwrap();
        assert_matches!(pub_key.verify(&role, msg, &sig), Ok(()));

        // Make sure we match what ring expects.
        let ring_key = ring::signature::Ed25519KeyPair::from_pkcs8(ed25519::PK8_1).unwrap();
        assert_eq!(key.public().as_bytes(), ring_key.public_key().as_ref());
        assert_eq!(sig.value().as_bytes(), ring_key.sign(msg).as_ref());

        // Make sure verification fails with the wrong key.
        let bad_pub_key = Ed25519PrivateKey::from_pkcs8(ed25519::PK8_2)
            .unwrap()
            .public()
            .clone();

        assert_matches!(
            bad_pub_key.verify(&role, msg, &sig),
            Err(Error::BadSignature(r))
            if r == role
        );
    }

    #[test]
    fn unknown_keytype_cannot_verify() {
        let pub_key = PublicKey::new(
            KeyType::Unknown("unknown-keytype".into()),
            SignatureScheme::Unknown("unknown-scheme".into()),
            None,
            b"unknown-key".to_vec(),
        )
        .unwrap();
        let role = MetadataPath::root();
        let msg = b"test";
        let sig = Signature {
            key_id: KeyId("key-id".into()),
            value: SignatureValue(b"sig-value".to_vec()),
        };

        assert_matches!(
            pub_key.verify(&role, msg, &sig),
            Err(Error::UnknownSignatureScheme(s))
            if s == "unknown-scheme"
        );
    }

    #[test]
    fn serde_key_id() {
        let s = "4750eaf6878740780d6f97b12dbad079fb012bec88c78de2c380add56d3f51db";
        let jsn = json!(s);
        let parsed: KeyId = serde_json::from_value(jsn.clone()).unwrap();
        assert_eq!(parsed, KeyId::from_str(s).unwrap());
        let encoded = serde_json::to_value(&parsed).unwrap();
        assert_eq!(encoded, jsn);
    }

    #[test]
    fn serde_key_type() {
        let jsn = json!("ed25519");
        let parsed: KeyType = serde_json::from_value(jsn.clone()).unwrap();
        assert_eq!(parsed, KeyType::Ed25519);

        let encoded = serde_json::to_value(&parsed).unwrap();
        assert_eq!(encoded, jsn);

        let jsn = json!("unknown");
        let parsed: KeyType = serde_json::from_value(jsn).unwrap();
        assert_eq!(parsed, KeyType::Unknown("unknown".into()));
    }

    #[test]
    fn serde_signature_scheme() {
        let jsn = json!("ed25519");
        let parsed: SignatureScheme = serde_json::from_value(jsn.clone()).unwrap();
        assert_eq!(parsed, SignatureScheme::Ed25519);

        let encoded = serde_json::to_value(&parsed).unwrap();
        assert_eq!(encoded, jsn);

        let jsn = json!("unknown");
        let parsed: SignatureScheme = serde_json::from_value(jsn).unwrap();
        assert_eq!(parsed, SignatureScheme::Unknown("unknown".into()));
    }

    #[test]
    fn serde_signature_value() {
        let s = "4750eaf6878740780d6f97b12dbad079fb012bec88c78de2c380add56d3f51db";
        let jsn = json!(s);
        let parsed: SignatureValue = serde_json::from_str(&format!("\"{}\"", s)).unwrap();
        assert_eq!(
            parsed,
            SignatureValue(HEXLOWER.decode(s.as_bytes()).unwrap())
        );
        let encoded = serde_json::to_value(&parsed).unwrap();
        assert_eq!(encoded, jsn);
    }

    #[test]
    fn serde_unknown_keytype_and_signature_scheme_public_key() {
        let pub_key = PublicKey::new(
            KeyType::Unknown("unknown-keytype".into()),
            SignatureScheme::Unknown("unknown-scheme".into()),
            None,
            b"unknown-key".to_vec(),
        )
        .unwrap();
        let encoded = serde_json::to_value(&pub_key).unwrap();
        let jsn = json!({
            "keytype": "unknown-keytype",
            "scheme": "unknown-scheme",
            "keyval": {
                "public": "unknown-key",
            }
        });
        assert_eq!(encoded, jsn);
        let decoded: PublicKey = serde_json::from_value(jsn).unwrap();
        assert_eq!(decoded, pub_key);
    }

    #[cfg(feature = "unstable_rsa")]
    #[test]
    fn serde_rsa_public_key() {
        let der = rsa::SPKI_2048;
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

    #[cfg(feature = "unstable_rsa")]
    #[test]
    fn de_ser_rsa_public_key_with_keyid_hash_algo() {
        let original = json!({
            "keytype": "rsa",
            "scheme": "rsassa-pss-sha256",
            "keyid_hash_algorithms": ["sha256", "sha512"],
            "keyval": {
                "public": BASE64URL.encode(rsa::SPKI_2048),
            }
        });

        let decoded: PublicKey = serde_json::from_value(original.clone()).unwrap();
        let encoded = serde_json::to_value(&decoded).unwrap();

        assert_eq!(original, encoded);
    }

    #[cfg(feature = "unstable_rsa")]
    #[test]
    fn de_ser_rsa_public_key_without_keyid_hash_algo() {
        let original = json!({
            "keytype": "rsa",
            "scheme": "rsassa-pss-sha256",
            "keyval": {
                "public": BASE64URL.encode(rsa::SPKI_2048),
            }
        });

        let decoded: PublicKey = serde_json::from_value(original.clone()).unwrap();
        let encoded = serde_json::to_value(&decoded).unwrap();

        assert_eq!(original, encoded);
    }

    #[test]
    fn serde_ed25519_public_key() {
        let pub_key = Ed25519PrivateKey::from_pkcs8(ed25519::PK8_1)
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
        let pub_key = Ed25519PrivateKey::from_pkcs8(ed25519::PK8_1)
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
        let pub_key = Ed25519PrivateKey::from_pkcs8(ed25519::PK8_1)
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
        let key = Ed25519PrivateKey::from_pkcs8(ed25519::PK8_1).unwrap();
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
            Ed25519PrivateKey::from_pkcs8_with_keyid_hash_algorithms(ed25519::PK8_1, None).unwrap();
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

    #[cfg(feature = "unstable_rsa")]
    #[cfg(not(any(target_os = "fuchsia", windows)))]
    #[test]
    fn new_rsa_key() {
        let bytes = RsaPrivateKey::pkcs8().unwrap();
        let _ = RsaPrivateKey::from_pkcs8(&bytes, SignatureScheme::RsaSsaPssSha256).unwrap();
    }

    #[test]
    fn new_ed25519_key() {
        let bytes = Ed25519PrivateKey::pkcs8().unwrap();
        let _ = Ed25519PrivateKey::from_pkcs8(&bytes).unwrap();
    }

    #[cfg(feature = "unstable_rsa")]
    #[test]
    fn test_rsa_public_key_eq() {
        let key256_1 =
            PublicKey::from_spki(rsa::SPKI_2048, SignatureScheme::RsaSsaPssSha256).unwrap();
        let key256_2 =
            PublicKey::from_spki(rsa::SPKI_2048, SignatureScheme::RsaSsaPssSha256).unwrap();
        let key512 =
            PublicKey::from_spki(rsa::SPKI_2048, SignatureScheme::RsaSsaPssSha512).unwrap();

        assert_eq!(key256_1, key256_2);
        assert_ne!(key256_1, key512);
    }

    #[test]
    fn test_ed25519_public_key_eq() {
        let key1 = Ed25519PrivateKey::from_pkcs8(ed25519::PK8_1).unwrap();
        let key2 = Ed25519PrivateKey::from_pkcs8(ed25519::PK8_2).unwrap();

        assert_eq!(key1.public(), key1.public());
        assert_ne!(key1.public(), key2.public());
    }

    fn check_public_key_hash(key1: &PublicKey, key2: &PublicKey) {
        use std::hash::{BuildHasher, Hash, Hasher};

        let state = std::collections::hash_map::RandomState::new();
        let mut hasher1 = state.build_hasher();
        key1.hash(&mut hasher1);

        let mut hasher2 = state.build_hasher();
        key2.hash(&mut hasher2);

        assert_ne!(hasher1.finish(), hasher2.finish());
    }

    #[cfg(feature = "unstable_rsa")]
    #[test]
    fn test_rsa_public_key_hash() {
        let key256 =
            PublicKey::from_spki(rsa::SPKI_2048, SignatureScheme::RsaSsaPssSha256).unwrap();
        let key512 =
            PublicKey::from_spki(rsa::SPKI_2048, SignatureScheme::RsaSsaPssSha512).unwrap();

        check_public_key_hash(&key256, &key512);
    }

    #[test]
    fn test_ed25519_public_key_hash() {
        let key1 = Ed25519PrivateKey::from_pkcs8(ed25519::PK8_1).unwrap();
        let key2 = Ed25519PrivateKey::from_pkcs8(ed25519::PK8_2).unwrap();

        check_public_key_hash(key1.public(), key2.public());
    }
}
