//! TUF metadata.

use chrono::offset::Utc;
use chrono::{DateTime, Duration};
use futures_io::AsyncRead;
use serde::de::{Deserialize, DeserializeOwned, Deserializer, Error as DeserializeError};
use serde::ser::{Error as SerializeError, Serialize, Serializer};
use serde_derive::{Deserialize, Serialize};
use std::borrow::{Borrow, Cow};
use std::collections::{HashMap, HashSet};
use std::fmt::{self, Debug, Display};
use std::marker::PhantomData;
use std::str;

use crate::crypto::{self, HashAlgorithm, HashValue, KeyId, PrivateKey, PublicKey, Signature};
use crate::error::Error;
use crate::pouf::pouf1::shims;
use crate::pouf::Pouf;
use crate::Result;

#[rustfmt::skip]
static PATH_ILLEGAL_COMPONENTS: &[&str] = &[
    ".", // current dir
    "..", // parent dir
         // TODO ? "0", // may translate to nul in windows
];

#[rustfmt::skip]
static PATH_ILLEGAL_COMPONENTS_CASE_INSENSITIVE: &[&str] = &[
    // DOS device files
    "CON",
    "PRN",
    "AUX",
    "NUL",
    "COM1",
    "COM2",
    "COM3",
    "COM4",
    "COM5",
    "COM6",
    "COM7",
    "COM8",
    "COM9",
    "LPT1",
    "LPT2",
    "LPT3",
    "LPT4",
    "LPT5",
    "LPT6",
    "LPT7",
    "LPT8",
    "LPT9",
    "KEYBD$",
    "CLOCK$",
    "SCREEN$",
    "$IDLE$",
    "CONFIG$",
];

#[rustfmt::skip]
static PATH_ILLEGAL_STRINGS: &[&str] = &[
    ":", // for *nix compatibility
    "\\", // for windows compatibility
    "<",
    ">",
    "\"",
    "|",
    "?",
    // control characters, all illegal in FAT
    "\u{000}",
    "\u{001}",
    "\u{002}",
    "\u{003}",
    "\u{004}",
    "\u{005}",
    "\u{006}",
    "\u{007}",
    "\u{008}",
    "\u{009}",
    "\u{00a}",
    "\u{00b}",
    "\u{00c}",
    "\u{00d}",
    "\u{00e}",
    "\u{00f}",
    "\u{010}",
    "\u{011}",
    "\u{012}",
    "\u{013}",
    "\u{014}",
    "\u{015}",
    "\u{016}",
    "\u{017}",
    "\u{018}",
    "\u{019}",
    "\u{01a}",
    "\u{01b}",
    "\u{01c}",
    "\u{01d}",
    "\u{01e}",
    "\u{01f}",
    "\u{07f}",
];

fn safe_path(path: &str) -> Result<()> {
    if path.is_empty() {
        return Err(Error::IllegalArgument("Path cannot be empty".into()));
    }

    if path.starts_with('/') {
        return Err(Error::IllegalArgument("Cannot start with '/'".into()));
    }

    for bad_str in PATH_ILLEGAL_STRINGS {
        if path.contains(bad_str) {
            return Err(Error::IllegalArgument(format!(
                "Path cannot contain {:?}",
                bad_str
            )));
        }
    }

    for component in path.split('/') {
        for bad_str in PATH_ILLEGAL_COMPONENTS {
            if component == *bad_str {
                return Err(Error::IllegalArgument(format!(
                    "Path cannot have component {:?}",
                    component
                )));
            }
        }

        let component_lower = component.to_lowercase();
        for bad_str in PATH_ILLEGAL_COMPONENTS_CASE_INSENSITIVE {
            if component_lower.as_str() == *bad_str {
                return Err(Error::IllegalArgument(format!(
                    "Path cannot have component {:?}",
                    component
                )));
            }
        }
    }

    Ok(())
}

/// The TUF role.
#[derive(Debug, PartialEq, Eq, Hash, Serialize, Deserialize)]
pub enum Role {
    /// The root role.
    #[serde(rename = "root")]
    Root,

    /// The snapshot role.
    #[serde(rename = "snapshot")]
    Snapshot,

    /// The targets role.
    #[serde(rename = "targets")]
    Targets,

    /// The timestamp role.
    #[serde(rename = "timestamp")]
    Timestamp,
}

impl Role {
    /// Check if this role could be associated with a given path.
    ///
    /// ```
    /// use tuf::metadata::{MetadataPath, Role};
    ///
    /// assert!(Role::Root.fuzzy_matches_path(&MetadataPath::root()));
    /// assert!(Role::Snapshot.fuzzy_matches_path(&MetadataPath::snapshot()));
    /// assert!(Role::Targets.fuzzy_matches_path(&MetadataPath::targets()));
    /// assert!(Role::Timestamp.fuzzy_matches_path(&MetadataPath::timestamp()));
    ///
    /// assert!(!Role::Root.fuzzy_matches_path(&MetadataPath::snapshot()));
    /// assert!(!Role::Root.fuzzy_matches_path(&MetadataPath::new("wat").unwrap()));
    /// ```
    pub fn fuzzy_matches_path(&self, path: &MetadataPath) -> bool {
        match *self {
            Role::Root if &path.0 == "root" => true,
            Role::Snapshot if &path.0 == "snapshot" => true,
            Role::Timestamp if &path.0 == "timestamp" => true,
            Role::Targets if &path.0 == "targets" => true,
            Role::Targets if !&["root", "snapshot", "targets"].contains(&path.0.as_ref()) => true,
            _ => false,
        }
    }

    /// Return the name of the role.
    pub fn name(&self) -> &'static str {
        match *self {
            Role::Root => "root",
            Role::Snapshot => "snapshot",
            Role::Targets => "targets",
            Role::Timestamp => "timestamp",
        }
    }
}

impl Display for Role {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        f.write_str(self.name())
    }
}

/// Enum used for addressing versioned TUF metadata.
#[derive(Debug, PartialEq, Eq, PartialOrd, Ord, Copy, Clone, Hash)]
pub enum MetadataVersion {
    /// The metadata is unversioned. This is the latest version of the metadata.
    None,
    /// The metadata is addressed by a specific version number.
    Number(u32),
}

impl Display for MetadataVersion {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            MetadataVersion::None => f.write_str("none"),
            MetadataVersion::Number(version) => write!(f, "{}", version),
        }
    }
}

impl MetadataVersion {
    /// Converts this struct into the string used for addressing metadata.
    pub fn prefix(&self) -> String {
        match *self {
            MetadataVersion::None => String::new(),
            MetadataVersion::Number(ref x) => format!("{}.", x),
        }
    }
}

/// Top level trait used for role metadata.
pub trait Metadata: Debug + PartialEq + Serialize + DeserializeOwned {
    /// The role associated with the metadata.
    const ROLE: Role;

    /// The version number.
    fn version(&self) -> u32;

    /// An immutable reference to the metadata's expiration `DateTime`.
    fn expires(&self) -> &DateTime<Utc>;
}

/// Unverified raw metadata with attached signatures and type information identifying the
/// metadata's type and serialization format.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct RawSignedMetadata<D, M> {
    bytes: Vec<u8>,
    _marker: PhantomData<(D, M)>,
}

impl<D, M> RawSignedMetadata<D, M>
where
    D: Pouf,
    M: Metadata,
{
    /// Create a new [`RawSignedMetadata`] using the provided `bytes`.
    pub fn new(bytes: Vec<u8>) -> Self {
        Self {
            bytes,
            _marker: PhantomData,
        }
    }

    /// Access this metadata's inner raw bytes.
    pub fn as_bytes(&self) -> &[u8] {
        &self.bytes
    }

    /// Parse this metadata.
    ///
    /// **WARNING**: This does not verify signatures, so it exposes users to potential parser
    /// exploits.
    pub fn parse_untrusted(&self) -> Result<SignedMetadata<D, M>> {
        D::from_slice(&self.bytes)
    }
}

/// A collection of [RawSignedMetadata] that describes the metadata at one
/// commit.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct RawSignedMetadataSet<D> {
    root: Option<RawSignedMetadata<D, RootMetadata>>,
    targets: Option<RawSignedMetadata<D, TargetsMetadata>>,
    snapshot: Option<RawSignedMetadata<D, SnapshotMetadata>>,
    timestamp: Option<RawSignedMetadata<D, TimestampMetadata>>,
}

impl<D> RawSignedMetadataSet<D> {
    /// Returns a reference to the built root metadata, if any.
    pub fn root(&self) -> Option<&RawSignedMetadata<D, RootMetadata>> {
        self.root.as_ref()
    }

    /// Returns a reference to the built targets metadata, if any.
    pub fn targets(&self) -> Option<&RawSignedMetadata<D, TargetsMetadata>> {
        self.targets.as_ref()
    }

    /// Returns a reference to the built snapshot metadata, if any.
    pub fn snapshot(&self) -> Option<&RawSignedMetadata<D, SnapshotMetadata>> {
        self.snapshot.as_ref()
    }

    /// Returns a reference to the built timestamp metadata, if any.
    pub fn timestamp(&self) -> Option<&RawSignedMetadata<D, TimestampMetadata>> {
        self.timestamp.as_ref()
    }
}

/// Builder for [RawSignedMetadataSet].
#[derive(Default)]
pub struct RawSignedMetadataSetBuilder<D>
where
    D: Pouf,
{
    metadata: RawSignedMetadataSet<D>,
}

impl<D> RawSignedMetadataSetBuilder<D>
where
    D: Pouf,
{
    /// Create a new [RawSignedMetadataSetBuilder].
    pub fn new() -> Self {
        Self {
            metadata: RawSignedMetadataSet {
                root: None,
                targets: None,
                snapshot: None,
                timestamp: None,
            },
        }
    }

    /// Set or replace the root metadata.
    pub fn root(mut self, root: RawSignedMetadata<D, RootMetadata>) -> Self {
        self.metadata.root = Some(root);
        self
    }

    /// Set or replace the targets metadata.
    pub fn targets(mut self, targets: RawSignedMetadata<D, TargetsMetadata>) -> Self {
        self.metadata.targets = Some(targets);
        self
    }

    /// Set or replace the snapshot metadata.
    pub fn snapshot(mut self, snapshot: RawSignedMetadata<D, SnapshotMetadata>) -> Self {
        self.metadata.snapshot = Some(snapshot);
        self
    }

    /// Set or replace the timestamp metadata.
    pub fn timestamp(mut self, timestamp: RawSignedMetadata<D, TimestampMetadata>) -> Self {
        self.metadata.timestamp = Some(timestamp);
        self
    }

    /// Return a [RawSignedMetadataSet].
    pub fn build(self) -> RawSignedMetadataSet<D> {
        self.metadata
    }
}

/// Helper to construct `SignedMetadata`.
#[derive(Debug, Clone)]
pub struct SignedMetadataBuilder<D, M>
where
    D: Pouf,
{
    signatures: HashMap<KeyId, Signature>,
    metadata: D::RawData,
    metadata_bytes: Vec<u8>,
    _marker: PhantomData<M>,
}

impl<D, M> SignedMetadataBuilder<D, M>
where
    D: Pouf,
    M: Metadata,
{
    /// Create a new `SignedMetadataBuilder` from a given `Metadata`.
    pub fn from_metadata(metadata: &M) -> Result<Self> {
        let metadata = D::serialize(metadata)?;
        Self::from_raw_metadata(metadata)
    }

    /// Create a new `SignedMetadataBuilder` from manually serialized metadata to be signed.
    /// Returns an error if `metadata` cannot be parsed into `M`.
    pub fn from_raw_metadata(metadata: D::RawData) -> Result<Self> {
        let _ensure_metadata_parses: M = D::deserialize(&metadata)?;
        let metadata_bytes = D::canonicalize(&metadata)?;
        Ok(Self {
            signatures: HashMap::new(),
            metadata,
            metadata_bytes,
            _marker: PhantomData,
        })
    }

    /// Sign the metadata using the given `private_key`, replacing any existing signatures with the
    /// same `KeyId`.
    ///
    /// **WARNING**: You should never have multiple TUF private keys on the same machine, so if
    /// you're using this to append several signatures at once, you are doing something wrong. The
    /// preferred method is to generate your copy of the metadata locally and use
    /// `SignedMetadata::merge_signatures` to perform the "append" operations.
    pub fn sign(mut self, private_key: &dyn PrivateKey) -> Result<Self> {
        let sig = private_key.sign(&self.metadata_bytes)?;
        let _ = self.signatures.insert(sig.key_id().clone(), sig);
        Ok(self)
    }

    /// Construct a new `SignedMetadata` using the included signatures, sorting the signatures by
    /// `KeyId`.
    pub fn build(self) -> SignedMetadata<D, M> {
        let mut signatures = self
            .signatures
            .into_iter()
            .map(|(_k, v)| v)
            .collect::<Vec<_>>();
        signatures.sort_unstable_by(|a, b| a.key_id().cmp(b.key_id()));

        SignedMetadata {
            signatures,
            metadata: self.metadata,
            _marker: PhantomData,
        }
    }
}

/// Serialized metadata with attached unverified signatures.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct SignedMetadata<D, M>
where
    D: Pouf,
{
    signatures: Vec<Signature>,
    #[serde(rename = "signed")]
    metadata: D::RawData,
    #[serde(skip_serializing, skip_deserializing)]
    _marker: PhantomData<M>,
}

impl<D, M> SignedMetadata<D, M>
where
    D: Pouf,
    M: Metadata,
{
    /// Create a new `SignedMetadata`. The supplied private key is used to sign the canonicalized
    /// bytes of the provided metadata with the provided scheme.
    ///
    /// ```
    /// # use chrono::prelude::*;
    /// # use tuf::crypto::{Ed25519PrivateKey, PrivateKey, SignatureScheme, HashAlgorithm};
    /// # use tuf::pouf::Pouf1;
    /// # use tuf::metadata::{SignedMetadata, SnapshotMetadataBuilder};
    /// #
    /// # let key: &[u8] = include_bytes!("../tests/ed25519/ed25519-1.pk8.der");
    /// let key = Ed25519PrivateKey::from_pkcs8(&key).unwrap();
    ///
    /// let snapshot = SnapshotMetadataBuilder::new().build().unwrap();
    /// SignedMetadata::<Pouf1, _>::new(&snapshot, &key).unwrap();
    /// ```
    pub fn new(metadata: &M, private_key: &dyn PrivateKey) -> Result<Self> {
        let raw = D::serialize(metadata)?;
        let bytes = D::canonicalize(&raw)?;
        let sig = private_key.sign(&bytes)?;
        Ok(Self {
            signatures: vec![sig],
            metadata: raw,
            _marker: PhantomData,
        })
    }

    /// Serialize this metadata to canonical bytes suitable for serialization. Note that this
    /// method is only intended to serialize signed metadata generated by this crate, not to
    /// re-serialize metadata that was originally obtained from a remote source.
    ///
    /// TUF metadata hashes are on the raw bytes of the metadata, so it is not guaranteed that the
    /// hash of the returned bytes will match a hash included in, for example, a snapshot metadata
    /// file, as:
    /// * Parsing metadata removes unknown fields, which would not be included in the returned
    /// bytes,
    /// * [Pouf] implementations only guarantee the bytes are canonical for the purpose of a
    /// signature. Metadata obtained from a remote source may have included different whitespace
    /// or ordered fields in a way that is not preserved when parsing that metadata.
    pub fn to_raw(&self) -> Result<RawSignedMetadata<D, M>> {
        let bytes = D::canonicalize(&D::serialize(self)?)?;
        Ok(RawSignedMetadata::new(bytes))
    }

    /// Append a signature to this signed metadata. Will overwrite signature by keys with the same
    /// ID.
    ///
    /// **WARNING**: You should never have multiple TUF private keys on the same machine, so if
    /// you're using this to append several signatures at once, you are doing something wrong. The
    /// preferred method is to generate your copy of the metadata locally and use `merge_signatures`
    /// to perform the "append" operations.
    ///
    /// ```
    /// # use chrono::prelude::*;
    /// # use tuf::crypto::{Ed25519PrivateKey, PrivateKey, SignatureScheme, HashAlgorithm};
    /// # use tuf::pouf::Pouf1;
    /// # use tuf::metadata::{SignedMetadata, SnapshotMetadataBuilder};
    /// #
    /// let key_1: &[u8] = include_bytes!("../tests/ed25519/ed25519-1.pk8.der");
    /// let key_1 = Ed25519PrivateKey::from_pkcs8(&key_1).unwrap();
    ///
    /// // Note: This is for demonstration purposes only.
    /// // You should never have multiple private keys on the same device.
    /// let key_2: &[u8] = include_bytes!("../tests/ed25519/ed25519-2.pk8.der");
    /// let key_2 = Ed25519PrivateKey::from_pkcs8(&key_2).unwrap();
    ///
    /// let snapshot = SnapshotMetadataBuilder::new().build().unwrap();
    /// let mut snapshot = SignedMetadata::<Pouf1, _>::new(&snapshot, &key_1).unwrap();
    ///
    /// snapshot.add_signature(&key_2).unwrap();
    /// assert_eq!(snapshot.signatures().len(), 2);
    ///
    /// snapshot.add_signature(&key_2).unwrap();
    /// assert_eq!(snapshot.signatures().len(), 2);
    /// ```
    pub fn add_signature(&mut self, private_key: &dyn PrivateKey) -> Result<()> {
        let bytes = D::canonicalize(&self.metadata)?;
        let sig = private_key.sign(&bytes)?;
        self.signatures
            .retain(|s| s.key_id() != private_key.public().key_id());
        self.signatures.push(sig);
        self.signatures.sort();
        Ok(())
    }

    /// Merge the singatures from `other` into `self` if and only if
    /// `self.as_ref() == other.as_ref()`. If `self` and `other` contain signatures from the same
    /// key ID, then the signatures from `self` will replace the signatures from `other`.
    pub fn merge_signatures(&mut self, other: &Self) -> Result<()> {
        if self.metadata != other.metadata {
            return Err(Error::IllegalArgument(
                "Attempted to merge unequal metadata".into(),
            ));
        }

        let key_ids = self
            .signatures
            .iter()
            .map(|s| s.key_id().clone())
            .collect::<HashSet<KeyId>>();

        self.signatures.extend(
            other
                .signatures
                .iter()
                .filter(|s| !key_ids.contains(s.key_id()))
                .cloned(),
        );
        self.signatures.sort();

        Ok(())
    }

    /// An immutable reference to the signatures.
    pub fn signatures(&self) -> &[Signature] {
        &self.signatures
    }

    /// Parse the version number of this metadata without verifying signatures.
    ///
    /// This operation is generally unsafe to do with metadata obtained from an untrusted source,
    /// but rolling forward to the most recent root.json requires using the version number of the
    /// latest root.json.
    pub(crate) fn parse_version_untrusted(&self) -> Result<u32> {
        #[derive(Deserialize)]
        pub struct MetadataVersion {
            version: u32,
        }

        let meta: MetadataVersion = D::deserialize(&self.metadata)?;
        Ok(meta.version)
    }

    /// Parse this metadata without verifying signatures.
    ///
    /// This operation is not safe to do with metadata obtained from an untrusted source.
    pub fn assume_valid(&self) -> Result<M> {
        D::deserialize(&self.metadata)
    }
}

/// Helper to construct `RootMetadata`.
pub struct RootMetadataBuilder {
    version: u32,
    expires: DateTime<Utc>,
    consistent_snapshot: bool,
    keys: HashMap<KeyId, PublicKey>,
    root_threshold: u32,
    root_key_ids: HashSet<KeyId>,
    snapshot_threshold: u32,
    snapshot_key_ids: HashSet<KeyId>,
    targets_threshold: u32,
    targets_key_ids: HashSet<KeyId>,
    timestamp_threshold: u32,
    timestamp_key_ids: HashSet<KeyId>,
}

impl RootMetadataBuilder {
    /// Create a new `RootMetadataBuilder`. It defaults to:
    ///
    /// * version: 1,
    /// * expires: 365 days from the current time.
    /// * consistent snapshot: true
    /// * role thresholds: 1
    pub fn new() -> Self {
        RootMetadataBuilder {
            version: 1,
            expires: Utc::now() + Duration::days(365),
            consistent_snapshot: true,
            keys: HashMap::new(),
            root_threshold: 1,
            root_key_ids: HashSet::new(),
            snapshot_threshold: 1,
            snapshot_key_ids: HashSet::new(),
            targets_threshold: 1,
            targets_key_ids: HashSet::new(),
            timestamp_threshold: 1,
            timestamp_key_ids: HashSet::new(),
        }
    }

    /// Set the version number for this metadata.
    pub fn version(mut self, version: u32) -> Self {
        self.version = version;
        self
    }

    /// Set the time this metadata expires.
    pub fn expires(mut self, expires: DateTime<Utc>) -> Self {
        self.expires = expires;
        self
    }

    /// Set this metadata to have a consistent snapshot.
    pub fn consistent_snapshot(mut self, consistent_snapshot: bool) -> Self {
        self.consistent_snapshot = consistent_snapshot;
        self
    }

    /// Set the root threshold.
    pub fn root_threshold(mut self, threshold: u32) -> Self {
        self.root_threshold = threshold;
        self
    }

    /// Add a root public key.
    pub fn root_key(mut self, public_key: PublicKey) -> Self {
        let key_id = public_key.key_id().clone();
        self.keys.insert(key_id.clone(), public_key);
        self.root_key_ids.insert(key_id);
        self
    }

    /// Set the snapshot threshold.
    pub fn snapshot_threshold(mut self, threshold: u32) -> Self {
        self.snapshot_threshold = threshold;
        self
    }

    /// Add a snapshot public key.
    pub fn snapshot_key(mut self, public_key: PublicKey) -> Self {
        let key_id = public_key.key_id().clone();
        self.keys.insert(key_id.clone(), public_key);
        self.snapshot_key_ids.insert(key_id);
        self
    }

    /// Set the targets threshold.
    pub fn targets_threshold(mut self, threshold: u32) -> Self {
        self.targets_threshold = threshold;
        self
    }

    /// Add a targets public key.
    pub fn targets_key(mut self, public_key: PublicKey) -> Self {
        let key_id = public_key.key_id().clone();
        self.keys.insert(key_id.clone(), public_key);
        self.targets_key_ids.insert(key_id);
        self
    }

    /// Set the timestamp threshold.
    pub fn timestamp_threshold(mut self, threshold: u32) -> Self {
        self.timestamp_threshold = threshold;
        self
    }

    /// Add a timestamp public key.
    pub fn timestamp_key(mut self, public_key: PublicKey) -> Self {
        let key_id = public_key.key_id().clone();
        self.keys.insert(key_id.clone(), public_key);
        self.timestamp_key_ids.insert(key_id);
        self
    }

    /// Construct a new `RootMetadata`.
    pub fn build(self) -> Result<RootMetadata> {
        RootMetadata::new(
            self.version,
            self.expires,
            self.consistent_snapshot,
            self.keys,
            RoleDefinition::new(self.root_threshold, self.root_key_ids)?,
            RoleDefinition::new(self.snapshot_threshold, self.snapshot_key_ids)?,
            RoleDefinition::new(self.targets_threshold, self.targets_key_ids)?,
            RoleDefinition::new(self.timestamp_threshold, self.timestamp_key_ids)?,
        )
    }

    /// Construct a new `SignedMetadata<D, RootMetadata>`.
    pub fn signed<D>(self, private_key: &dyn PrivateKey) -> Result<SignedMetadata<D, RootMetadata>>
    where
        D: Pouf,
    {
        SignedMetadata::new(&self.build()?, private_key)
    }
}

impl Default for RootMetadataBuilder {
    fn default() -> Self {
        RootMetadataBuilder::new()
    }
}

impl From<RootMetadata> for RootMetadataBuilder {
    fn from(metadata: RootMetadata) -> Self {
        RootMetadataBuilder {
            version: metadata.version,
            expires: metadata.expires,
            consistent_snapshot: metadata.consistent_snapshot,
            keys: metadata.keys,
            root_threshold: metadata.root.threshold,
            root_key_ids: metadata.root.key_ids,
            snapshot_threshold: metadata.snapshot.threshold,
            snapshot_key_ids: metadata.snapshot.key_ids,
            targets_threshold: metadata.targets.threshold,
            targets_key_ids: metadata.targets.key_ids,
            timestamp_threshold: metadata.timestamp.threshold,
            timestamp_key_ids: metadata.timestamp.key_ids,
        }
    }
}

/// Metadata for the root role.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct RootMetadata {
    version: u32,
    expires: DateTime<Utc>,
    consistent_snapshot: bool,
    keys: HashMap<KeyId, PublicKey>,
    root: RoleDefinition<RootMetadata>,
    snapshot: RoleDefinition<SnapshotMetadata>,
    targets: RoleDefinition<TargetsMetadata>,
    timestamp: RoleDefinition<TimestampMetadata>,
}

impl RootMetadata {
    /// Create new `RootMetadata`.
    pub fn new(
        version: u32,
        expires: DateTime<Utc>,
        consistent_snapshot: bool,
        keys: HashMap<KeyId, PublicKey>,
        root: RoleDefinition<RootMetadata>,
        snapshot: RoleDefinition<SnapshotMetadata>,
        targets: RoleDefinition<TargetsMetadata>,
        timestamp: RoleDefinition<TimestampMetadata>,
    ) -> Result<Self> {
        if version < 1 {
            return Err(Error::MetadataVersionMustBeGreaterThanZero(
                MetadataPath::root(),
            ));
        }

        Ok(RootMetadata {
            version,
            expires,
            consistent_snapshot,
            keys,
            root,
            snapshot,
            targets,
            timestamp,
        })
    }

    /// Whether or not this repository is currently implementing that TUF consistent snapshot
    /// feature.
    pub fn consistent_snapshot(&self) -> bool {
        self.consistent_snapshot
    }

    /// An immutable reference to the map of trusted keys.
    pub fn keys(&self) -> &HashMap<KeyId, PublicKey> {
        &self.keys
    }

    /// An iterator over all the trusted root public keys.
    pub fn root_keys(&self) -> impl Iterator<Item = &PublicKey> {
        self.root
            .key_ids()
            .iter()
            .filter_map(|key_id| self.keys.get(key_id))
    }

    /// An iterator over all the trusted targets public keys.
    pub fn targets_keys(&self) -> impl Iterator<Item = &PublicKey> {
        self.targets
            .key_ids()
            .iter()
            .filter_map(|key_id| self.keys.get(key_id))
    }

    /// An iterator over all the trusted snapshot public keys.
    pub fn snapshot_keys(&self) -> impl Iterator<Item = &PublicKey> {
        self.snapshot
            .key_ids()
            .iter()
            .filter_map(|key_id| self.keys.get(key_id))
    }

    /// An iterator over all the trusted timestamp public keys.
    pub fn timestamp_keys(&self) -> impl Iterator<Item = &PublicKey> {
        self.timestamp
            .key_ids()
            .iter()
            .filter_map(|key_id| self.keys.get(key_id))
    }

    /// An immutable reference to the root role's definition.
    pub fn root(&self) -> &RoleDefinition<RootMetadata> {
        &self.root
    }

    /// An immutable reference to the snapshot role's definition.
    pub fn snapshot(&self) -> &RoleDefinition<SnapshotMetadata> {
        &self.snapshot
    }

    /// An immutable reference to the targets role's definition.
    pub fn targets(&self) -> &RoleDefinition<TargetsMetadata> {
        &self.targets
    }

    /// An immutable reference to the timestamp role's definition.
    pub fn timestamp(&self) -> &RoleDefinition<TimestampMetadata> {
        &self.timestamp
    }
}

impl Metadata for RootMetadata {
    const ROLE: Role = Role::Root;

    fn version(&self) -> u32 {
        self.version
    }

    fn expires(&self) -> &DateTime<Utc> {
        &self.expires
    }
}

impl Serialize for RootMetadata {
    fn serialize<S>(&self, ser: S) -> ::std::result::Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        let m = shims::RootMetadata::from(self)
            .map_err(|e| SerializeError::custom(format!("{:?}", e)))?;
        m.serialize(ser)
    }
}

impl<'de> Deserialize<'de> for RootMetadata {
    fn deserialize<D: Deserializer<'de>>(de: D) -> ::std::result::Result<Self, D::Error> {
        let intermediate: shims::RootMetadata = Deserialize::deserialize(de)?;
        intermediate
            .try_into()
            .map_err(|e| DeserializeError::custom(format!("{:?}", e)))
    }
}

/// The definition of what allows a role to be trusted.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct RoleDefinition<M: Metadata> {
    threshold: u32,
    key_ids: HashSet<KeyId>,
    _metadata: PhantomData<M>,
}

impl<M: Metadata> RoleDefinition<M> {
    /// Create a new [RoleDefinition] with a given threshold and set of authorized [KeyId]s.
    pub fn new(threshold: u32, key_ids: HashSet<KeyId>) -> Result<Self> {
        if threshold < 1 {
            return Err(Error::MetadataThresholdMustBeGreaterThanZero(
                M::ROLE.into(),
            ));
        }

        if (key_ids.len() as u64) < u64::from(threshold) {
            return Err(Error::MetadataRoleDoesNotHaveEnoughKeyIds {
                role: M::ROLE.into(),
                key_ids: key_ids.len(),
                threshold,
            });
        }

        Ok(RoleDefinition {
            threshold,
            key_ids,
            _metadata: PhantomData,
        })
    }

    /// The threshold number of signatures required for the role to be trusted.
    pub fn threshold(&self) -> u32 {
        self.threshold
    }

    /// An immutable reference to the set of `KeyID`s that are authorized to sign the role.
    pub fn key_ids(&self) -> &HashSet<KeyId> {
        &self.key_ids
    }
}

impl<M: Metadata> Serialize for RoleDefinition<M> {
    fn serialize<S>(&self, ser: S) -> ::std::result::Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        shims::RoleDefinition::from(self).serialize(ser)
    }
}

impl<'de, M: Metadata> Deserialize<'de> for RoleDefinition<M> {
    fn deserialize<D: Deserializer<'de>>(de: D) -> ::std::result::Result<Self, D::Error> {
        let intermediate = shims::RoleDefinition::deserialize(de)?;
        intermediate
            .try_into()
            .map_err(|e| DeserializeError::custom(format!("{:?}", e)))
    }
}

/// Wrapper for a path to metadata.
///
/// Note: This should **not** contain the file extension. This is automatically added by the
/// library depending on what type of data pouf format is being used.
///
/// ```
/// use tuf::metadata::MetadataPath;
///
/// // right
/// let _ = MetadataPath::new("root");
///
/// // wrong
/// let _ = MetadataPath::new("root.json");
/// ```
#[derive(Debug, Clone, PartialEq, Eq, PartialOrd, Ord, Hash, Serialize)]
pub struct MetadataPath(Cow<'static, str>);

impl MetadataPath {
    /// Create a new `MetadataPath` for the Root role.
    pub fn root() -> Self {
        MetadataPath(Role::Root.name().into())
    }

    /// Create a new `MetadataPath` for the Timestamp role.
    pub fn timestamp() -> Self {
        MetadataPath(Role::Timestamp.name().into())
    }

    /// Create a new `MetadataPath` for the Snapshot role.
    pub fn snapshot() -> Self {
        MetadataPath(Role::Snapshot.name().into())
    }

    /// Create a new `MetadataPath` for the targets role.
    pub fn targets() -> Self {
        MetadataPath(Role::Targets.name().into())
    }

    /// Create a new `MetadataPath` from a `String`.
    ///
    /// ```
    /// # use tuf::metadata::MetadataPath;
    /// assert!(MetadataPath::new("foo").is_ok());
    /// assert!(MetadataPath::new("/foo").is_err());
    /// assert!(MetadataPath::new("../foo").is_err());
    /// assert!(MetadataPath::new("foo/..").is_err());
    /// assert!(MetadataPath::new("foo/../bar").is_err());
    /// assert!(MetadataPath::new("..foo").is_ok());
    /// assert!(MetadataPath::new("foo/..bar").is_ok());
    /// assert!(MetadataPath::new("foo/bar..").is_ok());
    /// ```
    pub fn new<P: Into<Cow<'static, str>>>(path: P) -> Result<Self> {
        let path = path.into();
        match path.as_ref() {
            "root" => Ok(MetadataPath::root()),
            "timestamp" => Ok(MetadataPath::timestamp()),
            "snapshot" => Ok(MetadataPath::snapshot()),
            "targets" => Ok(MetadataPath::targets()),
            _ => {
                safe_path(&path)?;
                Ok(MetadataPath(path))
            }
        }
    }

    /// Split `MetadataPath` into components that can be joined to create URL paths, Unix paths, or
    /// Windows paths.
    ///
    /// ```
    /// # use tuf::crypto::HashValue;
    /// # use tuf::pouf::Pouf1;
    /// # use tuf::metadata::{MetadataPath, MetadataVersion};
    /// #
    /// let path = MetadataPath::new("foo/bar").unwrap();
    /// assert_eq!(path.components::<Pouf1>(MetadataVersion::None),
    ///            ["foo".to_string(), "bar.json".to_string()]);
    /// assert_eq!(path.components::<Pouf1>(MetadataVersion::Number(1)),
    ///            ["foo".to_string(), "1.bar.json".to_string()]);
    /// ```
    pub fn components<D>(&self, version: MetadataVersion) -> Vec<String>
    where
        D: Pouf,
    {
        let mut buf: Vec<String> = self.0.split('/').map(|s| s.to_string()).collect();
        let len = buf.len();
        buf[len - 1] = format!("{}{}.{}", version.prefix(), buf[len - 1], D::extension());
        buf
    }
}

impl From<Role> for MetadataPath {
    fn from(role: Role) -> MetadataPath {
        match role {
            Role::Root => MetadataPath::root(),
            Role::Timestamp => MetadataPath::timestamp(),
            Role::Snapshot => MetadataPath::snapshot(),
            Role::Targets => MetadataPath::targets(),
        }
    }
}

impl Display for MetadataPath {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        f.write_str(&self.0)
    }
}

impl<'de> Deserialize<'de> for MetadataPath {
    fn deserialize<D: Deserializer<'de>>(de: D) -> ::std::result::Result<Self, D::Error> {
        let s: String = Deserialize::deserialize(de)?;
        MetadataPath::new(s).map_err(|e| DeserializeError::custom(format!("{:?}", e)))
    }
}

/// Helper to construct `TimestampMetadata`.
pub struct TimestampMetadataBuilder {
    version: u32,
    expires: DateTime<Utc>,
    snapshot: MetadataDescription<SnapshotMetadata>,
}

impl TimestampMetadataBuilder {
    /// Create a new `TimestampMetadataBuilder` from a given snapshot. It defaults to:
    ///
    /// * version: 1
    /// * expires: 1 day from the current time.
    pub fn from_snapshot<D>(
        snapshot: &SignedMetadata<D, SnapshotMetadata>,
        hash_algs: &[HashAlgorithm],
    ) -> Result<Self>
    where
        D: Pouf,
    {
        let raw_snapshot = snapshot.to_raw()?;
        let description = MetadataDescription::from_slice(
            raw_snapshot.as_bytes(),
            snapshot.parse_version_untrusted()?,
            hash_algs,
        )?;

        Ok(Self::from_metadata_description(description))
    }

    /// Create a new `TimestampMetadataBuilder` from a given
    /// `MetadataDescription`. It defaults to:
    ///
    /// * version: 1
    /// * expires: 1 day from the current time.
    pub fn from_metadata_description(description: MetadataDescription<SnapshotMetadata>) -> Self {
        TimestampMetadataBuilder {
            version: 1,
            expires: Utc::now() + Duration::days(1),
            snapshot: description,
        }
    }

    /// Set the version number for this metadata.
    pub fn version(mut self, version: u32) -> Self {
        self.version = version;
        self
    }

    /// Set the time this metadata expires.
    pub fn expires(mut self, expires: DateTime<Utc>) -> Self {
        self.expires = expires;
        self
    }

    /// Construct a new `TimestampMetadata`.
    pub fn build(self) -> Result<TimestampMetadata> {
        TimestampMetadata::new(self.version, self.expires, self.snapshot)
    }

    /// Construct a new `SignedMetadata<D, TimestampMetadata>`.
    pub fn signed<D>(
        self,
        private_key: &dyn PrivateKey,
    ) -> Result<SignedMetadata<D, TimestampMetadata>>
    where
        D: Pouf,
    {
        SignedMetadata::new(&self.build()?, private_key)
    }
}

/// Metadata for the timestamp role.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct TimestampMetadata {
    version: u32,
    expires: DateTime<Utc>,
    snapshot: MetadataDescription<SnapshotMetadata>,
}

impl TimestampMetadata {
    /// Create new `TimestampMetadata`.
    pub fn new(
        version: u32,
        expires: DateTime<Utc>,
        snapshot: MetadataDescription<SnapshotMetadata>,
    ) -> Result<Self> {
        if version < 1 {
            return Err(Error::MetadataVersionMustBeGreaterThanZero(
                MetadataPath::timestamp(),
            ));
        }

        Ok(TimestampMetadata {
            version,
            expires,
            snapshot,
        })
    }

    /// An immutable reference to the snapshot description.
    pub fn snapshot(&self) -> &MetadataDescription<SnapshotMetadata> {
        &self.snapshot
    }
}

impl Metadata for TimestampMetadata {
    const ROLE: Role = Role::Timestamp;

    fn version(&self) -> u32 {
        self.version
    }

    fn expires(&self) -> &DateTime<Utc> {
        &self.expires
    }
}

impl Serialize for TimestampMetadata {
    fn serialize<S>(&self, ser: S) -> ::std::result::Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        shims::TimestampMetadata::from(self)
            .map_err(|e| SerializeError::custom(format!("{:?}", e)))?
            .serialize(ser)
    }
}

impl<'de> Deserialize<'de> for TimestampMetadata {
    fn deserialize<D: Deserializer<'de>>(de: D) -> ::std::result::Result<Self, D::Error> {
        let intermediate: shims::TimestampMetadata = Deserialize::deserialize(de)?;
        intermediate
            .try_into()
            .map_err(|e| DeserializeError::custom(format!("{:?}", e)))
    }
}

/// Description of a piece of metadata, used in verification.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct MetadataDescription<M: Metadata> {
    version: u32,
    length: Option<usize>,
    hashes: HashMap<HashAlgorithm, HashValue>,
    _metadata: PhantomData<M>,
}

impl<M: Metadata> MetadataDescription<M> {
    /// Create a `MetadataDescription` from a slice. Size and hashes will be calculated.
    pub fn from_slice(buf: &[u8], version: u32, hash_algs: &[HashAlgorithm]) -> Result<Self> {
        if version < 1 {
            return Err(Error::IllegalArgument(
                "Version must be greater than zero".into(),
            ));
        }

        let hashes = if hash_algs.is_empty() {
            HashMap::new()
        } else {
            crypto::calculate_hashes_from_slice(buf, hash_algs)?
        };

        Ok(MetadataDescription {
            version,
            length: Some(buf.len()),
            hashes,
            _metadata: PhantomData,
        })
    }

    /// Create a new `MetadataDescription`.
    pub fn new(
        version: u32,
        length: Option<usize>,
        hashes: HashMap<HashAlgorithm, HashValue>,
    ) -> Result<Self> {
        if version < 1 {
            return Err(Error::MetadataVersionMustBeGreaterThanZero(M::ROLE.into()));
        }

        Ok(MetadataDescription {
            version,
            length,
            hashes,
            _metadata: PhantomData,
        })
    }

    /// The version of the described metadata.
    pub fn version(&self) -> u32 {
        self.version
    }

    /// The length of the described metadata.
    pub fn length(&self) -> Option<usize> {
        self.length
    }

    /// An immutable reference to the hashes of the described metadata.
    pub fn hashes(&self) -> &HashMap<HashAlgorithm, HashValue> {
        &self.hashes
    }
}

impl<M: Metadata> Serialize for MetadataDescription<M> {
    fn serialize<S>(&self, ser: S) -> ::std::result::Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        shims::MetadataDescription::from(self).serialize(ser)
    }
}

impl<'de, M: Metadata> Deserialize<'de> for MetadataDescription<M> {
    fn deserialize<D: Deserializer<'de>>(de: D) -> ::std::result::Result<Self, D::Error> {
        let intermediate = shims::MetadataDescription::deserialize(de)?;
        intermediate
            .try_into()
            .map_err(|e| DeserializeError::custom(format!("{:?}", e)))
    }
}

/// Helper to construct `SnapshotMetadata`.
pub struct SnapshotMetadataBuilder {
    version: u32,
    expires: DateTime<Utc>,
    meta: HashMap<MetadataPath, MetadataDescription<TargetsMetadata>>,
}

impl SnapshotMetadataBuilder {
    /// Create a new `SnapshotMetadataBuilder`. It defaults to:
    ///
    /// * version: 1
    /// * expires: 7 days from the current time.
    pub fn new() -> Self {
        SnapshotMetadataBuilder {
            version: 1,
            expires: Utc::now() + Duration::days(7),
            meta: HashMap::new(),
        }
    }

    /// Create a new [SnapshotMetadataBuilder] from a given snapshot. It defaults to:
    ///
    /// * version: 1
    /// * expires: 7 day from the current time.
    pub fn from_targets<D>(
        targets: &SignedMetadata<D, TargetsMetadata>,
        hash_algs: &[HashAlgorithm],
    ) -> Result<Self>
    where
        D: Pouf,
    {
        SnapshotMetadataBuilder::new().insert_metadata(targets, hash_algs)
    }

    /// Set the version number for this metadata.
    pub fn version(mut self, version: u32) -> Self {
        self.version = version;
        self
    }

    /// Set the time this metadata expires.
    pub fn expires(mut self, expires: DateTime<Utc>) -> Self {
        self.expires = expires;
        self
    }

    /// Add metadata to this snapshot metadata using the default path.
    pub fn insert_metadata<D, M>(
        self,
        metadata: &SignedMetadata<D, M>,
        hash_algs: &[HashAlgorithm],
    ) -> Result<Self>
    where
        M: Metadata,
        D: Pouf,
    {
        self.insert_metadata_with_path(M::ROLE.name(), metadata, hash_algs)
    }

    /// Add metadata to this snapshot metadata using a custom path.
    pub fn insert_metadata_with_path<P, D, M>(
        self,
        path: P,
        metadata: &SignedMetadata<D, M>,
        hash_algs: &[HashAlgorithm],
    ) -> Result<Self>
    where
        P: Into<Cow<'static, str>>,
        M: Metadata,
        D: Pouf,
    {
        let raw_metadata = metadata.to_raw()?;
        let description = MetadataDescription::from_slice(
            raw_metadata.as_bytes(),
            metadata.parse_version_untrusted()?,
            hash_algs,
        )?;
        let path = MetadataPath::new(path)?;
        Ok(self.insert_metadata_description(path, description))
    }

    /// Add `MetadataDescription` to this snapshot metadata using a custom path.
    pub fn insert_metadata_description(
        mut self,
        path: MetadataPath,
        description: MetadataDescription<TargetsMetadata>,
    ) -> Self {
        self.meta.insert(path, description);
        self
    }

    /// Construct a new `SnapshotMetadata`.
    pub fn build(self) -> Result<SnapshotMetadata> {
        SnapshotMetadata::new(self.version, self.expires, self.meta)
    }

    /// Construct a new `SignedMetadata<D, SnapshotMetadata>`.
    pub fn signed<D>(
        self,
        private_key: &dyn PrivateKey,
    ) -> Result<SignedMetadata<D, SnapshotMetadata>>
    where
        D: Pouf,
    {
        SignedMetadata::new(&self.build()?, private_key)
    }
}

impl Default for SnapshotMetadataBuilder {
    fn default() -> Self {
        SnapshotMetadataBuilder::new()
    }
}

impl From<SnapshotMetadata> for SnapshotMetadataBuilder {
    fn from(meta: SnapshotMetadata) -> Self {
        SnapshotMetadataBuilder {
            version: meta.version,
            expires: meta.expires,
            meta: meta.meta,
        }
    }
}

/// Metadata for the snapshot role.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SnapshotMetadata {
    version: u32,
    expires: DateTime<Utc>,
    meta: HashMap<MetadataPath, MetadataDescription<TargetsMetadata>>,
}

impl SnapshotMetadata {
    /// Create new `SnapshotMetadata`.
    pub fn new(
        version: u32,
        expires: DateTime<Utc>,
        meta: HashMap<MetadataPath, MetadataDescription<TargetsMetadata>>,
    ) -> Result<Self> {
        if version < 1 {
            return Err(Error::MetadataVersionMustBeGreaterThanZero(
                MetadataPath::snapshot(),
            ));
        }

        Ok(SnapshotMetadata {
            version,
            expires,
            meta,
        })
    }

    /// An immutable reference to the metadata paths and descriptions.
    pub fn meta(&self) -> &HashMap<MetadataPath, MetadataDescription<TargetsMetadata>> {
        &self.meta
    }
}

impl Metadata for SnapshotMetadata {
    const ROLE: Role = Role::Snapshot;

    fn version(&self) -> u32 {
        self.version
    }

    fn expires(&self) -> &DateTime<Utc> {
        &self.expires
    }
}

impl Serialize for SnapshotMetadata {
    fn serialize<S>(&self, ser: S) -> ::std::result::Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        shims::SnapshotMetadata::from(self)
            .map_err(|e| SerializeError::custom(format!("{:?}", e)))?
            .serialize(ser)
    }
}

impl<'de> Deserialize<'de> for SnapshotMetadata {
    fn deserialize<D: Deserializer<'de>>(de: D) -> ::std::result::Result<Self, D::Error> {
        let intermediate: shims::SnapshotMetadata = Deserialize::deserialize(de)?;
        intermediate
            .try_into()
            .map_err(|e| DeserializeError::custom(format!("{:?}", e)))
    }
}

/// Wrapper for the virtual path to a target.
#[derive(Debug, Clone, PartialEq, Hash, Eq, PartialOrd, Ord, Serialize)]
pub struct TargetPath(String);

impl TargetPath {
    /// Create a new `TargetPath` from a `String`.
    ///
    /// ```
    /// # use tuf::metadata::TargetPath;
    /// assert!(TargetPath::new("foo").is_ok());
    /// assert!(TargetPath::new("/foo").is_err());
    /// assert!(TargetPath::new("../foo").is_err());
    /// assert!(TargetPath::new("foo/..").is_err());
    /// assert!(TargetPath::new("foo/../bar").is_err());
    /// assert!(TargetPath::new("..foo").is_ok());
    /// assert!(TargetPath::new("foo/..bar").is_ok());
    /// assert!(TargetPath::new("foo/bar..").is_ok());
    /// ```
    pub fn new<P: Into<String>>(path: P) -> Result<Self> {
        let path = path.into();
        safe_path(&path)?;
        Ok(TargetPath(path))
    }

    /// Split `TargetPath` into components that can be joined to create URL paths, Unix
    /// paths, or Windows paths.
    ///
    /// ```
    /// # use tuf::metadata::TargetPath;
    /// let path = TargetPath::new("foo/bar").unwrap();
    /// assert_eq!(path.components(), ["foo".to_string(), "bar".to_string()]);
    /// ```
    pub fn components(&self) -> Vec<String> {
        self.0.split('/').map(|s| s.to_string()).collect()
    }

    /// Return whether this path is the child of another path.
    ///
    /// ```
    /// # use tuf::metadata::TargetPath;
    /// let path1 = TargetPath::new("foo").unwrap();
    /// let path2 = TargetPath::new("foo/bar").unwrap();
    /// assert!(!path2.is_child(&path1));
    ///
    /// let path1 = TargetPath::new("foo/").unwrap();
    /// let path2 = TargetPath::new("foo/bar").unwrap();
    /// assert!(path2.is_child(&path1));
    ///
    /// let path2 = TargetPath::new("foo/bar/baz").unwrap();
    /// assert!(path2.is_child(&path1));
    ///
    /// let path2 = TargetPath::new("wat").unwrap();
    /// assert!(!path2.is_child(&path1))
    /// ```
    pub fn is_child(&self, parent: &Self) -> bool {
        if !parent.0.ends_with('/') {
            return false;
        }

        self.0.starts_with(&parent.0)
    }

    /// Whether or not the current target is available at the end of the given chain of target
    /// paths. For the chain to be valid, each target path in a group must be a child of of all
    /// previous groups.
    // TODO this is hideous and uses way too much clone/heap but I think recursively,
    // so here we are
    pub fn matches_chain(&self, parents: &[HashSet<TargetPath>]) -> bool {
        if parents.is_empty() {
            return false;
        }
        if parents.len() == 1 {
            return parents[0].iter().any(|p| p == self || self.is_child(p));
        }

        let new = parents[1..]
            .iter()
            .map(|group| {
                group
                    .iter()
                    .filter(|parent| {
                        parents[0]
                            .iter()
                            .any(|p| parent.is_child(p) || parent == &p)
                    })
                    .cloned()
                    .collect::<HashSet<_>>()
            })
            .collect::<Vec<_>>();
        self.matches_chain(&new)
    }

    /// Prefix the target path with a hash value to support TUF spec 5.5.2.
    pub fn with_hash_prefix(&self, hash: &HashValue) -> Result<TargetPath> {
        let mut components = self.components();

        let file_name = components
            .pop()
            .ok_or_else(|| Error::IllegalArgument("Path cannot be empty".into()))?;

        components.push(format!("{}.{}", hash, file_name));

        TargetPath::new(components.join("/"))
    }

    /// The string value of the path.
    pub fn as_str(&self) -> &str {
        &self.0
    }
}

impl Display for TargetPath {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        f.write_str(&self.0)
    }
}

impl<'de> Deserialize<'de> for TargetPath {
    fn deserialize<D: Deserializer<'de>>(de: D) -> ::std::result::Result<Self, D::Error> {
        let s: String = Deserialize::deserialize(de)?;
        TargetPath::new(s).map_err(|e| DeserializeError::custom(format!("{:?}", e)))
    }
}

impl Borrow<str> for TargetPath {
    fn borrow(&self) -> &str {
        self.as_str()
    }
}

/// Description of a target, used in verification.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct TargetDescription {
    length: u64,
    hashes: HashMap<HashAlgorithm, HashValue>,
    custom: HashMap<String, serde_json::Value>,
}

impl TargetDescription {
    /// Create a new `TargetDescription`.
    ///
    /// Note: Creating this manually could lead to errors, and the `from_reader` method is
    /// preferred.
    pub fn new(
        length: u64,
        hashes: HashMap<HashAlgorithm, HashValue>,
        custom: HashMap<String, serde_json::Value>,
    ) -> Result<Self> {
        if hashes.is_empty() {
            return Err(Error::IllegalArgument(
                "Cannot have empty set of hashes".into(),
            ));
        }

        Ok(TargetDescription {
            length,
            hashes,
            custom,
        })
    }

    /// Read the from the given slice and calculate the length and hash values.
    ///
    /// ```
    /// # use data_encoding::BASE64URL;
    /// # use tuf::crypto::{HashAlgorithm,HashValue};
    /// # use tuf::metadata::TargetDescription;
    /// #
    /// let bytes: &[u8] = b"it was a pleasure to burn";
    ///
    /// let target_description = TargetDescription::from_slice(
    ///     bytes,
    ///     &[HashAlgorithm::Sha256, HashAlgorithm::Sha512],
    /// ).unwrap();
    ///
    /// let s = "Rd9zlbzrdWfeL7gnIEi05X-Yv2TCpy4qqZM1N72ZWQs=";
    /// let sha256 = HashValue::new(BASE64URL.decode(s.as_bytes()).unwrap());
    ///
    /// let s ="tuIxwKybYdvJpWuUj6dubvpwhkAozWB6hMJIRzqn2jOUdtDTBg381brV4K\
    ///     BU1zKP8GShoJuXEtCf5NkDTCEJgQ==";
    /// let sha512 = HashValue::new(BASE64URL.decode(s.as_bytes()).unwrap());
    ///
    /// assert_eq!(target_description.length(), bytes.len() as u64);
    /// assert_eq!(target_description.hashes().get(&HashAlgorithm::Sha256), Some(&sha256));
    /// assert_eq!(target_description.hashes().get(&HashAlgorithm::Sha512), Some(&sha512));
    /// ```
    pub fn from_slice(buf: &[u8], hash_algs: &[HashAlgorithm]) -> Result<Self> {
        Self::from_slice_with_custom(buf, hash_algs, HashMap::new())
    }

    /// Read the from the given reader and custom metadata and calculate the length and hash
    /// values.
    ///
    /// ```
    /// # use data_encoding::BASE64URL;
    /// # use serde_json::Value;
    /// # use std::collections::HashMap;
    /// # use tuf::crypto::{HashAlgorithm,HashValue};
    /// # use tuf::metadata::TargetDescription;
    /// #
    /// let bytes: &[u8] = b"it was a pleasure to burn";
    ///
    /// let mut custom = HashMap::new();
    /// custom.insert("Hello".into(), "World".into());
    ///
    /// let target_description = TargetDescription::from_slice_with_custom(
    ///     bytes,
    ///     &[HashAlgorithm::Sha256, HashAlgorithm::Sha512],
    ///     custom,
    /// ).unwrap();
    ///
    /// let s = "Rd9zlbzrdWfeL7gnIEi05X-Yv2TCpy4qqZM1N72ZWQs=";
    /// let sha256 = HashValue::new(BASE64URL.decode(s.as_bytes()).unwrap());
    ///
    /// let s ="tuIxwKybYdvJpWuUj6dubvpwhkAozWB6hMJIRzqn2jOUdtDTBg381brV4K\
    ///     BU1zKP8GShoJuXEtCf5NkDTCEJgQ==";
    /// let sha512 = HashValue::new(BASE64URL.decode(s.as_bytes()).unwrap());
    ///
    /// assert_eq!(target_description.length(), bytes.len() as u64);
    /// assert_eq!(target_description.hashes().get(&HashAlgorithm::Sha256), Some(&sha256));
    /// assert_eq!(target_description.hashes().get(&HashAlgorithm::Sha512), Some(&sha512));
    /// assert_eq!(target_description.custom().get("Hello"), Some(&"World".into()));
    /// ```
    pub fn from_slice_with_custom(
        buf: &[u8],
        hash_algs: &[HashAlgorithm],
        custom: HashMap<String, serde_json::Value>,
    ) -> Result<Self> {
        let hashes = crypto::calculate_hashes_from_slice(buf, hash_algs)?;
        Ok(TargetDescription {
            length: buf.len() as u64,
            hashes,
            custom,
        })
    }

    /// Read the from the given reader and calculate the length and hash values.
    ///
    /// ```
    /// # use data_encoding::BASE64URL;
    /// # use futures_executor::block_on;
    /// # use tuf::crypto::{HashAlgorithm,HashValue};
    /// # use tuf::metadata::TargetDescription;
    /// #
    /// # block_on(async {
    /// let bytes: &[u8] = b"it was a pleasure to burn";
    ///
    /// let target_description = TargetDescription::from_reader(
    ///     bytes,
    ///     &[HashAlgorithm::Sha256, HashAlgorithm::Sha512],
    /// ).await.unwrap();
    ///
    /// let s = "Rd9zlbzrdWfeL7gnIEi05X-Yv2TCpy4qqZM1N72ZWQs=";
    /// let sha256 = HashValue::new(BASE64URL.decode(s.as_bytes()).unwrap());
    ///
    /// let s ="tuIxwKybYdvJpWuUj6dubvpwhkAozWB6hMJIRzqn2jOUdtDTBg381brV4K\
    ///     BU1zKP8GShoJuXEtCf5NkDTCEJgQ==";
    /// let sha512 = HashValue::new(BASE64URL.decode(s.as_bytes()).unwrap());
    ///
    /// assert_eq!(target_description.length(), bytes.len() as u64);
    /// assert_eq!(target_description.hashes().get(&HashAlgorithm::Sha256), Some(&sha256));
    /// assert_eq!(target_description.hashes().get(&HashAlgorithm::Sha512), Some(&sha512));
    /// # })
    /// ```
    pub async fn from_reader<R>(read: R, hash_algs: &[HashAlgorithm]) -> Result<Self>
    where
        R: AsyncRead + Unpin,
    {
        Self::from_reader_with_custom(read, hash_algs, HashMap::new()).await
    }

    /// Read the from the given reader and custom metadata and calculate the length and hash
    /// values.
    ///
    /// ```
    /// # use data_encoding::BASE64URL;
    /// # use futures_executor::block_on;
    /// # use serde_json::Value;
    /// # use std::collections::HashMap;
    /// # use tuf::crypto::{HashAlgorithm,HashValue};
    /// # use tuf::metadata::TargetDescription;
    /// #
    /// # block_on(async {
    /// let bytes: &[u8] = b"it was a pleasure to burn";
    ///
    /// let mut custom = HashMap::new();
    /// custom.insert("Hello".into(), "World".into());
    ///
    /// let target_description = TargetDescription::from_reader_with_custom(
    ///     bytes,
    ///     &[HashAlgorithm::Sha256, HashAlgorithm::Sha512],
    ///     custom,
    /// ).await.unwrap();
    ///
    /// let s = "Rd9zlbzrdWfeL7gnIEi05X-Yv2TCpy4qqZM1N72ZWQs=";
    /// let sha256 = HashValue::new(BASE64URL.decode(s.as_bytes()).unwrap());
    ///
    /// let s ="tuIxwKybYdvJpWuUj6dubvpwhkAozWB6hMJIRzqn2jOUdtDTBg381brV4K\
    ///     BU1zKP8GShoJuXEtCf5NkDTCEJgQ==";
    /// let sha512 = HashValue::new(BASE64URL.decode(s.as_bytes()).unwrap());
    ///
    /// assert_eq!(target_description.length(), bytes.len() as u64);
    /// assert_eq!(target_description.hashes().get(&HashAlgorithm::Sha256), Some(&sha256));
    /// assert_eq!(target_description.hashes().get(&HashAlgorithm::Sha512), Some(&sha512));
    /// assert_eq!(target_description.custom().get("Hello"), Some(&"World".into()));
    /// })
    /// ```
    pub async fn from_reader_with_custom<R>(
        read: R,
        hash_algs: &[HashAlgorithm],
        custom: HashMap<String, serde_json::Value>,
    ) -> Result<Self>
    where
        R: AsyncRead + Unpin,
    {
        let (length, hashes) = crypto::calculate_hashes_from_reader(read, hash_algs).await?;
        Ok(TargetDescription {
            length,
            hashes,
            custom,
        })
    }

    /// The maximum length of the target.
    pub fn length(&self) -> u64 {
        self.length
    }

    /// An immutable reference to the list of calculated hashes.
    pub fn hashes(&self) -> &HashMap<HashAlgorithm, HashValue> {
        &self.hashes
    }

    /// An immutable reference to the custom metadata.
    pub fn custom(&self) -> &HashMap<String, serde_json::Value> {
        &self.custom
    }
}

impl Serialize for TargetDescription {
    fn serialize<S>(&self, ser: S) -> ::std::result::Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        shims::TargetDescription::from(self).serialize(ser)
    }
}

impl<'de> Deserialize<'de> for TargetDescription {
    fn deserialize<D: Deserializer<'de>>(de: D) -> ::std::result::Result<Self, D::Error> {
        let intermediate: shims::TargetDescription = Deserialize::deserialize(de)?;
        intermediate
            .try_into()
            .map_err(|e| DeserializeError::custom(format!("{:?}", e)))
    }
}

/// Metadata for the targets role.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct TargetsMetadata {
    version: u32,
    expires: DateTime<Utc>,
    targets: HashMap<TargetPath, TargetDescription>,
    delegations: Delegations,
}

impl TargetsMetadata {
    /// Create new `TargetsMetadata`.
    pub fn new(
        version: u32,
        expires: DateTime<Utc>,
        targets: HashMap<TargetPath, TargetDescription>,
        delegations: Delegations,
    ) -> Result<Self> {
        if version < 1 {
            return Err(Error::MetadataVersionMustBeGreaterThanZero(
                MetadataPath::targets(),
            ));
        }

        Ok(TargetsMetadata {
            version,
            expires,
            targets,
            delegations,
        })
    }

    /// An immutable reference to the descriptions of targets.
    pub fn targets(&self) -> &HashMap<TargetPath, TargetDescription> {
        &self.targets
    }

    /// An immutable reference to the optional delegations.
    pub fn delegations(&self) -> &Delegations {
        &self.delegations
    }
}

impl Metadata for TargetsMetadata {
    const ROLE: Role = Role::Targets;

    fn version(&self) -> u32 {
        self.version
    }

    fn expires(&self) -> &DateTime<Utc> {
        &self.expires
    }
}

impl Serialize for TargetsMetadata {
    fn serialize<S>(&self, ser: S) -> ::std::result::Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        shims::TargetsMetadata::from(self)
            .map_err(|e| SerializeError::custom(format!("{:?}", e)))?
            .serialize(ser)
    }
}

impl<'de> Deserialize<'de> for TargetsMetadata {
    fn deserialize<D: Deserializer<'de>>(de: D) -> ::std::result::Result<Self, D::Error> {
        let intermediate: shims::TargetsMetadata = Deserialize::deserialize(de)?;
        intermediate
            .try_into()
            .map_err(|e| DeserializeError::custom(format!("{:?}", e)))
    }
}

/// Helper to construct `TargetsMetadata`.
pub struct TargetsMetadataBuilder {
    version: u32,
    expires: DateTime<Utc>,
    targets: HashMap<TargetPath, TargetDescription>,
    delegations: Option<Delegations>,
}

impl TargetsMetadataBuilder {
    /// Create a new `TargetsMetadataBuilder`. It defaults to:
    ///
    /// * version: 1
    /// * expires: 90 days from the current time.
    pub fn new() -> Self {
        TargetsMetadataBuilder {
            version: 1,
            expires: Utc::now() + Duration::days(90),
            targets: HashMap::new(),
            delegations: None,
        }
    }

    /// Set the version number for this metadata.
    pub fn version(mut self, version: u32) -> Self {
        self.version = version;
        self
    }

    /// Set the time this metadata expires.
    pub fn expires(mut self, expires: DateTime<Utc>) -> Self {
        self.expires = expires;
        self
    }

    /// Add target to the target metadata.
    pub fn insert_target_from_slice(
        self,
        path: TargetPath,
        buf: &[u8],
        hash_algs: &[HashAlgorithm],
    ) -> Result<Self> {
        let description = TargetDescription::from_slice(buf, hash_algs)?;
        Ok(self.insert_target_description(path, description))
    }

    /// Add target to the target metadata.
    pub async fn insert_target_from_reader<R>(
        self,
        path: TargetPath,
        read: R,
        hash_algs: &[HashAlgorithm],
    ) -> Result<Self>
    where
        R: AsyncRead + Unpin,
    {
        let description = TargetDescription::from_reader(read, hash_algs).await?;
        Ok(self.insert_target_description(path, description))
    }

    /// Add `TargetDescription` to this target metadata target description.
    pub fn insert_target_description(
        mut self,
        path: TargetPath,
        description: TargetDescription,
    ) -> Self {
        self.targets.insert(path, description);
        self
    }

    /// Add `Delegations` to this target metadata.
    pub fn delegations(mut self, delegations: Delegations) -> Self {
        self.delegations = Some(delegations);
        self
    }

    /// Construct a new `TargetsMetadata`.
    pub fn build(self) -> Result<TargetsMetadata> {
        TargetsMetadata::new(
            self.version,
            self.expires,
            self.targets,
            self.delegations.unwrap_or_default(),
        )
    }

    /// Construct a new `SignedMetadata<D, TargetsMetadata>`.
    pub fn signed<D>(
        self,
        private_key: &dyn PrivateKey,
    ) -> Result<SignedMetadata<D, TargetsMetadata>>
    where
        D: Pouf,
    {
        SignedMetadata::new(&self.build()?, private_key)
    }
}

impl Default for TargetsMetadataBuilder {
    fn default() -> Self {
        TargetsMetadataBuilder::new()
    }
}

/// Wrapper to described a collections of delegations.
#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct Delegations {
    keys: HashMap<KeyId, PublicKey>,
    roles: Vec<Delegation>,
}

impl Delegations {
    /// Return a [DelegationsBuilder].
    pub fn builder() -> DelegationsBuilder {
        DelegationsBuilder::new()
    }

    // TODO check all keys are used
    // TODO check all roles have their ID in the set of keys
    /// Create a new `Delegations` wrapper from the given set of trusted keys and roles.
    pub fn new(keys: HashMap<KeyId, PublicKey>, roles: Vec<Delegation>) -> Result<Self> {
        if roles.len()
            != roles
                .iter()
                .map(|r| &r.name)
                .collect::<HashSet<&MetadataPath>>()
                .len()
        {
            return Err(Error::IllegalArgument(
                "Cannot have duplicated roles in delegations.".into(),
            ));
        }

        Ok(Delegations { keys, roles })
    }

    /// Return if this delegation is empty.
    pub fn is_empty(&self) -> bool {
        self.keys.is_empty() && self.roles.is_empty()
    }

    /// An immutable reference to the keys used for this set of delegations.
    pub fn keys(&self) -> &HashMap<KeyId, PublicKey> {
        &self.keys
    }

    /// An immutable reference to the delegated roles.
    pub fn roles(&self) -> &Vec<Delegation> {
        &self.roles
    }
}

impl Serialize for Delegations {
    fn serialize<S>(&self, ser: S) -> ::std::result::Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        shims::Delegations::from(self).serialize(ser)
    }
}

impl<'de> Deserialize<'de> for Delegations {
    fn deserialize<D: Deserializer<'de>>(de: D) -> ::std::result::Result<Self, D::Error> {
        let intermediate: shims::Delegations = Deserialize::deserialize(de)?;
        intermediate
            .try_into()
            .map_err(|e| DeserializeError::custom(format!("{:?}", e)))
    }
}

/// A builder for [Delegations].
#[derive(Default)]
pub struct DelegationsBuilder {
    keys: HashMap<KeyId, PublicKey>,
    roles: Vec<Delegation>,
    role_index: HashMap<MetadataPath, usize>,
}

impl DelegationsBuilder {
    /// Create a new [DelegationsBuilder].
    pub fn new() -> Self {
        Self {
            keys: HashMap::new(),
            roles: vec![],
            role_index: HashMap::new(),
        }
    }

    /// Include this key in the delegation [PublicKey] set.
    pub fn key(mut self, key: PublicKey) -> Self {
        self.keys.insert(key.key_id().clone(), key);
        self
    }

    /// Add a [Delegation].
    pub fn role(mut self, delegation: Delegation) -> Self {
        // The delegation list is ordered and unique by role name, so check if we should overwrite
        // the old delegation.
        if let Some(idx) = self.role_index.get(&delegation.name) {
            self.roles[*idx] = delegation;
        } else {
            self.role_index
                .insert(delegation.name.clone(), self.roles.len());

            self.roles.push(delegation);
        }

        self
    }

    /// Construct a new [Delegations].
    pub fn build(self) -> Result<Delegations> {
        Delegations::new(self.keys, self.roles)
    }
}

/// A delegated targets role.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Delegation {
    name: MetadataPath,
    terminating: bool,
    threshold: u32,
    key_ids: HashSet<KeyId>,
    paths: HashSet<TargetPath>,
}

impl Delegation {
    /// Create a new [DelegationBuilder] for a delegation named `role`.
    pub fn builder(role: MetadataPath) -> DelegationBuilder {
        DelegationBuilder::new(role)
    }

    /// Create a new delegation.
    pub fn new(
        name: MetadataPath,
        terminating: bool,
        threshold: u32,
        key_ids: HashSet<KeyId>,
        paths: HashSet<TargetPath>,
    ) -> Result<Self> {
        if key_ids.is_empty() {
            return Err(Error::IllegalArgument("Cannot have empty key IDs".into()));
        }

        if paths.is_empty() {
            return Err(Error::IllegalArgument("Cannot have empty paths".into()));
        }

        if threshold < 1 {
            return Err(Error::IllegalArgument("Cannot have threshold < 1".into()));
        }

        if (key_ids.len() as u64) < u64::from(threshold) {
            return Err(Error::IllegalArgument(
                "Cannot have threshold less than number of keys".into(),
            ));
        }

        Ok(Delegation {
            name,
            terminating,
            threshold,
            key_ids,
            paths,
        })
    }

    /// An immutable reference to the delegations's metadata path (role).
    pub fn name(&self) -> &MetadataPath {
        &self.name
    }

    /// Whether or not this delegation is terminating.
    pub fn terminating(&self) -> bool {
        self.terminating
    }

    /// An immutable reference to the delegations's trusted key IDs.
    pub fn key_ids(&self) -> &HashSet<KeyId> {
        &self.key_ids
    }

    /// The delegation's threshold.
    pub fn threshold(&self) -> u32 {
        self.threshold
    }

    /// An immutable reference to the delegation's authorized paths.
    pub fn paths(&self) -> &HashSet<TargetPath> {
        &self.paths
    }
}

impl Serialize for Delegation {
    fn serialize<S>(&self, ser: S) -> ::std::result::Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        shims::Delegation::from(self).serialize(ser)
    }
}

impl<'de> Deserialize<'de> for Delegation {
    fn deserialize<D: Deserializer<'de>>(de: D) -> ::std::result::Result<Self, D::Error> {
        let intermediate: shims::Delegation = Deserialize::deserialize(de)?;
        intermediate
            .try_into()
            .map_err(|e| DeserializeError::custom(format!("{:?}", e)))
    }
}

/// A builder for [Delegation].
pub struct DelegationBuilder {
    role: MetadataPath,
    terminating: bool,
    threshold: u32,
    key_ids: HashSet<KeyId>,
    paths: HashSet<TargetPath>,
}

impl DelegationBuilder {
    /// Create a new [DelegationBuilder] for a delegation named `role`.
    pub fn new(role: MetadataPath) -> Self {
        Self {
            role,
            terminating: false,
            threshold: 1,
            key_ids: HashSet::new(),
            paths: HashSet::new(),
        }
    }

    /// The threshold number of signatures required for the delegation to be trusted.
    pub fn threshold(mut self, threshold: u32) -> Self {
        self.threshold = threshold;
        self
    }

    /// This delegation can be signed by this [PublicKey].
    pub fn key(mut self, key: &PublicKey) -> Self {
        self.key_ids.insert(key.key_id().clone());
        self
    }

    /// This delegation can be signed by this [KeyId].
    pub fn key_id(mut self, key_id: KeyId) -> Self {
        self.key_ids.insert(key_id);
        self
    }

    /// Delegate `path` to this delegation.
    pub fn delegate_path(mut self, path: TargetPath) -> Self {
        self.paths.insert(path);
        self
    }

    /// Construct the [Delegation].
    pub fn build(self) -> Result<Delegation> {
        Delegation::new(
            self.role,
            self.terminating,
            self.threshold,
            self.key_ids,
            self.paths,
        )
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::crypto::Ed25519PrivateKey;
    use crate::pouf::Pouf1;
    use crate::verify::verify_signatures;
    use assert_matches::assert_matches;
    use chrono::prelude::*;
    use futures_executor::block_on;
    use maplit::{hashmap, hashset};
    use pretty_assertions::assert_eq;
    use serde_json::json;
    use std::str::FromStr;

    const ED25519_1_PK8: &[u8] = include_bytes!("../tests/ed25519/ed25519-1.pk8.der");
    const ED25519_2_PK8: &[u8] = include_bytes!("../tests/ed25519/ed25519-2.pk8.der");
    const ED25519_3_PK8: &[u8] = include_bytes!("../tests/ed25519/ed25519-3.pk8.der");
    const ED25519_4_PK8: &[u8] = include_bytes!("../tests/ed25519/ed25519-4.pk8.der");

    #[test]
    fn no_pardir_in_target_path() {
        let bad_paths = &[
            "..",
            "../some/path",
            "../some/path/",
            "some/../path",
            "some/../path/..",
        ];

        for path in bad_paths.iter() {
            assert!(safe_path(path).is_err());
            assert!(TargetPath::new(path.to_string()).is_err());
            assert!(MetadataPath::new(path.to_string()).is_err());
            assert!(TargetPath::new(path.to_string()).is_err());
        }
    }

    #[test]
    fn allow_asterisk_in_target_path() {
        let good_paths = &[
            "*",
            "*/some/path",
            "*/some/path/",
            "some/*/path",
            "some/*/path/*",
        ];

        for path in good_paths.iter() {
            assert!(safe_path(path).is_ok());
            assert!(TargetPath::new(path.to_string()).is_ok());
            assert!(MetadataPath::new(path.to_string()).is_ok());
        }
    }

    #[test]
    fn path_matches_chain() {
        let test_cases: &[(bool, &str, &[&[&str]])] = &[
            // simplest case
            (true, "foo", &[&["foo"]]),
            // direct delegation case
            (true, "foo", &[&["foo"], &["foo"]]),
            // is a dir
            (false, "foo", &[&["foo/"]]),
            // target not in last position
            (false, "foo", &[&["foo"], &["bar"]]),
            // target nested
            (true, "foo/bar", &[&["foo/"], &["foo/bar"]]),
            // target illegally nested
            (false, "foo/bar", &[&["baz/"], &["foo/bar"]]),
            // target illegally deeply nested
            (
                false,
                "foo/bar/baz",
                &[&["foo/"], &["foo/quux/"], &["foo/bar/baz"]],
            ),
            // empty
            (false, "foo", &[&[]]),
            // empty 2
            (false, "foo", &[&[], &["foo"]]),
            // empty 3
            (false, "foo", &[&["foo"], &[]]),
        ];

        for case in test_cases {
            let expected = case.0;
            let target = TargetPath::new(case.1).unwrap();
            let parents = case
                .2
                .iter()
                .map(|group| {
                    group
                        .iter()
                        .map(|p| TargetPath::new(p.to_string()).unwrap())
                        .collect::<HashSet<_>>()
                })
                .collect::<Vec<_>>();
            println!(
                "CASE: expect: {} path: {:?} parents: {:?}",
                expected, target, parents
            );
            assert_eq!(target.matches_chain(&parents), expected);
        }
    }

    #[test]
    fn serde_target_path() {
        let s = "foo/bar";
        let t = serde_json::from_str::<TargetPath>(&format!("\"{}\"", s)).unwrap();
        assert_eq!(t.to_string().as_str(), s);
        assert_eq!(serde_json::to_value(t).unwrap(), json!("foo/bar"));
    }

    #[test]
    fn serde_metadata_path() {
        let s = "foo/bar";
        let m = serde_json::from_str::<MetadataPath>(&format!("\"{}\"", s)).unwrap();
        assert_eq!(m.to_string().as_str(), s);
        assert_eq!(serde_json::to_value(m).unwrap(), json!("foo/bar"));
    }

    #[test]
    fn serde_target_description() {
        let s: &[u8] = b"from water does all life begin";
        let description = TargetDescription::from_slice(s, &[HashAlgorithm::Sha256]).unwrap();
        let jsn_str = serde_json::to_string(&description).unwrap();
        let jsn = json!({
            "length": 30,
            "hashes": {
                "sha256": "fc5d745c712bc86ea9a31264dac0c956eeb53857f677eed05829\
                    bb71013cae18",
            },
        });
        let parsed_str: TargetDescription = serde_json::from_str(&jsn_str).unwrap();
        let parsed_jsn: TargetDescription = serde_json::from_value(jsn).unwrap();
        assert_eq!(parsed_str, parsed_jsn);
    }

    #[test]
    fn serde_role_definition() {
        // keyid ordering must be preserved.
        let keyids = hashset![
            KeyId::from_str("40e35e8f6003ab90d104710cf88901edab931597401f91c19eeb366060ab3d53")
                .unwrap(),
            KeyId::from_str("01892c662c8cd79fab20edec21de1dcb8b75d9353103face7fe086ff5c0098e4")
                .unwrap(),
            KeyId::from_str("4750eaf6878740780d6f97b12dbad079fb012bec88c78de2c380add56d3f51db")
                .unwrap(),
        ];
        let role_def = RoleDefinition::new(3, keyids).unwrap();
        let jsn = json!({
            "threshold": 3,
            "keyids": [
                "01892c662c8cd79fab20edec21de1dcb8b75d9353103face7fe086ff5c0098e4",
                "40e35e8f6003ab90d104710cf88901edab931597401f91c19eeb366060ab3d53",
                "4750eaf6878740780d6f97b12dbad079fb012bec88c78de2c380add56d3f51db",
            ],
        });
        let encoded = serde_json::to_value(&role_def).unwrap();
        assert_eq!(encoded, jsn);
        let decoded: RoleDefinition<RootMetadata> = serde_json::from_value(encoded).unwrap();
        assert_eq!(decoded, role_def);
    }

    #[test]
    fn serde_invalid_role_definitions() {
        let jsn = json!({
            "threshold": 0,
            "keyids": [
                "01892c662c8cd79fab20edec21de1dcb8b75d9353103face7fe086ff5c0098e4",
                "4750eaf6878740780d6f97b12dbad079fb012bec88c78de2c380add56d3f51db",
            ],
        });
        assert!(serde_json::from_value::<RoleDefinition<RootMetadata>>(jsn).is_err());

        let jsn = json!({
            "threshold": -1,
            "keyids": [
                "01892c662c8cd79fab20edec21de1dcb8b75d9353103face7fe086ff5c0098e4",
                "4750eaf6878740780d6f97b12dbad079fb012bec88c78de2c380add56d3f51db",
            ],
        });
        assert!(serde_json::from_value::<RoleDefinition<RootMetadata>>(jsn).is_err());
    }

    #[test]
    fn serde_root_metadata() {
        let root_key = Ed25519PrivateKey::from_pkcs8(ED25519_1_PK8).unwrap();
        let snapshot_key = Ed25519PrivateKey::from_pkcs8(ED25519_2_PK8).unwrap();
        let targets_key = Ed25519PrivateKey::from_pkcs8(ED25519_3_PK8).unwrap();
        let timestamp_key = Ed25519PrivateKey::from_pkcs8(ED25519_4_PK8).unwrap();

        let root = RootMetadataBuilder::new()
            .expires(Utc.ymd(2017, 1, 1).and_hms(0, 0, 0))
            .root_key(root_key.public().clone())
            .snapshot_key(snapshot_key.public().clone())
            .targets_key(targets_key.public().clone())
            .timestamp_key(timestamp_key.public().clone())
            .build()
            .unwrap();

        let jsn = json!({
            "_type": "root",
            "spec_version": "1.0",
            "version": 1,
            "expires": "2017-01-01T00:00:00Z",
            "consistent_snapshot": true,
            "keys": {
                "09557ed63f91b5b95917d46f66c63ea79bdaef1b008ba823808bca849f1d18a1": {
                    "keytype": "ed25519",
                    "scheme": "ed25519",
                    "keyid_hash_algorithms": ["sha256", "sha512"],
                    "keyval": {
                        "public": "1410ae3053aa70bbfa98428a879d64d3002a3578f7dfaaeb1cb0764e860f7e0b",
                    },
                },
                "40e35e8f6003ab90d104710cf88901edab931597401f91c19eeb366060ab3d53": {
                    "keytype": "ed25519",
                    "scheme": "ed25519",
                    "keyid_hash_algorithms": ["sha256", "sha512"],
                    "keyval": {
                        "public": "166376c90a7f717d027056272f361c252fb050bed1a067ff2089a0302fbab73d",
                    },
                },
                "a9f3ebc9b138762563a9c27b6edd439959e559709babd123e8d449ba2c18c61a": {
                    "keytype": "ed25519",
                    "scheme": "ed25519",
                    "keyid_hash_algorithms": ["sha256", "sha512"],
                    "keyval": {
                        "public": "eb8ac26b5c9ef0279e3be3e82262a93bce16fe58ee422500d38caf461c65a3b6",
                    },
                },
                "fd7b7741686fa44903f1e4b61d7db869939f402b4acedc044767922c7d309983": {
                    "keytype": "ed25519",
                    "scheme": "ed25519",
                    "keyid_hash_algorithms": ["sha256", "sha512"],
                    "keyval": {
                        "public": "68d9ecb387371005a8eb8e60105305c34356a8fcd859d7fef3cc228bf2b2b3b2",
                    },
                }
            },
            "roles": {
                "root": {
                    "threshold": 1,
                    "keyids": ["a9f3ebc9b138762563a9c27b6edd439959e559709babd123e8d449ba2c18c61a"],
                },
                "snapshot": {
                    "threshold": 1,
                    "keyids": ["fd7b7741686fa44903f1e4b61d7db869939f402b4acedc044767922c7d309983"],
                },
                "targets": {
                    "threshold": 1,
                    "keyids": ["40e35e8f6003ab90d104710cf88901edab931597401f91c19eeb366060ab3d53"],
                },
                "timestamp": {
                    "threshold": 1,
                    "keyids": ["09557ed63f91b5b95917d46f66c63ea79bdaef1b008ba823808bca849f1d18a1"],
                },
            },
        });

        let encoded = serde_json::to_value(&root).unwrap();
        assert_eq!(encoded, jsn);
        let decoded: RootMetadata = serde_json::from_value(encoded).unwrap();
        assert_eq!(decoded, root);
    }

    fn jsn_root_metadata_without_keyid_hash_algos() -> serde_json::Value {
        json!({
            "_type": "root",
            "spec_version": "1.0",
            "version": 1,
            "expires": "2017-01-01T00:00:00Z",
            "consistent_snapshot": false,
            "keys": {
                "12435b260b6172bd750aeb102f54a347c56b109e0524ab1f144593c07af66356": {
                    "keytype": "ed25519",
                    "scheme": "ed25519",
                    "keyval": {
                        "public": "68d9ecb387371005a8eb8e60105305c34356a8fcd859d7fef3cc228bf2b2b3b2",
                    },
                },
                "3af6b427c05274532231760f39d81212fdf8ac1a9f8fddf12722623ccec02fec": {
                    "keytype": "ed25519",
                    "scheme": "ed25519",
                    "keyval": {
                        "public": "1410ae3053aa70bbfa98428a879d64d3002a3578f7dfaaeb1cb0764e860f7e0b",
                    },
                },
                "b9c336828063cf4fe5348e9fe2d86827c7b3104a76b1f4484a56bbef1ef08cfb": {
                    "keytype": "ed25519",
                    "scheme": "ed25519",
                    "keyval": {
                        "public": "166376c90a7f717d027056272f361c252fb050bed1a067ff2089a0302fbab73d",
                    },
                },
                "e0294a3f17cc8563c3ed5fceb3bd8d3f6bfeeaca499b5c9572729ae015566554": {
                    "keytype": "ed25519",
                    "scheme": "ed25519",
                    "keyval": {
                        "public": "eb8ac26b5c9ef0279e3be3e82262a93bce16fe58ee422500d38caf461c65a3b6",
                    },
                }
            },
            "roles": {
                "root": {
                    "threshold": 1,
                    "keyids": ["e0294a3f17cc8563c3ed5fceb3bd8d3f6bfeeaca499b5c9572729ae015566554"],
                },
                "snapshot": {
                    "threshold": 1,
                    "keyids": ["12435b260b6172bd750aeb102f54a347c56b109e0524ab1f144593c07af66356"],
                },
                "targets": {
                    "threshold": 1,
                    "keyids": ["b9c336828063cf4fe5348e9fe2d86827c7b3104a76b1f4484a56bbef1ef08cfb"],
                },
                "timestamp": {
                    "threshold": 1,
                    "keyids": ["3af6b427c05274532231760f39d81212fdf8ac1a9f8fddf12722623ccec02fec"],
                },
            },
        })
    }

    #[test]
    fn de_ser_root_metadata_without_keyid_hash_algorithms() {
        let jsn = jsn_root_metadata_without_keyid_hash_algos();
        let decoded: RootMetadata = serde_json::from_value(jsn.clone()).unwrap();
        let encoded = serde_json::to_value(decoded).unwrap();

        assert_eq!(jsn, encoded);
    }

    #[test]
    fn de_ser_root_metadata_wrong_key_id() {
        let jsn = jsn_root_metadata_without_keyid_hash_algos();
        let mut jsn_str = str::from_utf8(&Pouf1::canonicalize(&jsn).unwrap())
            .unwrap()
            .to_owned();
        // Replace the key id to something else.
        jsn_str = jsn_str.replace(
            "12435b260b6172bd750aeb102f54a347c56b109e0524ab1f144593c07af66356",
            "00435b260b6172bd750aeb102f54a347c56b109e0524ab1f144593c07af66356",
        );
        let decoded: RootMetadata = serde_json::from_str(&jsn_str).unwrap();
        assert_eq!(3, decoded.keys.len());
    }

    #[test]
    fn sign_and_verify_root_metadata() {
        let jsn = jsn_root_metadata_without_keyid_hash_algos();
        let root_key = Ed25519PrivateKey::from_pkcs8(ED25519_1_PK8).unwrap();
        let decoded: RootMetadata = serde_json::from_value(jsn).unwrap();

        let signed: SignedMetadata<crate::pouf::pouf1::Pouf1, _> =
            SignedMetadata::new(&decoded, &root_key).unwrap();
        let raw_root = signed.to_raw().unwrap();

        assert_matches!(
            verify_signatures(
                &MetadataPath::root(),
                &raw_root,
                1,
                &[root_key.public().clone()]
            ),
            Ok(_)
        );
    }

    #[test]
    fn verify_signed_serialized_root_metadata() {
        let jsn = json!({
            "signatures": [{
                "keyid": "a9f3ebc9b138762563a9c27b6edd439959e559709babd123e8d449ba2c18c61a",
                "sig": "c4ba838e0d3f783716393a4d691f568f840733ff488bb79ac68287e97e0b31d63fcef392dbc978e878c2103ba231905af634cc651d6f0e63a35782d051ac6e00"
            }],
            "signed": jsn_root_metadata_without_keyid_hash_algos()
        });
        let root_key = Ed25519PrivateKey::from_pkcs8(ED25519_1_PK8).unwrap();
        let decoded: SignedMetadata<crate::pouf::pouf1::Pouf1, RootMetadata> =
            serde_json::from_value(jsn).unwrap();
        let raw_root = decoded.to_raw().unwrap();

        assert_matches!(
            verify_signatures(
                &MetadataPath::root(),
                &raw_root,
                1,
                &[root_key.public().clone()]
            ),
            Ok(_)
        );
    }

    #[test]
    fn verify_signed_serialized_root_metadata_with_duplicate_sig() {
        let jsn = json!({
            "signatures": [{
                "keyid": "a9f3ebc9b138762563a9c27b6edd439959e559709babd123e8d449ba2c18c61a",
                "sig": "c4ba838e0d3f783716393a4d691f568f840733ff488bb79ac68287e97e0b31d63fcef392dbc978e878c2103ba231905af634cc651d6f0e63a35782d051ac6e00"
            },
            {
                "keyid": "a9f3ebc9b138762563a9c27b6edd439959e559709babd123e8d449ba2c18c61a",
                "sig": "c4ba838e0d3f783716393a4d691f568f840733ff488bb79ac68287e97e0b31d63fcef392dbc978e878c2103ba231905af634cc651d6f0e63a35782d051ac6e00"
            }],
            "signed": jsn_root_metadata_without_keyid_hash_algos()
        });
        let root_key = Ed25519PrivateKey::from_pkcs8(ED25519_1_PK8).unwrap();
        let decoded: SignedMetadata<crate::pouf::pouf1::Pouf1, RootMetadata> =
            serde_json::from_value(jsn).unwrap();
        let raw_root = decoded.to_raw().unwrap();
        assert_matches!(
            verify_signatures(&MetadataPath::root(), &raw_root, 2, &[root_key.public().clone()]),
            Err(Error::MetadataMissingSignatures {
                role,
                number_of_valid_signatures: 1,
                threshold: 2,
            })
            if role == MetadataPath::root()
        );
        assert_matches!(
            verify_signatures(
                &MetadataPath::root(),
                &raw_root,
                1,
                &[root_key.public().clone()]
            ),
            Ok(_)
        );
    }

    fn verify_signature_with_unknown_fields<M>(mut metadata: serde_json::Value)
    where
        M: Metadata,
    {
        let key = Ed25519PrivateKey::from_pkcs8(ED25519_1_PK8).unwrap();
        let public_keys = vec![key.public().clone()];

        let mut standard = SignedMetadataBuilder::<Pouf1, M>::from_raw_metadata(metadata.clone())
            .unwrap()
            .sign(&key)
            .unwrap()
            .build()
            .to_raw()
            .unwrap()
            .parse_untrusted()
            .unwrap();

        metadata.as_object_mut().unwrap().insert(
            "custom".into(),
            json!({
                "metadata": ["please", "sign", "me"],
                "this-too": 42,
            }),
        );
        let mut custom = SignedMetadataBuilder::<Pouf1, M>::from_raw_metadata(metadata)
            .unwrap()
            .sign(&key)
            .unwrap()
            .build()
            .to_raw()
            .unwrap()
            .parse_untrusted()
            .unwrap();

        // Ensure the signatures are valid as-is.
        assert_matches!(
            verify_signatures(
                &M::ROLE.into(),
                &standard.to_raw().unwrap(),
                1,
                &public_keys
            ),
            Ok(_)
        );
        assert_matches!(
            verify_signatures(
                &M::ROLE.into(),
                &custom.to_raw().unwrap(),
                1,
                std::iter::once(key.public())
            ),
            Ok(_)
        );

        // But not if the metadata was signed with custom fields and they are now missing or
        // unexpected new fields appear.
        std::mem::swap(&mut standard.metadata, &mut custom.metadata);
        assert_matches!(
            verify_signatures(
                &M::ROLE.into(),
                &standard.to_raw().unwrap(),
                1,
                std::iter::once(key.public())
            ),
            Err(Error::MetadataMissingSignatures { role, number_of_valid_signatures: 0, threshold: 1 })
            if role == M::ROLE.into()
        );
        assert_matches!(
            verify_signatures(
                &M::ROLE.into(),
                &custom.to_raw().unwrap(),
                1,
                std::iter::once(key.public())
            ),
            Err(Error::MetadataMissingSignatures { role, number_of_valid_signatures: 0, threshold: 1 })
            if role == M::ROLE.into()
        );
    }

    #[test]
    fn unknown_fields_included_in_root_metadata_signature() {
        verify_signature_with_unknown_fields::<RootMetadata>(
            jsn_root_metadata_without_keyid_hash_algos(),
        );
    }

    #[test]
    fn unknown_fields_included_in_timestamp_metadata_signature() {
        verify_signature_with_unknown_fields::<TimestampMetadata>(make_timestamp());
    }

    #[test]
    fn unknown_fields_included_in_snapshot_metadata_signature() {
        verify_signature_with_unknown_fields::<SnapshotMetadata>(make_snapshot());
    }

    #[test]
    fn unknown_fields_included_in_targets_metadata_signature() {
        verify_signature_with_unknown_fields::<TargetsMetadata>(make_targets());
    }

    #[test]
    fn serde_timestamp_metadata() {
        let description = MetadataDescription::new(
            1,
            Some(100),
            hashmap! { HashAlgorithm::Sha256 => HashValue::new(vec![]) },
        )
        .unwrap();

        let timestamp = TimestampMetadataBuilder::from_metadata_description(description)
            .expires(Utc.ymd(2017, 1, 1).and_hms(0, 0, 0))
            .build()
            .unwrap();

        let jsn = json!({
            "_type": "timestamp",
            "spec_version": "1.0",
            "version": 1,
            "expires": "2017-01-01T00:00:00Z",
            "meta": {
                "snapshot.json": {
                    "version": 1,
                    "length": 100,
                    "hashes": {
                        "sha256": "",
                    },
                },
            }
        });

        let encoded = serde_json::to_value(&timestamp).unwrap();
        assert_eq!(encoded, jsn);
        let decoded: TimestampMetadata = serde_json::from_value(encoded).unwrap();
        assert_eq!(decoded, timestamp);
    }

    // Deserialize timestamp metadata with optional length and hashes
    #[test]
    fn serde_timestamp_metadata_without_length_and_hashes() {
        let description = MetadataDescription::new(1, None, HashMap::new()).unwrap();

        let timestamp = TimestampMetadataBuilder::from_metadata_description(description)
            .expires(Utc.ymd(2017, 1, 1).and_hms(0, 0, 0))
            .build()
            .unwrap();

        let jsn = json!({
            "_type": "timestamp",
            "spec_version": "1.0",
            "version": 1,
            "expires": "2017-01-01T00:00:00Z",
            "meta": {
                "snapshot.json": {
                    "version": 1
                },
            }
        });

        let encoded = serde_json::to_value(&timestamp).unwrap();
        assert_eq!(encoded, jsn);
        let decoded: TimestampMetadata = serde_json::from_value(encoded).unwrap();
        assert_eq!(decoded, timestamp);
    }

    #[test]
    fn serde_timestamp_metadata_missing_snapshot() {
        let jsn = json!({
            "_type": "timestamp",
            "spec_version": "1.0",
            "version": 1,
            "expires": "2017-01-01T00:00:00Z",
            "meta": {}
        });

        assert_matches!(
            serde_json::from_value::<TimestampMetadata>(jsn),
            Err(ref err) if err.to_string() == "missing field `snapshot.json`"
        );
    }

    #[test]
    fn serde_timestamp_metadata_extra_metadata() {
        let jsn = json!({
            "_type": "timestamp",
            "spec_version": "1.0",
            "version": 1,
            "expires": "2017-01-01T00:00:00Z",
            "meta": {
                "snapshot.json": {
                    "version": 1,
                    "length": 100,
                    "hashes": {
                        "sha256": "",
                    },
                },
                "targets.json": {
                    "version": 1,
                    "length": 100,
                    "hashes": {
                        "sha256": "",
                    },
                },
            }
        });

        assert_matches!(
            serde_json::from_value::<TimestampMetadata>(jsn),
            Err(ref err) if err.to_string() ==
            "unknown field `targets.json`, expected `snapshot.json`"
        );
    }

    #[test]
    fn serde_snapshot_metadata() {
        let snapshot = SnapshotMetadataBuilder::new()
            .expires(Utc.ymd(2017, 1, 1).and_hms(0, 0, 0))
            .insert_metadata_description(
                MetadataPath::new("targets").unwrap(),
                MetadataDescription::new(
                    1,
                    Some(100),
                    hashmap! { HashAlgorithm::Sha256 => HashValue::new(vec![]) },
                )
                .unwrap(),
            )
            .build()
            .unwrap();

        let jsn = json!({
            "_type": "snapshot",
            "spec_version": "1.0",
            "version": 1,
            "expires": "2017-01-01T00:00:00Z",
            "meta": {
                "targets.json": {
                    "version": 1,
                    "length": 100,
                    "hashes": {
                        "sha256": "",
                    },
                },
            },
        });

        let encoded = serde_json::to_value(&snapshot).unwrap();
        assert_eq!(encoded, jsn);
        let decoded: SnapshotMetadata = serde_json::from_value(encoded).unwrap();
        assert_eq!(decoded, snapshot);
    }

    // Deserialize snapshot metadata with optional length and hashes
    #[test]
    fn serde_snapshot_optional_length_and_hashes() {
        let snapshot = SnapshotMetadataBuilder::new()
            .expires(Utc.ymd(2017, 1, 1).and_hms(0, 0, 0))
            .insert_metadata_description(
                MetadataPath::new("targets").unwrap(),
                MetadataDescription::new(1, None, HashMap::new()).unwrap(),
            )
            .build()
            .unwrap();

        let jsn = json!({
            "_type": "snapshot",
            "spec_version": "1.0",
            "version": 1,
            "expires": "2017-01-01T00:00:00Z",
            "meta": {
                "targets.json": {
                    "version": 1,
                },
            },
        });

        let encoded = serde_json::to_value(&snapshot).unwrap();
        assert_eq!(encoded, jsn);
        let decoded: SnapshotMetadata = serde_json::from_value(encoded).unwrap();
        assert_eq!(decoded, snapshot);
    }

    #[test]
    fn serde_targets_metadata() {
        block_on(async {
            let targets = TargetsMetadataBuilder::new()
                .expires(Utc.ymd(2017, 1, 1).and_hms(0, 0, 0))
                .insert_target_from_slice(
                    TargetPath::new("insert-target-from-slice").unwrap(),
                    &b"foo"[..],
                    &[HashAlgorithm::Sha256],
                )
                .unwrap()
                .insert_target_from_reader(
                    TargetPath::new("insert-target-from-reader").unwrap(),
                    &b"foo"[..],
                    &[HashAlgorithm::Sha256],
                )
                .await
                .unwrap()
                .insert_target_description(
                    TargetPath::new("insert-target-description-from-slice-with-custom").unwrap(),
                    TargetDescription::from_slice_with_custom(
                        &b"foo"[..],
                        &[HashAlgorithm::Sha256],
                        HashMap::new(),
                    )
                    .unwrap(),
                )
                .insert_target_description(
                    TargetPath::new("insert-target-description-from-reader-with-custom").unwrap(),
                    TargetDescription::from_reader_with_custom(
                        &b"foo"[..],
                        &[HashAlgorithm::Sha256],
                        hashmap! {
                            "foo".into() => 1.into(),
                            "bar".into() => "baz".into(),
                        },
                    )
                    .await
                    .unwrap(),
                )
                .build()
                .unwrap();

            let jsn = json!({
                "_type": "targets",
                "spec_version": "1.0",
                "version": 1,
                "expires": "2017-01-01T00:00:00Z",
                "targets": {
                    "insert-target-from-slice": {
                        "length": 3,
                        "hashes": {
                            "sha256": "2c26b46b68ffc68ff99b453c1d30413413422d706483\
                                bfa0f98a5e886266e7ae",
                        },
                    },
                    "insert-target-description-from-slice-with-custom": {
                        "length": 3,
                        "hashes": {
                            "sha256": "2c26b46b68ffc68ff99b453c1d30413413422d706483\
                                bfa0f98a5e886266e7ae",
                        },
                    },
                    "insert-target-from-reader": {
                        "length": 3,
                        "hashes": {
                            "sha256": "2c26b46b68ffc68ff99b453c1d30413413422d706483\
                                bfa0f98a5e886266e7ae",
                        },
                    },
                    "insert-target-description-from-reader-with-custom": {
                        "length": 3,
                        "hashes": {
                            "sha256": "2c26b46b68ffc68ff99b453c1d30413413422d706483\
                                bfa0f98a5e886266e7ae",
                        },
                        "custom": {
                            "foo": 1,
                            "bar": "baz",
                        },
                    },
                },
            });

            let encoded = serde_json::to_value(&targets).unwrap();
            assert_eq!(encoded, jsn);
            let decoded: TargetsMetadata = serde_json::from_value(encoded).unwrap();
            assert_eq!(decoded, targets);
        })
    }

    #[test]
    fn serde_targets_with_delegations_metadata() {
        let key = Ed25519PrivateKey::from_pkcs8(ED25519_1_PK8).unwrap();
        let delegations = Delegations::new(
            hashmap! { key.public().key_id().clone() => key.public().clone() },
            vec![Delegation::new(
                MetadataPath::new("foo/bar").unwrap(),
                false,
                1,
                hashset!(key.public().key_id().clone()),
                hashset!(TargetPath::new("baz/quux").unwrap()),
            )
            .unwrap()],
        )
        .unwrap();

        let targets = TargetsMetadataBuilder::new()
            .expires(Utc.ymd(2017, 1, 1).and_hms(0, 0, 0))
            .delegations(delegations)
            .build()
            .unwrap();

        let jsn = json!({
            "_type": "targets",
            "spec_version": "1.0",
            "version": 1,
            "expires": "2017-01-01T00:00:00Z",
            "targets": {},
            "delegations": {
                "keys": {
                    "a9f3ebc9b138762563a9c27b6edd439959e559709babd123e8d449ba2c18c61a": {
                        "keytype": "ed25519",
                        "scheme": "ed25519",
                        "keyid_hash_algorithms": ["sha256", "sha512"],
                        "keyval": {
                            "public": "eb8ac26b5c9ef0279e3be3e82262a93bce16fe58\
                                ee422500d38caf461c65a3b6",
                        }
                    },
                },
                "roles": [
                    {
                        "name": "foo/bar",
                        "terminating": false,
                        "threshold": 1,
                        "keyids": ["a9f3ebc9b138762563a9c27b6edd439959e559709babd123e8d449ba2c18c61a"],
                        "paths": ["baz/quux"],
                    },
                ],
            }
        });

        let encoded = serde_json::to_value(&targets).unwrap();
        assert_eq!(encoded, jsn);
        let decoded: TargetsMetadata = serde_json::from_value(encoded).unwrap();
        assert_eq!(decoded, targets);
    }

    #[test]
    fn serde_signed_metadata() {
        let snapshot = SnapshotMetadataBuilder::new()
            .expires(Utc.ymd(2017, 1, 1).and_hms(0, 0, 0))
            .insert_metadata_description(
                MetadataPath::new("targets").unwrap(),
                MetadataDescription::new(
                    1,
                    Some(100),
                    hashmap! { HashAlgorithm::Sha256 => HashValue::new(vec![]) },
                )
                .unwrap(),
            )
            .build()
            .unwrap();

        let key = Ed25519PrivateKey::from_pkcs8(ED25519_1_PK8).unwrap();

        let signed = SignedMetadata::<Pouf1, _>::new(&snapshot, &key).unwrap();

        let jsn = json!({
            "signatures": [
                {
                    "keyid": "a9f3ebc9b138762563a9c27b6edd439959e559709babd123e8d449ba2c18c61a",
                    "sig": "ea48ddc7b3ea614b394e508eb8722100f94ff1a4e3aac3af09d\
                        a0dada4f878431e8ac26160833405ec239924dfe62edf605fee8294\
                        c49b4acade55c76e817602",
                }
            ],
            "signed": {
                "_type": "snapshot",
                "spec_version": "1.0",
                "version": 1,
                "expires": "2017-01-01T00:00:00Z",
                "meta": {
                    "targets.json": {
                        "version": 1,
                        "length": 100,
                        "hashes": {
                            "sha256": "",
                        },
                    },
                },
            },
        });

        let encoded = serde_json::to_value(&signed).unwrap();
        assert_eq!(encoded, jsn, "{:#?} != {:#?}", encoded, jsn);
        let decoded: SignedMetadata<Pouf1, SnapshotMetadata> =
            serde_json::from_value(encoded).unwrap();
        assert_eq!(decoded, signed);
    }

    ///////////////////////////////////////////////////////////////////////////////////////////////
    //
    // Here there be test cases about what metadata is allowed to be parsed wherein we do all sorts
    // of naughty things and make sure the parsers puke appropriately.
    //                                   ______________
    //                             ,===:'.,            `-._
    //                                  `:.`---.__         `-._
    //                                    `:.     `--.         `.
    //                                      \.        `.         `.
    //                              (,,(,    \.         `.   ____,-`.,
    //                           (,'     `/   \.   ,--.___`.'
    //                       ,  ,'  ,--.  `,   \.;'         `
    //                        `(o, /    \  :    \;
    //                          |,,'    /  /    //
    //                          j;;    /  ,' ,-//.    ,---.      ,
    //                          \;'   /  ,' /  _  \  /  _  \   ,'/
    //                                \   `'  / \  `'  / \  `.' /
    //                                 `.___,'   `.__,'   `.__,'
    //
    ///////////////////////////////////////////////////////////////////////////////////////////////

    // TODO test for mismatched ed25519/rsa keys/schemes

    fn make_root() -> serde_json::Value {
        let root_key = Ed25519PrivateKey::from_pkcs8(ED25519_1_PK8).unwrap();
        let snapshot_key = Ed25519PrivateKey::from_pkcs8(ED25519_2_PK8).unwrap();
        let targets_key = Ed25519PrivateKey::from_pkcs8(ED25519_3_PK8).unwrap();
        let timestamp_key = Ed25519PrivateKey::from_pkcs8(ED25519_4_PK8).unwrap();

        let root = RootMetadataBuilder::new()
            .expires(Utc.ymd(2038, 1, 1).and_hms(0, 0, 0))
            .root_key(root_key.public().clone())
            .snapshot_key(snapshot_key.public().clone())
            .targets_key(targets_key.public().clone())
            .timestamp_key(timestamp_key.public().clone())
            .build()
            .unwrap();

        serde_json::to_value(&root).unwrap()
    }

    fn make_snapshot() -> serde_json::Value {
        let snapshot = SnapshotMetadataBuilder::new()
            .expires(Utc.ymd(2038, 1, 1).and_hms(0, 0, 0))
            .build()
            .unwrap();

        serde_json::to_value(&snapshot).unwrap()
    }

    fn make_timestamp() -> serde_json::Value {
        let description =
            MetadataDescription::from_slice(&[][..], 1, &[HashAlgorithm::Sha256]).unwrap();

        let timestamp = TimestampMetadataBuilder::from_metadata_description(description)
            .expires(Utc.ymd(2017, 1, 1).and_hms(0, 0, 0))
            .build()
            .unwrap();

        serde_json::to_value(&timestamp).unwrap()
    }

    fn make_targets() -> serde_json::Value {
        let targets = TargetsMetadata::new(
            1,
            Utc.ymd(2038, 1, 1).and_hms(0, 0, 0),
            hashmap!(),
            Delegations::default(),
        )
        .unwrap();

        serde_json::to_value(&targets).unwrap()
    }

    fn make_delegations() -> serde_json::Value {
        let key = Ed25519PrivateKey::from_pkcs8(ED25519_1_PK8)
            .unwrap()
            .public()
            .clone();
        let delegations = Delegations::new(
            hashmap! { key.key_id().clone() => key.clone() },
            vec![Delegation::new(
                MetadataPath::new("foo").unwrap(),
                false,
                1,
                hashset!(key.key_id().clone()),
                hashset!(TargetPath::new("bar").unwrap()),
            )
            .unwrap()],
        )
        .unwrap();

        serde_json::to_value(&delegations).unwrap()
    }

    fn make_delegation() -> serde_json::Value {
        let key = Ed25519PrivateKey::from_pkcs8(ED25519_1_PK8)
            .unwrap()
            .public()
            .clone();
        let delegation = Delegation::new(
            MetadataPath::new("foo").unwrap(),
            false,
            1,
            hashset!(key.key_id().clone()),
            hashset!(TargetPath::new("bar").unwrap()),
        )
        .unwrap();

        serde_json::to_value(&delegation).unwrap()
    }

    fn set_version(value: &mut serde_json::Value, version: i64) {
        match value.as_object_mut() {
            Some(obj) => {
                let _ = obj.insert("version".into(), json!(version));
            }
            None => panic!(),
        }
    }

    // Refuse to deserialize root metadata if the version is not > 0
    #[test]
    fn deserialize_json_root_illegal_version() {
        let mut root_json = make_root();
        set_version(&mut root_json, 0);
        assert!(serde_json::from_value::<RootMetadata>(root_json.clone()).is_err());

        let mut root_json = make_root();
        set_version(&mut root_json, -1);
        assert!(serde_json::from_value::<RootMetadata>(root_json).is_err());
    }

    // Refuse to deserialize root metadata if it contains duplicate keys
    #[test]
    fn deserialize_json_root_duplicate_keys() {
        let root_json = r#"{
            "_type": "root",
            "spec_version": "1.0",
            "version": 1,
            "expires": "2017-01-01T00:00:00Z",
            "consistent_snapshot": false,
            "keys": {
                "09557ed63f91b5b95917d46f66c63ea79bdaef1b008ba823808bca849f1d18a1": {
                    "keytype": "ed25519",
                    "scheme": "ed25519",
                    "keyval": {
                        "public": "1410ae3053aa70bbfa98428a879d64d3002a3578f7dfaaeb1cb0764e860f7e0b"
                    }
                },
                "09557ed63f91b5b95917d46f66c63ea79bdaef1b008ba823808bca849f1d18a1": {
                    "keytype": "ed25519",
                    "scheme": "ed25519",
                    "keyval": {
                        "public": "166376c90a7f717d027056272f361c252fb050bed1a067ff2089a0302fbab73d"
                    }
                }
            },
            "roles": {
                "root": {
                    "threshold": 1,
                    "keyids": ["09557ed63f91b5b95917d46f66c63ea79bdaef1b008ba823808bca849f1d18a1"]
                },
                "snapshot": {
                    "threshold": 1,
                    "keyids": ["09557ed63f91b5b95917d46f66c63ea79bdaef1b008ba823808bca849f1d18a1"]
                },
                "targets": {
                    "threshold": 1,
                    "keyids": ["09557ed63f91b5b95917d46f66c63ea79bdaef1b008ba823808bca849f1d18a1"]
                },
                "timestamp": {
                    "threshold": 1,
                    "keyids": ["09557ed63f91b5b95917d46f66c63ea79bdaef1b008ba823808bca849f1d18a1"]
                }
            }
        }"#;
        match serde_json::from_str::<RootMetadata>(root_json) {
            Err(ref err) if err.is_data() => {
                assert!(
                    err.to_string().starts_with("Cannot have duplicate keys"),
                    "unexpected err: {:?}",
                    err
                );
            }
            result => panic!("unexpected result: {:?}", result),
        }
    }

    fn set_threshold(value: &mut serde_json::Value, threshold: i32) {
        match value.as_object_mut() {
            Some(obj) => {
                let _ = obj.insert("threshold".into(), json!(threshold));
            }
            None => panic!(),
        }
    }

    // Refuse to deserialize role definitions with illegal thresholds
    #[test]
    fn deserialize_json_role_definition_illegal_threshold() {
        let role_def = RoleDefinition::<RootMetadata>::new(
            1,
            hashset![Ed25519PrivateKey::from_pkcs8(ED25519_1_PK8)
                .unwrap()
                .public()
                .key_id()
                .clone()],
        )
        .unwrap();

        let mut jsn = serde_json::to_value(&role_def).unwrap();
        set_threshold(&mut jsn, 0);
        assert!(serde_json::from_value::<RoleDefinition<RootMetadata>>(jsn).is_err());

        let mut jsn = serde_json::to_value(&role_def).unwrap();
        set_threshold(&mut jsn, -1);
        assert!(serde_json::from_value::<RoleDefinition<RootMetadata>>(jsn).is_err());

        let role_def = RoleDefinition::<RootMetadata>::new(
            2,
            hashset![
                Ed25519PrivateKey::from_pkcs8(ED25519_1_PK8)
                    .unwrap()
                    .public()
                    .key_id()
                    .clone(),
                Ed25519PrivateKey::from_pkcs8(ED25519_2_PK8)
                    .unwrap()
                    .public()
                    .key_id()
                    .clone(),
            ],
        )
        .unwrap();

        let mut jsn = serde_json::to_value(&role_def).unwrap();
        set_threshold(&mut jsn, 3);
        assert!(serde_json::from_value::<RoleDefinition<RootMetadata>>(jsn).is_err());
    }

    // Refuse to deserialize root metadata with wrong type field
    #[test]
    fn deserialize_json_root_bad_type() {
        let mut root = make_root();
        let _ = root
            .as_object_mut()
            .unwrap()
            .insert("_type".into(), json!("snapshot"));
        assert!(serde_json::from_value::<RootMetadata>(root).is_err());
    }

    // Refuse to deserialize root metadata with unknown spec version
    #[test]
    fn deserialize_json_root_bad_spec_version() {
        let mut root = make_root();
        let _ = root
            .as_object_mut()
            .unwrap()
            .insert("spec_version".into(), json!("0"));
        assert!(serde_json::from_value::<RootMetadata>(root).is_err());
    }

    // Refuse to deserialize role definitions with duplicated key ids
    #[test]
    fn deserialize_json_role_definition_duplicate_key_ids() {
        let key_id = Ed25519PrivateKey::from_pkcs8(ED25519_1_PK8)
            .unwrap()
            .public()
            .key_id()
            .clone();
        let role_def = RoleDefinition::<RootMetadata>::new(1, hashset![key_id.clone()]).unwrap();
        let mut jsn = serde_json::to_value(&role_def).unwrap();

        match jsn.as_object_mut() {
            Some(obj) => match obj.get_mut("keyids").unwrap().as_array_mut() {
                Some(arr) => arr.push(json!(key_id)),
                None => panic!(),
            },
            None => panic!(),
        }

        assert!(serde_json::from_value::<RoleDefinition<RootMetadata>>(jsn).is_err());
    }

    // Refuse to deserialize snapshot metadata with illegal versions
    #[test]
    fn deserialize_json_snapshot_illegal_version() {
        let mut snapshot = make_snapshot();
        set_version(&mut snapshot, 0);
        assert!(serde_json::from_value::<SnapshotMetadata>(snapshot).is_err());

        let mut snapshot = make_snapshot();
        set_version(&mut snapshot, -1);
        assert!(serde_json::from_value::<SnapshotMetadata>(snapshot).is_err());
    }

    // Refuse to deserialize snapshot metadata with wrong type field
    #[test]
    fn deserialize_json_snapshot_bad_type() {
        let mut snapshot = make_snapshot();
        let _ = snapshot
            .as_object_mut()
            .unwrap()
            .insert("_type".into(), json!("root"));
        assert!(serde_json::from_value::<SnapshotMetadata>(snapshot).is_err());
    }

    // Refuse to deserialize snapshot metadata with unknown spec version
    #[test]
    fn deserialize_json_snapshot_spec_version() {
        let mut snapshot = make_snapshot();
        let _ = snapshot
            .as_object_mut()
            .unwrap()
            .insert("spec_version".into(), json!("0"));
        assert!(serde_json::from_value::<SnapshotMetadata>(snapshot).is_err());
    }

    // Refuse to deserialize snapshot metadata if it contains duplicate metadata
    #[test]
    fn deserialize_json_snapshot_duplicate_metadata() {
        let snapshot_json = r#"{
            "_type": "snapshot",
            "spec_version": "1.0",
            "version": 1,
            "expires": "2017-01-01T00:00:00Z",
            "meta": {
                "targets.json": {
                    "version": 1,
                    "length": 100,
                    "hashes": {
                        "sha256": ""
                    }
                },
                "targets.json": {
                    "version": 1,
                    "length": 100,
                    "hashes": {
                        "sha256": ""
                    }
                }
            }
        }"#;
        match serde_json::from_str::<SnapshotMetadata>(snapshot_json) {
            Err(ref err) if err.is_data() => {}
            result => panic!("unexpected result: {:?}", result),
        }
    }

    // Refuse to deserialize timestamp metadata with illegal versions
    #[test]
    fn deserialize_json_timestamp_illegal_version() {
        let mut timestamp = make_timestamp();
        set_version(&mut timestamp, 0);
        assert!(serde_json::from_value::<TimestampMetadata>(timestamp).is_err());

        let mut timestamp = make_timestamp();
        set_version(&mut timestamp, -1);
        assert!(serde_json::from_value::<TimestampMetadata>(timestamp).is_err());
    }

    // Refuse to deserialize timestamp metadata with wrong type field
    #[test]
    fn deserialize_json_timestamp_bad_type() {
        let mut timestamp = make_timestamp();
        let _ = timestamp
            .as_object_mut()
            .unwrap()
            .insert("_type".into(), json!("root"));
        assert!(serde_json::from_value::<TimestampMetadata>(timestamp).is_err());
    }

    // Refuse to deserialize timestamp metadata with unknown spec version
    #[test]
    fn deserialize_json_timestamp_bad_spec_version() {
        let mut timestamp = make_timestamp();
        let _ = timestamp
            .as_object_mut()
            .unwrap()
            .insert("spec_version".into(), json!("0"));
        assert!(serde_json::from_value::<TimestampMetadata>(timestamp).is_err());
    }

    // Refuse to deserialize timestamp metadata if it contains duplicate metadata
    #[test]
    fn deserialize_json_timestamp_duplicate_metadata() {
        let timestamp_json = r#"{
            "_type": "timestamp",
            "spec_version": "1.0",
            "version": 1,
            "expires": "2017-01-01T00:00:00Z",
            "meta": {
                "snapshot.json": {
                    "version": 1,
                    "length": 100,
                    "hashes": {
                        "sha256": ""
                    }
                },
                "snapshot.json": {
                    "version": 1,
                    "length": 100,
                    "hashes": {
                        "sha256": ""
                    }
                }
            }
        }"#;
        match serde_json::from_str::<TimestampMetadata>(timestamp_json) {
            Err(ref err) if err.is_data() => {}
            result => panic!("unexpected result: {:?}", result),
        }
    }

    // Refuse to deserialize targets metadata with illegal versions
    #[test]
    fn deserialize_json_targets_illegal_version() {
        let mut targets = make_targets();
        set_version(&mut targets, 0);
        assert!(serde_json::from_value::<TargetsMetadata>(targets).is_err());

        let mut targets = make_targets();
        set_version(&mut targets, -1);
        assert!(serde_json::from_value::<TargetsMetadata>(targets).is_err());
    }

    // Refuse to deserialize targets metadata with wrong type field
    #[test]
    fn deserialize_json_targets_bad_type() {
        let mut targets = make_targets();
        let _ = targets
            .as_object_mut()
            .unwrap()
            .insert("_type".into(), json!("root"));
        assert!(serde_json::from_value::<TargetsMetadata>(targets).is_err());
    }

    // Refuse to deserialize targets metadata with unknown spec version
    #[test]
    fn deserialize_json_targets_bad_spec_version() {
        let mut targets = make_targets();
        let _ = targets
            .as_object_mut()
            .unwrap()
            .insert("spec_version".into(), json!("0"));
        assert!(serde_json::from_value::<TargetsMetadata>(targets).is_err());
    }

    // Refuse to deserialize delegations with duplicated roles
    #[test]
    fn deserialize_json_delegations_duplicated_roles() {
        let mut delegations = make_delegations();
        let dupe = delegations
            .as_object()
            .unwrap()
            .get("roles")
            .unwrap()
            .as_array()
            .unwrap()[0]
            .clone();
        delegations
            .as_object_mut()
            .unwrap()
            .get_mut("roles")
            .unwrap()
            .as_array_mut()
            .unwrap()
            .push(dupe);
        assert!(serde_json::from_value::<Delegations>(delegations).is_err());
    }

    // Refuse to deserialize a delegation with insufficient threshold
    #[test]
    fn deserialize_json_delegation_bad_threshold() {
        let mut delegation = make_delegation();
        set_threshold(&mut delegation, 0);
        assert!(serde_json::from_value::<Delegation>(delegation).is_err());

        let mut delegation = make_delegation();
        set_threshold(&mut delegation, 2);
        assert!(serde_json::from_value::<Delegation>(delegation).is_err());
    }

    // Refuse to deserialize a delegation with duplicate key IDs
    #[test]
    fn deserialize_json_delegation_duplicate_key_ids() {
        let mut delegation = make_delegation();
        let dupe = delegation
            .as_object()
            .unwrap()
            .get("keyids")
            .unwrap()
            .as_array()
            .unwrap()[0]
            .clone();
        delegation
            .as_object_mut()
            .unwrap()
            .get_mut("keyids")
            .unwrap()
            .as_array_mut()
            .unwrap()
            .push(dupe);
        assert!(serde_json::from_value::<Delegation>(delegation).is_err());
    }

    // Refuse to deserialize a delegation with duplicate paths
    #[test]
    fn deserialize_json_delegation_duplicate_paths() {
        let mut delegation = make_delegation();
        let dupe = delegation
            .as_object()
            .unwrap()
            .get("paths")
            .unwrap()
            .as_array()
            .unwrap()[0]
            .clone();
        delegation
            .as_object_mut()
            .unwrap()
            .get_mut("paths")
            .unwrap()
            .as_array_mut()
            .unwrap()
            .push(dupe);
        assert!(serde_json::from_value::<Delegation>(delegation).is_err());
    }

    // Refuse to deserialize a Delegations struct with duplicate keys
    #[test]
    fn deserialize_json_delegations_duplicate_keys() {
        let delegations_json = r#"{
            "keys": {
                "qfrfBrkB4lBBSDEBlZgaTGS_SrE6UfmON9kP4i3dJFY=": {
                    "public_key": "MCwwBwYDK2VwBQADIQDrisJrXJ7wJ5474-giYqk7zhb-WO5CJQDTjK9GHGWjtg==",
                    "scheme": "ed25519",
                    "type": "ed25519"
                },
                "qfrfBrkB4lBBSDEBlZgaTGS_SrE6UfmON9kP4i3dJFY=": {
                    "public_key": "MCwwBwYDK2VwBQADIQDrisJrXJ7wJ5474-giYqk7zhb-WO5CJQDTjK9GHGWjtg==",
                    "scheme": "ed25519",
                    "type": "ed25519"
                }
            },
            "roles": [
            {
                "keyids": [
                    "qfrfBrkB4lBBSDEBlZgaTGS_SrE6UfmON9kP4i3dJFY="
                ],
                "paths": [
                    "bar"
                ],
                "role": "foo",
                "terminating": false,
                "threshold": 1
            }
            ]
        }"#;
        match serde_json::from_str::<Delegations>(delegations_json) {
            Err(ref err) if err.is_data() => {}
            result => panic!("unexpected result: {:?}", result),
        }
    }
}
