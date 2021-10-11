//! Clients for high level interactions with TUF repositories.
//!
//! # Example
//!
//! ```no_run
//! # use futures_executor::block_on;
//! # use hyper::client::Client as HttpClient;
//! # use std::path::PathBuf;
//! # use std::str::FromStr;
//! # use tuf::{Result, Tuf};
//! # use tuf::crypto::PublicKey;
//! # use tuf::client::{Client, Config};
//! # use tuf::metadata::{RootMetadata, Role, MetadataPath, MetadataVersion};
//! # use tuf::interchange::Json;
//! # use tuf::repository::{FileSystemRepository, HttpRepositoryBuilder};
//! #
//! # const PUBLIC_KEY: &'static [u8] = include_bytes!("../tests/ed25519/ed25519-1.pub");
//! #
//! # fn load_root_public_keys() -> Vec<PublicKey> {
//! #      vec![PublicKey::from_ed25519(PUBLIC_KEY).unwrap()]
//! # }
//! #
//! # fn main() -> Result<()> {
//! # block_on(async {
//! let root_public_keys = load_root_public_keys();
//! let local = FileSystemRepository::<Json>::new(PathBuf::from("~/.rustup"))?;
//!
//! let remote = HttpRepositoryBuilder::new_with_uri(
//!     "https://static.rust-lang.org/".parse::<http::Uri>().unwrap(),
//!     HttpClient::new(),
//! )
//! .user_agent("rustup/1.4.0")
//! .build();
//!
//! let mut client = Client::with_trusted_root_keys(
//!     Config::default(),
//!     &MetadataVersion::Number(1),
//!     1,
//!     &root_public_keys,
//!     local,
//!     remote,
//! ).await?;
//!
//! let _ = client.update().await?;
//! # Ok(())
//! # })
//! # }
//! ```

use chrono::offset::Utc;
use futures_io::{AsyncRead, AsyncWrite};
use futures_util::io::copy;
use log::{error, warn};
use std::collections::HashMap;
use std::future::Future;
use std::pin::Pin;

use crate::crypto::{self, HashAlgorithm, HashValue, PublicKey};
use crate::error::Error;
use crate::interchange::DataInterchange;
use crate::metadata::{
    Metadata, MetadataPath, MetadataVersion, RawSignedMetadata, Role, RootMetadata,
    SnapshotMetadata, TargetDescription, TargetPath, TargetsMetadata, TimestampMetadata,
    VirtualTargetPath,
};
use crate::repository::{Repository, RepositoryProvider, RepositoryStorage};
use crate::tuf::Tuf;
use crate::verify::Verified;
use crate::Result;

/// Translates real paths (where a file is stored) into virtual paths (how it is addressed in TUF)
/// and back.
///
/// Implementations must obey the following identities for all possible inputs.
///
/// ```
/// # use tuf::client::{PathTranslator, DefaultTranslator};
/// # use tuf::metadata::{VirtualTargetPath, TargetPath};
/// let path = TargetPath::new("foo".into()).unwrap();
/// let virt = VirtualTargetPath::new("foo".into()).unwrap();
/// let translator = DefaultTranslator::new();
/// assert_eq!(path,
///            translator.virtual_to_real(&translator.real_to_virtual(&path).unwrap()).unwrap());
/// assert_eq!(virt,
///            translator.real_to_virtual(&translator.virtual_to_real(&virt).unwrap()).unwrap());
/// ```
pub trait PathTranslator {
    /// Convert a real path into a virtual path.
    fn real_to_virtual(&self, path: &TargetPath) -> Result<VirtualTargetPath>;

    /// Convert a virtual path into a real path.
    fn virtual_to_real(&self, path: &VirtualTargetPath) -> Result<TargetPath>;
}

/// A `PathTranslator` that does nothing.
#[derive(Clone, Debug, Default)]
pub struct DefaultTranslator;

impl DefaultTranslator {
    /// Create a new `DefaultTranslator`.
    pub fn new() -> Self {
        DefaultTranslator
    }
}

impl PathTranslator for DefaultTranslator {
    fn real_to_virtual(&self, path: &TargetPath) -> Result<VirtualTargetPath> {
        VirtualTargetPath::new(path.value().into())
    }

    fn virtual_to_real(&self, path: &VirtualTargetPath) -> Result<TargetPath> {
        TargetPath::new(path.value().into())
    }
}

/// A client that interacts with TUF repositories.
#[derive(Debug)]
pub struct Client<D, L, R, T>
where
    D: DataInterchange + Sync,
    L: RepositoryProvider<D> + RepositoryStorage<D>,
    R: RepositoryProvider<D>,
    T: PathTranslator,
{
    tuf: Tuf<D>,
    config: Config<T>,
    local: Repository<L, D>,
    remote: Repository<R, D>,
}

impl<D, L, R, T> Client<D, L, R, T>
where
    D: DataInterchange + Sync,
    L: RepositoryProvider<D> + RepositoryStorage<D>,
    R: RepositoryProvider<D>,
    T: PathTranslator,
{
    /// Create a new TUF client. It will attempt to load the latest root metadata from the local
    /// repo and use it as the initial trusted root metadata, or it will return an error if it
    /// cannot do so.
    ///
    /// **WARNING**: This is trust-on-first-use (TOFU) and offers weaker security guarantees than
    /// the related methods [`Client::with_trusted_root`], [`Client::with_trusted_root_keys`].
    ///
    /// # Examples
    ///
    /// ```
    /// # use chrono::offset::{Utc, TimeZone};
    /// # use futures_executor::block_on;
    /// # use tuf::{
    /// #     Error,
    /// #     interchange::Json,
    /// #     client::{Client, Config},
    /// #     crypto::{Ed25519PrivateKey, PrivateKey, SignatureScheme},
    /// #     metadata::{MetadataPath, MetadataVersion, Role, RootMetadataBuilder},
    /// #     repository::{EphemeralRepository, RepositoryStorage},
    /// # };
    /// # fn main() -> Result<(), Error> {
    /// # block_on(async {
    /// # let private_key = Ed25519PrivateKey::from_pkcs8(
    /// #     &Ed25519PrivateKey::pkcs8()?,
    /// # )?;
    /// # let public_key = private_key.public().clone();
    /// let local = EphemeralRepository::<Json>::new();
    /// let remote = EphemeralRepository::<Json>::new();
    ///
    /// let root_version = 1;
    /// let root = RootMetadataBuilder::new()
    ///     .version(root_version)
    ///     .expires(Utc.ymd(2038, 1, 1).and_hms(0, 0, 0))
    ///     .root_key(public_key.clone())
    ///     .snapshot_key(public_key.clone())
    ///     .targets_key(public_key.clone())
    ///     .timestamp_key(public_key.clone())
    ///     .signed::<Json>(&private_key)?;
    ///
    /// let root_path = MetadataPath::from_role(&Role::Root);
    /// let root_version = MetadataVersion::Number(root_version);
    ///
    /// local.store_metadata(
    ///     &root_path,
    ///     &root_version,
    ///     &mut root.to_raw().unwrap().as_bytes()
    /// ).await?;
    ///
    /// let client = Client::with_trusted_local(
    ///     Config::default(),
    ///     local,
    ///     remote,
    /// ).await?;
    /// # Ok(())
    /// # })
    /// # }
    /// ```
    pub async fn with_trusted_local(config: Config<T>, local: L, remote: R) -> Result<Self> {
        let (local, remote) = (Repository::new(local), Repository::new(remote));
        let root_path = MetadataPath::from_role(&Role::Root);

        // FIXME should this be MetadataVersion::None so we bootstrap with the latest version?
        let root_version = MetadataVersion::Number(1);

        let raw_root: RawSignedMetadata<_, RootMetadata> = local
            .fetch_metadata(&root_path, &root_version, config.max_root_length, None)
            .await?;

        let tuf = Tuf::from_trusted_root(&raw_root)?;

        Self::new(config, tuf, local, remote).await
    }

    /// Create a new TUF client. It will trust this initial root metadata.
    ///
    /// # Examples
    ///
    /// ```
    /// # use chrono::offset::{Utc, TimeZone};
    /// # use futures_executor::block_on;
    /// # use tuf::{
    /// #     Error,
    /// #     interchange::Json,
    /// #     client::{Client, Config},
    /// #     crypto::{Ed25519PrivateKey, KeyType, PrivateKey, SignatureScheme},
    /// #     metadata::{MetadataPath, MetadataVersion, Role, RootMetadataBuilder},
    /// #     repository::{EphemeralRepository},
    /// # };
    /// # fn main() -> Result<(), Error> {
    /// # block_on(async {
    /// # let private_key = Ed25519PrivateKey::from_pkcs8(
    /// #     &Ed25519PrivateKey::pkcs8()?,
    /// # )?;
    /// # let public_key = private_key.public().clone();
    /// let local = EphemeralRepository::<Json>::new();
    /// let remote = EphemeralRepository::<Json>::new();
    ///
    /// let root_version = 1;
    /// let root_threshold = 1;
    /// let raw_root = RootMetadataBuilder::new()
    ///     .version(root_version)
    ///     .expires(Utc.ymd(2038, 1, 1).and_hms(0, 0, 0))
    ///     .root_key(public_key.clone())
    ///     .root_threshold(root_threshold)
    ///     .snapshot_key(public_key.clone())
    ///     .targets_key(public_key.clone())
    ///     .timestamp_key(public_key.clone())
    ///     .signed::<Json>(&private_key)
    ///     .unwrap()
    ///     .to_raw()
    ///     .unwrap();
    ///
    /// let client = Client::with_trusted_root(
    ///     Config::default(),
    ///     &raw_root,
    ///     local,
    ///     remote,
    /// ).await?;
    /// # Ok(())
    /// # })
    /// # }
    /// ```
    pub async fn with_trusted_root(
        config: Config<T>,
        trusted_root: &RawSignedMetadata<D, RootMetadata>,
        local: L,
        remote: R,
    ) -> Result<Self> {
        let (local, remote) = (Repository::new(local), Repository::new(remote));
        let tuf = Tuf::from_trusted_root(trusted_root)?;

        Self::new(config, tuf, local, remote).await
    }

    /// Create a new TUF client. It will attempt to load initial root metadata from the local and remote
    /// repositories using the provided keys to pin the verification.
    ///
    /// # Examples
    ///
    /// ```
    /// # use chrono::offset::{Utc, TimeZone};
    /// # use futures_executor::block_on;
    /// # use std::iter::once;
    /// # use tuf::{
    /// #     Error,
    /// #     interchange::Json,
    /// #     client::{Client, Config},
    /// #     crypto::{Ed25519PrivateKey, KeyType, PrivateKey, SignatureScheme},
    /// #     metadata::{MetadataPath, MetadataVersion, Role, RootMetadataBuilder},
    /// #     repository::{EphemeralRepository, RepositoryStorage},
    /// # };
    /// # fn main() -> Result<(), Error> {
    /// # block_on(async {
    /// # let private_key = Ed25519PrivateKey::from_pkcs8(
    /// #     &Ed25519PrivateKey::pkcs8()?,
    /// # )?;
    /// # let public_key = private_key.public().clone();
    /// let local = EphemeralRepository::<Json>::new();
    /// let remote = EphemeralRepository::<Json>::new();
    ///
    /// let root_version = 1;
    /// let root_threshold = 1;
    /// let root = RootMetadataBuilder::new()
    ///     .version(root_version)
    ///     .expires(Utc.ymd(2038, 1, 1).and_hms(0, 0, 0))
    ///     .root_key(public_key.clone())
    ///     .root_threshold(root_threshold)
    ///     .snapshot_key(public_key.clone())
    ///     .targets_key(public_key.clone())
    ///     .timestamp_key(public_key.clone())
    ///     .signed::<Json>(&private_key)?;
    ///
    /// let root_path = MetadataPath::from_role(&Role::Root);
    /// let root_version = MetadataVersion::Number(root_version);
    ///
    /// remote.store_metadata(
    ///     &root_path,
    ///     &root_version,
    ///     &mut root.to_raw().unwrap().as_bytes()
    /// ).await?;
    ///
    /// let client = Client::with_trusted_root_keys(
    ///     Config::default(),
    ///     &root_version,
    ///     root_threshold,
    ///     once(&public_key),
    ///     local,
    ///     remote,
    /// ).await?;
    /// # Ok(())
    /// # })
    /// # }
    /// ```
    pub async fn with_trusted_root_keys<'a, I>(
        config: Config<T>,
        root_version: &MetadataVersion,
        root_threshold: u32,
        trusted_root_keys: I,
        local: L,
        remote: R,
    ) -> Result<Self>
    where
        I: IntoIterator<Item = &'a PublicKey>,
    {
        let (local, remote) = (Repository::new(local), Repository::new(remote));

        let root_path = MetadataPath::from_role(&Role::Root);
        let (fetched, raw_root) = fetch_metadata_from_local_or_else_remote(
            &root_path,
            root_version,
            config.max_root_length,
            None,
            &local,
            &remote,
        )
        .await?;

        let tuf = Tuf::from_root_with_trusted_keys(&raw_root, root_threshold, trusted_root_keys)?;

        // FIXME(#253) verify the trusted root version matches the provided version.
        let root_version = MetadataVersion::Number(tuf.trusted_root().version());

        // Only store the metadata after we have validated it.
        if fetched {
            // NOTE(#301): The spec only states that the unversioned root metadata needs to be
            // written to non-volatile storage. This enables a method like
            // `Client::with_trusted_local` to initialize trust with the latest root version.
            // However, this doesn't work well when trust is established with an externally
            // provided root, such as with `Clietn::with_trusted_root` or
            // `Client::with_trusted_root_keys`. In those cases, it's possible those initial roots
            // could be multiple versions behind the latest cached root metadata. So we'd most
            // likely never use the locally cached `root.json`.
            //
            // Instead, as an extension to the spec, we'll write the `$VERSION.root.json` metadata
            // to the local store. This will eventually enable us to initialize metadata from the
            // local store (see #301).
            local
                .store_metadata(&root_path, &root_version, &raw_root)
                .await?;

            // FIXME: should we also store the root as `MetadataVersion::None`?
        }

        Self::new(config, tuf, local, remote).await
    }

    async fn new(
        config: Config<T>,
        mut tuf: Tuf<D>,
        local: Repository<L, D>,
        remote: Repository<R, D>,
    ) -> Result<Self> {
        let res = async {
            let _r = Self::update_root_with_repos(&config, &mut tuf, None, &local).await?;
            let _ts = Self::update_timestamp_with_repos(&config, &mut tuf, None, &local).await?;
            let _sn = Self::update_snapshot_with_repos(&mut tuf, None, &local, false).await?;
            let _ta = Self::update_targets_with_repos(&mut tuf, None, &local, false).await?;

            Ok(())
        }
        .await;

        match res {
            Ok(()) | Err(Error::NotFound) => {}
            Err(err) => {
                warn!("error loading local metadata: : {}", err);
            }
        }

        Ok(Client {
            tuf,
            config,
            local,
            remote,
        })
    }

    /// Update TUF metadata from the remote repository.
    ///
    /// Returns `true` if an update occurred and `false` otherwise.
    pub async fn update(&mut self) -> Result<bool> {
        let r = self.update_root().await?;
        let ts = self.update_timestamp().await?;
        let sn = self.update_snapshot().await?;
        let ta = self.update_targets().await?;

        Ok(r || ts || sn || ta)
    }

    /// Returns the current trusted root version.
    pub fn root_version(&self) -> u32 {
        self.tuf.trusted_root().version()
    }

    /// Returns the current trusted timestamp version.
    pub fn timestamp_version(&self) -> Option<u32> {
        Some(self.tuf.trusted_timestamp()?.version())
    }

    /// Returns the current trusted snapshot version.
    pub fn snapshot_version(&self) -> Option<u32> {
        Some(self.tuf.trusted_snapshot()?.version())
    }

    /// Returns the current trusted targets version.
    pub fn targets_version(&self) -> Option<u32> {
        Some(self.tuf.trusted_targets()?.version())
    }

    /// Returns the current trusted delegations version for a given role.
    pub fn delegations_version(&self, role: &MetadataPath) -> Option<u32> {
        Some(self.tuf.trusted_delegations().get(role)?.version())
    }

    /// Returns the current trusted root.
    pub fn trusted_root(&self) -> &Verified<RootMetadata> {
        self.tuf.trusted_root()
    }

    /// Returns the current trusted timestamp.
    pub fn trusted_timestamp(&self) -> Option<&Verified<TimestampMetadata>> {
        self.tuf.trusted_timestamp()
    }

    /// Returns the current trusted snapshot.
    pub fn trusted_snapshot(&self) -> Option<&Verified<SnapshotMetadata>> {
        self.tuf.trusted_snapshot()
    }

    /// Returns the current trusted targets.
    pub fn trusted_targets(&self) -> Option<&Verified<TargetsMetadata>> {
        self.tuf.trusted_targets()
    }

    /// Returns the current trusted delegations.
    pub fn trusted_delegations(&self) -> &HashMap<MetadataPath, Verified<TargetsMetadata>> {
        self.tuf.trusted_delegations()
    }

    /// Update TUF root metadata from the remote repository.
    ///
    /// Returns `true` if an update occurred and `false` otherwise.
    pub async fn update_root(&mut self) -> Result<bool> {
        Self::update_root_with_repos(&self.config, &mut self.tuf, Some(&self.local), &self.remote)
            .await
    }

    async fn update_root_with_repos<Remote>(
        config: &Config<T>,
        tuf: &mut Tuf<D>,
        local: Option<&Repository<L, D>>,
        remote: &Repository<Remote, D>,
    ) -> Result<bool>
    where
        Remote: RepositoryProvider<D>,
    {
        let root_path = MetadataPath::from_role(&Role::Root);

        let mut updated = false;

        loop {
            /////////////////////////////////////////
            // TUF-1.0.9 §5.1.2:
            //
            //     Try downloading version N+1 of the root metadata file, up to some W number of
            //     bytes (because the size is unknown). The value for W is set by the authors of
            //     the application using TUF. For example, W may be tens of kilobytes. The filename
            //     used to download the root metadata file is of the fixed form
            //     VERSION_NUMBER.FILENAME.EXT (e.g., 42.root.json). If this file is not available,
            //     or we have downloaded more than Y number of root metadata files (because the
            //     exact number is as yet unknown), then go to step 5.1.9. The value for Y is set
            //     by the authors of the application using TUF. For example, Y may be 2^10.

            // FIXME(#306) We do not have an upper bound on the number of root metadata we'll
            // fetch. This means that an attacker that's stolen the root keys could cause a client
            // to fall into an infinite loop (but if an attacker has stolen the root keys, the
            // client probably has worse problems to worry about).

            let next_version = MetadataVersion::Number(tuf.trusted_root().version() + 1);
            let res = remote
                .fetch_metadata(&root_path, &next_version, config.max_root_length, None)
                .await;

            let raw_signed_root = match res {
                Ok(raw_signed_root) => raw_signed_root,
                Err(Error::NotFound) => {
                    break;
                }
                Err(err) => {
                    return Err(err);
                }
            };

            updated = true;

            tuf.update_root(&raw_signed_root)?;

            /////////////////////////////////////////
            // TUF-1.0.9 §5.1.7:
            //
            //     Persist root metadata. The client MUST write the file to non-volatile storage as
            //     FILENAME.EXT (e.g. root.json).

            if let Some(local) = local {
                local
                    .store_metadata(&root_path, &MetadataVersion::None, &raw_signed_root)
                    .await?;

                // NOTE(#301): See the comment in `Client::with_trusted_root_keys`.
                local
                    .store_metadata(&root_path, &next_version, &raw_signed_root)
                    .await?;
            }

            /////////////////////////////////////////
            // TUF-1.0.9 §5.1.8:
            //
            //     Repeat steps 5.1.1 to 5.1.8.
        }

        /////////////////////////////////////////
        // TUF-1.0.9 §5.1.9:
        //
        //     Check for a freeze attack. The latest known time MUST be lower than the expiration
        //     timestamp in the trusted root metadata file (version N). If the trusted root
        //     metadata file has expired, abort the update cycle, report the potential freeze
        //     attack. On the next update cycle, begin at step 5.0 and version N of the root
        //     metadata file.

        // TODO: Consider moving the root metadata expiration check into `tuf::Tuf`, since that's
        // where we check timestamp/snapshot/targets/delegations for expiration.
        if tuf.trusted_root().expires() <= &Utc::now() {
            error!("Root metadata expired, potential freeze attack");
            return Err(Error::ExpiredMetadata(Role::Root));
        }

        /////////////////////////////////////////
        // TUF-1.0.5 §5.1.10:
        //
        //     Set whether consistent snapshots are used as per the trusted root metadata file (see
        //     Section 4.3).

        Ok(updated)
    }

    /// Returns `true` if an update occurred and `false` otherwise.
    async fn update_timestamp(&mut self) -> Result<bool> {
        Self::update_timestamp_with_repos(
            &self.config,
            &mut self.tuf,
            Some(&self.local),
            &self.remote,
        )
        .await
    }

    async fn update_timestamp_with_repos<Remote>(
        config: &Config<T>,
        tuf: &mut Tuf<D>,
        local: Option<&Repository<L, D>>,
        remote: &Repository<Remote, D>,
    ) -> Result<bool>
    where
        Remote: RepositoryProvider<D>,
    {
        let timestamp_path = MetadataPath::from_role(&Role::Timestamp);

        /////////////////////////////////////////
        // TUF-1.0.9 §5.2:
        //
        //     Download the timestamp metadata file, up to X number of bytes (because the size is
        //     unknown). The value for X is set by the authors of the application using TUF. For
        //     example, X may be tens of kilobytes. The filename used to download the timestamp
        //     metadata file is of the fixed form FILENAME.EXT (e.g., timestamp.json).

        let raw_signed_timestamp = remote
            .fetch_metadata(
                &timestamp_path,
                &MetadataVersion::None,
                config.max_timestamp_length,
                None,
            )
            .await?;

        if tuf.update_timestamp(&raw_signed_timestamp)?.is_some() {
            /////////////////////////////////////////
            // TUF-1.0.9 §5.2.4:
            //
            //     Persist timestamp metadata. The client MUST write the file to non-volatile
            //     storage as FILENAME.EXT (e.g. timestamp.json).

            if let Some(local) = local {
                local
                    .store_metadata(
                        &timestamp_path,
                        &MetadataVersion::None,
                        &raw_signed_timestamp,
                    )
                    .await?;
            }

            Ok(true)
        } else {
            Ok(false)
        }
    }

    /// Returns `true` if an update occurred and `false` otherwise.
    async fn update_snapshot(&mut self) -> Result<bool> {
        let consistent_snapshot = self.tuf.trusted_root().consistent_snapshot();
        Self::update_snapshot_with_repos(
            &mut self.tuf,
            Some(&self.local),
            &self.remote,
            consistent_snapshot,
        )
        .await
    }

    async fn update_snapshot_with_repos<Remote>(
        tuf: &mut Tuf<D>,
        local: Option<&Repository<L, D>>,
        remote: &Repository<Remote, D>,
        consistent_snapshots: bool,
    ) -> Result<bool>
    where
        Remote: RepositoryProvider<D>,
    {
        // 5.3.1 Check against timestamp metadata. The hashes and version number listed in the
        // timestamp metadata. If hashes and version do not match, discard the new snapshot
        // metadata, abort the update cycle, and report the failure.
        let snapshot_description = match tuf.trusted_timestamp() {
            Some(ts) => Ok(ts.snapshot()),
            None => Err(Error::MissingMetadata(Role::Timestamp)),
        }?
        .clone();

        if snapshot_description.version()
            <= tuf.trusted_snapshot().map(|s| s.version()).unwrap_or(0)
        {
            return Ok(false);
        }

        let (alg, value) = crypto::hash_preference(snapshot_description.hashes())?;

        let version = if consistent_snapshots {
            MetadataVersion::Number(snapshot_description.version())
        } else {
            MetadataVersion::None
        };

        let snapshot_path = MetadataPath::from_role(&Role::Snapshot);
        let snapshot_length = Some(snapshot_description.length());

        let raw_signed_snapshot = remote
            .fetch_metadata(
                &snapshot_path,
                &version,
                snapshot_length,
                Some((alg, value.clone())),
            )
            .await?;

        if tuf.update_snapshot(&raw_signed_snapshot)? {
            /////////////////////////////////////////
            // TUF-1.0.9 §5.3.5:
            //
            //     Persist snapshot metadata. The client MUST write the file to non-volatile
            //     storage as FILENAME.EXT (e.g. snapshot.json).

            if let Some(local) = local {
                local
                    .store_metadata(&snapshot_path, &MetadataVersion::None, &raw_signed_snapshot)
                    .await?;
            }

            Ok(true)
        } else {
            Ok(false)
        }
    }

    /// Returns `true` if an update occurred and `false` otherwise.
    async fn update_targets(&mut self) -> Result<bool> {
        let consistent_snapshot = self.tuf.trusted_root().consistent_snapshot();
        Self::update_targets_with_repos(
            &mut self.tuf,
            Some(&self.local),
            &self.remote,
            consistent_snapshot,
        )
        .await
    }

    async fn update_targets_with_repos<Remote>(
        tuf: &mut Tuf<D>,
        local: Option<&Repository<L, D>>,
        remote: &Repository<Remote, D>,
        consistent_snapshot: bool,
    ) -> Result<bool>
    where
        Remote: RepositoryProvider<D>,
    {
        let targets_description = match tuf.trusted_snapshot() {
            Some(sn) => match sn.meta().get(&MetadataPath::from_role(&Role::Targets)) {
                Some(d) => Ok(d),
                None => Err(Error::VerificationFailure(
                    "Snapshot metadata did not contain a description of the \
                     current targets metadata."
                        .into(),
                )),
            },
            None => Err(Error::MissingMetadata(Role::Snapshot)),
        }?
        .clone();

        if targets_description.version() <= tuf.trusted_targets().map(|t| t.version()).unwrap_or(0)
        {
            return Ok(false);
        }

        let (alg, value) = crypto::hash_preference(targets_description.hashes())?;

        let version = if consistent_snapshot {
            MetadataVersion::Number(targets_description.version())
        } else {
            MetadataVersion::None
        };

        let targets_path = MetadataPath::from_role(&Role::Targets);
        let targets_length = Some(targets_description.length());

        let raw_signed_targets = remote
            .fetch_metadata(
                &targets_path,
                &version,
                targets_length,
                Some((alg, value.clone())),
            )
            .await?;

        if tuf.update_targets(&raw_signed_targets)? {
            /////////////////////////////////////////
            // TUF-1.0.9 §5.4.4:
            //
            //     Persist targets metadata. The client MUST write the file to non-volatile storage
            //     as FILENAME.EXT (e.g. targets.json).

            if let Some(local) = local {
                local
                    .store_metadata(&targets_path, &MetadataVersion::None, &raw_signed_targets)
                    .await?;
            }

            Ok(true)
        } else {
            Ok(false)
        }
    }

    /// Fetch a target from the remote repo and write it to the local repo.
    pub async fn fetch_target<'a>(&'a mut self, target: &'a TargetPath) -> Result<()> {
        let mut read = self._fetch_target(target).await?;
        self.local.store_target(&mut read, target).await
    }

    /// Fetch a target from the remote repo and write it to the provided writer.
    ///
    /// It is **critical** that none of the bytes written to the `write` are used until this future
    /// returns `Ok`, as the hash of the target is not verified until all bytes are read from the
    /// repository.
    pub async fn fetch_target_to_writer<'a, W>(
        &'a mut self,
        target: &'a TargetPath,
        mut write: W,
    ) -> Result<()>
    where
        W: AsyncWrite + Send + Unpin,
    {
        let read = self._fetch_target(&target).await?;
        copy(read, &mut write).await?;
        Ok(())
    }

    /// Fetch a target description from the remote repo and return it.
    pub async fn fetch_target_description<'a>(
        &'a mut self,
        target: &'a TargetPath,
    ) -> Result<TargetDescription> {
        let virt = self.config.path_translator.real_to_virtual(target)?;

        let snapshot = self
            .tuf
            .trusted_snapshot()
            .ok_or_else(|| Error::MissingMetadata(Role::Snapshot))?
            .clone();
        let (_, target_description) = self
            .lookup_target_description(false, 0, &virt, &snapshot, None)
            .await;
        target_description
    }

    // TODO this should check the local repo first
    async fn _fetch_target<'a>(
        &'a mut self,
        target: &'a TargetPath,
    ) -> Result<impl AsyncRead + Send + Unpin> {
        let target_description = self.fetch_target_description(target).await?;

        // According to TUF section 5.5.2, when consistent snapshot is enabled, target files should
        // be found at `$HASH.FILENAME.EXT`. Otherwise it is stored at `FILENAME.EXT`.
        if self.tuf.trusted_root().consistent_snapshot() {
            let (_, value) = crypto::hash_preference(target_description.hashes())?;
            let target = target.with_hash_prefix(value)?;
            self.remote.fetch_target(&target, &target_description).await
        } else {
            self.remote.fetch_target(target, &target_description).await
        }
    }

    async fn lookup_target_description<'a>(
        &'a mut self,
        default_terminate: bool,
        current_depth: u32,
        target: &'a VirtualTargetPath,
        snapshot: &'a SnapshotMetadata,
        targets: Option<(&'a Verified<TargetsMetadata>, MetadataPath)>,
    ) -> (bool, Result<TargetDescription>) {
        if current_depth > self.config.max_delegation_depth {
            warn!(
                "Walking the delegation graph would have exceeded the configured max depth: {}",
                self.config.max_delegation_depth
            );
            return (default_terminate, Err(Error::NotFound));
        }

        // these clones are dumb, but we need immutable values and not references for update
        // tuf in the loop below
        let (targets, targets_role) = match targets {
            Some((t, role)) => (t.clone(), role),
            None => match self.tuf.trusted_targets() {
                Some(t) => (t.clone(), MetadataPath::from_role(&Role::Targets)),
                None => {
                    return (
                        default_terminate,
                        Err(Error::MissingMetadata(Role::Targets)),
                    );
                }
            },
        };

        if let Some(t) = targets.targets().get(target) {
            return (default_terminate, Ok(t.clone()));
        }

        let delegations = match targets.delegations() {
            Some(d) => d,
            None => return (default_terminate, Err(Error::NotFound)),
        };

        for delegation in delegations.roles().iter() {
            if !delegation.paths().iter().any(|p| target.is_child(p)) {
                if delegation.terminating() {
                    return (true, Err(Error::NotFound));
                } else {
                    continue;
                }
            }

            let role_meta = match snapshot.meta().get(delegation.role()) {
                Some(m) => m,
                None if !delegation.terminating() => continue,
                None => return (true, Err(Error::NotFound)),
            };

            let (alg, value) = match crypto::hash_preference(role_meta.hashes()) {
                Ok(h) => h,
                Err(e) => return (delegation.terminating(), Err(e)),
            };

            /////////////////////////////////////////
            // TUF-1.0.9 §5.4:
            //
            //     Download the top-level targets metadata file, up to either the number of bytes
            //     specified in the snapshot metadata file, or some Z number of bytes. The value
            //     for Z is set by the authors of the application using TUF. For example, Z may be
            //     tens of kilobytes. If consistent snapshots are not used (see Section 7), then
            //     the filename used to download the targets metadata file is of the fixed form
            //     FILENAME.EXT (e.g., targets.json). Otherwise, the filename is of the form
            //     VERSION_NUMBER.FILENAME.EXT (e.g., 42.targets.json), where VERSION_NUMBER is the
            //     version number of the targets metadata file listed in the snapshot metadata
            //     file.

            let version = if self.tuf.trusted_root().consistent_snapshot() {
                MetadataVersion::Number(role_meta.version())
            } else {
                MetadataVersion::None
            };

            // FIXME: Other than root, this is the only place that first tries using the local
            // metadata before failing back to the remote server. Is this logic correct?
            let role_length = Some(role_meta.length());
            let raw_signed_meta = self
                .local
                .fetch_metadata(
                    delegation.role(),
                    &MetadataVersion::None,
                    role_length,
                    Some((alg, value.clone())),
                )
                .await;

            let raw_signed_meta = match raw_signed_meta {
                Ok(m) => m,
                Err(_) => {
                    match self
                        .remote
                        .fetch_metadata(
                            delegation.role(),
                            &version,
                            role_length,
                            Some((alg, value.clone())),
                        )
                        .await
                    {
                        Ok(m) => m,
                        Err(ref e) if !delegation.terminating() => {
                            warn!("Failed to fetch metadata {:?}: {:?}", delegation.role(), e);
                            continue;
                        }
                        Err(e) => {
                            warn!("Failed to fetch metadata {:?}: {:?}", delegation.role(), e);
                            return (true, Err(e));
                        }
                    }
                }
            };

            match self
                .tuf
                .update_delegation(&targets_role, delegation.role(), &raw_signed_meta)
            {
                Ok(_) => {
                    /////////////////////////////////////////
                    // TUF-1.0.9 §5.4.4:
                    //
                    //     Persist targets metadata. The client MUST write the file to non-volatile
                    //     storage as FILENAME.EXT (e.g. targets.json).

                    match self
                        .local
                        .store_metadata(delegation.role(), &MetadataVersion::None, &raw_signed_meta)
                        .await
                    {
                        Ok(_) => (),
                        Err(e) => {
                            warn!(
                                "Error storing metadata {:?} locally: {:?}",
                                delegation.role(),
                                e
                            )
                        }
                    }

                    let meta = self
                        .tuf
                        .trusted_delegations()
                        .get(delegation.role())
                        .unwrap()
                        .clone();
                    let f: Pin<Box<dyn Future<Output = _>>> =
                        Box::pin(self.lookup_target_description(
                            delegation.terminating(),
                            current_depth + 1,
                            target,
                            snapshot,
                            Some((&meta, delegation.role().clone())),
                        ));
                    let (term, res) = f.await;

                    if term && res.is_err() {
                        return (true, res);
                    }

                    // TODO end recursion early
                }
                Err(_) if !delegation.terminating() => continue,
                Err(e) => return (true, Err(e)),
            };
        }

        (default_terminate, Err(Error::NotFound))
    }
}

/// Helper function that first tries to fetch the metadata from the local store, and if it doesn't
/// exist or does and fails to parse, try fetching it from the remote store.
async fn fetch_metadata_from_local_or_else_remote<'a, D, L, R, M>(
    path: &'a MetadataPath,
    version: &'a MetadataVersion,
    max_length: Option<usize>,
    hash_data: Option<(&'static HashAlgorithm, HashValue)>,
    local: &'a Repository<L, D>,
    remote: &'a Repository<R, D>,
) -> Result<(bool, RawSignedMetadata<D, M>)>
where
    D: DataInterchange + Sync,
    L: RepositoryProvider<D> + RepositoryStorage<D>,
    R: RepositoryProvider<D>,
    M: Metadata + 'static,
{
    match local
        .fetch_metadata(path, version, max_length, hash_data.clone())
        .await
    {
        Ok(raw_meta) => Ok((false, raw_meta)),
        Err(Error::NotFound) => {
            let raw_meta = remote
                .fetch_metadata(path, version, max_length, hash_data)
                .await?;
            Ok((true, raw_meta))
        }
        Err(err) => Err(err),
    }
}

/// Configuration for a TUF `Client`.
///
/// # Defaults
///
/// The following values are considered reasonably safe defaults, however these values may change
/// as this crate moves out of beta. If you are concered about them changing, you should use the
/// `ConfigBuilder` and set your own values.
///
/// ```
/// # use tuf::client::{Config, DefaultTranslator};
/// let config = Config::default();
/// assert_eq!(config.max_root_length(), &Some(1024 * 1024));
/// assert_eq!(config.max_timestamp_length(), &Some(32 * 1024));
/// assert_eq!(config.max_delegation_depth(), 8);
/// let _: &DefaultTranslator = config.path_translator();
/// ```
#[derive(Clone, Debug)]
pub struct Config<T>
where
    T: PathTranslator,
{
    max_root_length: Option<usize>,
    max_timestamp_length: Option<usize>,
    max_delegation_depth: u32,
    path_translator: T,
}

impl Config<DefaultTranslator> {
    /// Initialize a `ConfigBuilder` with the default values.
    pub fn build() -> ConfigBuilder<DefaultTranslator> {
        ConfigBuilder::default()
    }
}

impl<T> Config<T>
where
    T: PathTranslator,
{
    /// Return the optional maximum root metadata length.
    pub fn max_root_length(&self) -> &Option<usize> {
        &self.max_root_length
    }

    /// Return the optional maximum timestamp metadata size.
    pub fn max_timestamp_length(&self) -> &Option<usize> {
        &self.max_timestamp_length
    }

    /// The maximum number of steps used when walking the delegation graph.
    pub fn max_delegation_depth(&self) -> u32 {
        self.max_delegation_depth
    }

    /// The `PathTranslator`.
    pub fn path_translator(&self) -> &T {
        &self.path_translator
    }
}

impl Default for Config<DefaultTranslator> {
    fn default() -> Self {
        Config {
            max_root_length: Some(1024 * 1024),
            max_timestamp_length: Some(32 * 1024),
            max_delegation_depth: 8,
            path_translator: DefaultTranslator::new(),
        }
    }
}

/// Helper for building and validating a TUF client `Config`.
#[derive(Debug, PartialEq)]
pub struct ConfigBuilder<T>
where
    T: PathTranslator,
{
    max_root_length: Option<usize>,
    max_timestamp_length: Option<usize>,
    max_delegation_depth: u32,
    path_translator: T,
}

impl<T> ConfigBuilder<T>
where
    T: PathTranslator,
{
    /// Validate this builder return a `Config` if validation succeeds.
    pub fn finish(self) -> Result<Config<T>> {
        Ok(Config {
            max_root_length: self.max_root_length,
            max_timestamp_length: self.max_timestamp_length,
            max_delegation_depth: self.max_delegation_depth,
            path_translator: self.path_translator,
        })
    }

    /// Set the optional maximum download length for root metadata.
    pub fn max_root_length(mut self, max: Option<usize>) -> Self {
        self.max_root_length = max;
        self
    }

    /// Set the optional maximum download length for timestamp metadata.
    pub fn max_timestamp_length(mut self, max: Option<usize>) -> Self {
        self.max_timestamp_length = max;
        self
    }

    /// Set the maximum number of steps used when walking the delegation graph.
    pub fn max_delegation_depth(mut self, max: u32) -> Self {
        self.max_delegation_depth = max;
        self
    }

    /// Set the `PathTranslator`.
    pub fn path_translator<TT>(self, path_translator: TT) -> ConfigBuilder<TT>
    where
        TT: PathTranslator,
    {
        ConfigBuilder {
            max_root_length: self.max_root_length,
            max_timestamp_length: self.max_timestamp_length,
            max_delegation_depth: self.max_delegation_depth,
            path_translator,
        }
    }
}

impl Default for ConfigBuilder<DefaultTranslator> {
    fn default() -> ConfigBuilder<DefaultTranslator> {
        let cfg = Config::default();
        ConfigBuilder {
            max_root_length: cfg.max_root_length,
            max_timestamp_length: cfg.max_timestamp_length,
            max_delegation_depth: cfg.max_delegation_depth,
            path_translator: cfg.path_translator,
        }
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::crypto::{Ed25519PrivateKey, HashAlgorithm, PrivateKey};
    use crate::interchange::Json;
    use crate::metadata::{MetadataPath, MetadataVersion};
    use crate::repo_builder::RepoBuilder;
    use crate::repository::{EphemeralRepository, ErrorRepository, Track, TrackRepository};
    use chrono::prelude::*;
    use futures_executor::block_on;
    use lazy_static::lazy_static;
    use maplit::hashmap;
    use matches::assert_matches;
    use serde_json::json;
    use std::iter::once;

    lazy_static! {
        static ref KEYS: Vec<Ed25519PrivateKey> = {
            let keys: &[&[u8]] = &[
                include_bytes!("../tests/ed25519/ed25519-1.pk8.der"),
                include_bytes!("../tests/ed25519/ed25519-2.pk8.der"),
                include_bytes!("../tests/ed25519/ed25519-3.pk8.der"),
                include_bytes!("../tests/ed25519/ed25519-4.pk8.der"),
                include_bytes!("../tests/ed25519/ed25519-5.pk8.der"),
                include_bytes!("../tests/ed25519/ed25519-6.pk8.der"),
            ];
            keys.iter()
                .map(|b| Ed25519PrivateKey::from_pkcs8(b).unwrap())
                .collect()
        };
    }

    enum ConstructorMode {
        WithTrustedLocal,
        WithTrustedRoot,
        WithTrustedRootKeys,
    }

    #[test]
    fn client_constructors_err_with_not_found() {
        block_on(async {
            let local = EphemeralRepository::<Json>::new();
            let remote = EphemeralRepository::<Json>::new();

            let private_key =
                Ed25519PrivateKey::from_pkcs8(&Ed25519PrivateKey::pkcs8().unwrap()).unwrap();
            let public_key = private_key.public().clone();

            assert_matches!(
                Client::with_trusted_local(Config::default(), &local, &remote).await,
                Err(Error::NotFound)
            );

            assert_matches!(
                Client::with_trusted_root_keys(
                    Config::default(),
                    &MetadataVersion::Number(1),
                    1,
                    once(&public_key),
                    &local,
                    &remote,
                )
                .await,
                Err(Error::NotFound)
            );
        })
    }

    #[test]
    fn client_constructors_err_with_invalid_keys() {
        block_on(async {
            let remote = EphemeralRepository::new();

            let root_version = 1;
            let good_private_key = &KEYS[0];
            let bad_private_key = &KEYS[1];

            let _ = RepoBuilder::<_, Json>::new(&remote)
                .root_keys(vec![good_private_key])
                .with_root_builder(|bld| {
                    bld.version(root_version)
                        .expires(Utc.ymd(2038, 1, 1).and_hms(0, 0, 0))
                        .root_key(good_private_key.public().clone())
                        .snapshot_key(good_private_key.public().clone())
                        .targets_key(good_private_key.public().clone())
                        .timestamp_key(good_private_key.public().clone())
                })
                .commit()
                .await
                .unwrap();

            assert_matches!(
                Client::with_trusted_root_keys(
                    Config::default(),
                    &MetadataVersion::Number(root_version),
                    1,
                    once(bad_private_key.public()),
                    EphemeralRepository::new(),
                    &remote,
                )
                .await,
                Err(Error::VerificationFailure(_))
            );
        })
    }

    #[test]
    fn with_trusted_local_loads_metadata_from_local_repo() {
        block_on(constructors_load_metadata_from_local_repo(
            ConstructorMode::WithTrustedLocal,
        ))
    }

    #[test]
    fn with_trusted_root_loads_metadata_from_local_repo() {
        block_on(constructors_load_metadata_from_local_repo(
            ConstructorMode::WithTrustedRoot,
        ))
    }

    #[test]
    fn with_trusted_root_keys_loads_metadata_from_local_repo() {
        block_on(constructors_load_metadata_from_local_repo(
            ConstructorMode::WithTrustedRootKeys,
        ))
    }

    async fn constructors_load_metadata_from_local_repo(constructor_mode: ConstructorMode) {
        // Store an expired root in the local store.
        let local = EphemeralRepository::<Json>::new();
        let metadata1 = RepoBuilder::new(&local)
            .root_keys(vec![&KEYS[0]])
            .targets_keys(vec![&KEYS[0]])
            .snapshot_keys(vec![&KEYS[0]])
            .timestamp_keys(vec![&KEYS[0]])
            .with_root_builder(|bld| {
                bld.version(1)
                    .consistent_snapshot(true)
                    .expires(Utc.ymd(2038, 1, 1).and_hms(0, 0, 0))
                    .root_key(KEYS[0].public().clone())
                    .snapshot_key(KEYS[0].public().clone())
                    .targets_key(KEYS[0].public().clone())
                    .timestamp_key(KEYS[0].public().clone())
            })
            .targets_version(1)
            .commit()
            .await
            .unwrap();

        // Remote repo has unexpired metadata.
        let remote = EphemeralRepository::<Json>::new();
        let metadata2 = RepoBuilder::new(&remote)
            .root_keys(vec![&KEYS[0]])
            .targets_keys(vec![&KEYS[0]])
            .snapshot_keys(vec![&KEYS[0]])
            .timestamp_keys(vec![&KEYS[0]])
            .with_root_builder(|bld| {
                bld.version(2)
                    .consistent_snapshot(true)
                    .expires(Utc.ymd(2038, 1, 1).and_hms(0, 0, 0))
                    .root_key(KEYS[0].public().clone())
                    .snapshot_key(KEYS[0].public().clone())
                    .targets_key(KEYS[0].public().clone())
                    .timestamp_key(KEYS[0].public().clone())
            })
            .targets_version(2)
            .snapshot_version(2)
            .timestamp_version(2)
            .commit()
            .await
            .unwrap();

        // Now, make sure that the local metadata got version 1.
        let track_local = TrackRepository::new(&local);
        let track_remote = TrackRepository::new(&remote);

        // Make sure the client initialized metadata in the right order. Each has a slightly
        // different usage of the local repository.
        let mut client = match constructor_mode {
            ConstructorMode::WithTrustedLocal => {
                Client::with_trusted_local(Config::default(), &track_local, &track_remote)
                    .await
                    .unwrap()
            }
            ConstructorMode::WithTrustedRoot => Client::with_trusted_root(
                Config::default(),
                &metadata1.root,
                &track_local,
                &track_remote,
            )
            .await
            .unwrap(),
            ConstructorMode::WithTrustedRootKeys => Client::with_trusted_root_keys(
                Config::default(),
                &MetadataVersion::Number(1),
                1,
                once(&KEYS[0].public().clone()),
                &track_local,
                &track_remote,
            )
            .await
            .unwrap(),
        };

        assert_eq!(client.tuf.trusted_root().version(), 1);
        assert_eq!(track_remote.take_tracks(), vec![]);

        match constructor_mode {
            ConstructorMode::WithTrustedLocal => {
                assert_eq!(
                    track_local.take_tracks(),
                    vec![
                        Track::fetch_meta_found(&MetadataVersion::Number(1), &metadata1.root),
                        Track::FetchErr(
                            MetadataPath::from_role(&Role::Root),
                            MetadataVersion::Number(2)
                        ),
                        Track::fetch_meta_found(
                            &MetadataVersion::None,
                            &metadata1.timestamp.as_ref().unwrap()
                        ),
                        Track::fetch_meta_found(
                            &MetadataVersion::None,
                            &metadata1.snapshot.as_ref().unwrap()
                        ),
                        Track::fetch_meta_found(
                            &MetadataVersion::None,
                            metadata1.targets.as_ref().unwrap()
                        ),
                    ],
                );
            }
            ConstructorMode::WithTrustedRoot => {
                assert_eq!(
                    track_local.take_tracks(),
                    vec![
                        Track::FetchErr(
                            MetadataPath::from_role(&Role::Root),
                            MetadataVersion::Number(2)
                        ),
                        Track::fetch_meta_found(
                            &MetadataVersion::None,
                            &metadata1.timestamp.as_ref().unwrap()
                        ),
                        Track::fetch_meta_found(
                            &MetadataVersion::None,
                            &metadata1.snapshot.as_ref().unwrap()
                        ),
                        Track::fetch_meta_found(
                            &MetadataVersion::None,
                            metadata1.targets.as_ref().unwrap()
                        ),
                    ],
                );
            }
            ConstructorMode::WithTrustedRootKeys => {
                assert_eq!(
                    track_local.take_tracks(),
                    vec![
                        Track::fetch_meta_found(&MetadataVersion::Number(1), &metadata1.root),
                        Track::FetchErr(
                            MetadataPath::from_role(&Role::Root),
                            MetadataVersion::Number(2)
                        ),
                        Track::fetch_meta_found(
                            &MetadataVersion::None,
                            &metadata1.timestamp.as_ref().unwrap()
                        ),
                        Track::fetch_meta_found(
                            &MetadataVersion::None,
                            &metadata1.snapshot.as_ref().unwrap()
                        ),
                        Track::fetch_meta_found(
                            &MetadataVersion::None,
                            metadata1.targets.as_ref().unwrap()
                        ),
                    ],
                );
            }
        };

        assert_matches!(client.update().await, Ok(true));
        assert_eq!(client.tuf.trusted_root().version(), 2);

        // We should only fetch metadata from the remote repository and write it to the local
        // repository.
        assert_eq!(
            track_remote.take_tracks(),
            vec![
                Track::fetch_meta_found(&MetadataVersion::Number(2), &metadata2.root),
                Track::FetchErr(
                    MetadataPath::from_role(&Role::Root),
                    MetadataVersion::Number(3)
                ),
                Track::fetch_meta_found(
                    &MetadataVersion::None,
                    &metadata2.timestamp.as_ref().unwrap()
                ),
                Track::fetch_meta_found(
                    &MetadataVersion::Number(2),
                    &metadata2.snapshot.as_ref().unwrap()
                ),
                Track::fetch_meta_found(
                    &MetadataVersion::Number(2),
                    metadata2.targets.as_ref().unwrap()
                ),
            ],
        );
        assert_eq!(
            track_local.take_tracks(),
            vec![
                Track::store_meta(&MetadataVersion::None, &metadata2.root),
                Track::store_meta(&MetadataVersion::Number(2), &metadata2.root),
                Track::store_meta(
                    &MetadataVersion::None,
                    metadata2.timestamp.as_ref().unwrap()
                ),
                Track::store_meta(&MetadataVersion::None, metadata2.snapshot.as_ref().unwrap()),
                Track::store_meta(&MetadataVersion::None, metadata2.targets.as_ref().unwrap()),
            ],
        );

        // Another update should not fetch anything.
        assert_matches!(client.update().await, Ok(false));
        assert_eq!(client.tuf.trusted_root().version(), 2);

        // Make sure we only fetched the next root and timestamp, and didn't store anything.
        assert_eq!(
            track_remote.take_tracks(),
            vec![
                Track::FetchErr(
                    MetadataPath::from_role(&Role::Root),
                    MetadataVersion::Number(3)
                ),
                Track::fetch_meta_found(
                    &MetadataVersion::None,
                    metadata2.timestamp.as_ref().unwrap(),
                ),
            ]
        );
        assert_eq!(track_local.take_tracks(), vec![]);
    }

    #[test]
    fn constructor_succeeds_with_missing_metadata() {
        block_on(async {
            let local = EphemeralRepository::<Json>::new();
            let remote = EphemeralRepository::<Json>::new();

            // Store only a root in the local store.
            let metadata1 = RepoBuilder::new(&local)
                .root_keys(vec![&KEYS[0]])
                .targets_keys(vec![&KEYS[0]])
                .snapshot_keys(vec![&KEYS[0]])
                .timestamp_keys(vec![&KEYS[0]])
                .with_root_builder(|bld| {
                    bld.version(1)
                        .consistent_snapshot(true)
                        .expires(Utc.ymd(2038, 1, 1).and_hms(0, 0, 0))
                        .root_key(KEYS[0].public().clone())
                        .snapshot_key(KEYS[0].public().clone())
                        .targets_key(KEYS[0].public().clone())
                        .timestamp_key(KEYS[0].public().clone())
                })
                .commit()
                .await
                .unwrap();

            let track_local = TrackRepository::new(&local);
            let track_remote = TrackRepository::new(&remote);

            // Create a client, which should try to fetch metadata from the local store.
            let mut client = Client::with_trusted_root(
                Config::default(),
                &metadata1.root,
                &track_local,
                &track_remote,
            )
            .await
            .unwrap();

            assert_eq!(client.tuf.trusted_root().version(), 1);

            // We shouldn't fetch metadata.
            assert_eq!(track_remote.take_tracks(), vec![]);

            // We should have tried fetching a new timestamp, but it shouldn't exist in the
            // repository.
            assert_eq!(
                track_local.take_tracks(),
                vec![
                    Track::FetchErr(
                        MetadataPath::from_role(&Role::Root),
                        MetadataVersion::Number(2)
                    ),
                    Track::FetchErr(
                        MetadataPath::from_role(&Role::Timestamp),
                        MetadataVersion::None
                    )
                ],
            );

            // An update should succeed.
            let _metadata2 = RepoBuilder::new(&remote)
                .root_keys(vec![&KEYS[0]])
                .targets_keys(vec![&KEYS[0]])
                .snapshot_keys(vec![&KEYS[0]])
                .timestamp_keys(vec![&KEYS[0]])
                .with_root_builder(|bld| {
                    bld.version(2)
                        .consistent_snapshot(true)
                        .expires(Utc.ymd(2038, 1, 1).and_hms(0, 0, 0))
                        .root_key(KEYS[0].public().clone())
                        .snapshot_key(KEYS[0].public().clone())
                        .targets_key(KEYS[0].public().clone())
                        .timestamp_key(KEYS[0].public().clone())
                })
                .targets_version(1)
                .snapshot_version(1)
                .timestamp_version(1)
                .commit()
                .await
                .unwrap();

            assert_matches!(client.update().await, Ok(true));
            assert_eq!(client.tuf.trusted_root().version(), 2);
        })
    }

    #[test]
    fn constructor_succeeds_with_expired_metadata() {
        block_on(async {
            let local = EphemeralRepository::<Json>::new();
            let remote = EphemeralRepository::<Json>::new();

            // Store an expired root in the local store.
            let metadata1 = RepoBuilder::new(&local)
                .root_keys(vec![&KEYS[0]])
                .targets_keys(vec![&KEYS[0]])
                .snapshot_keys(vec![&KEYS[0]])
                .timestamp_keys(vec![&KEYS[0]])
                .with_root_builder(|bld| {
                    bld.version(1)
                        .consistent_snapshot(true)
                        .expires(Utc.ymd(1970, 1, 1).and_hms(0, 0, 0))
                        .root_key(KEYS[0].public().clone())
                        .snapshot_key(KEYS[0].public().clone())
                        .targets_key(KEYS[0].public().clone())
                        .timestamp_key(KEYS[0].public().clone())
                })
                .targets_version(1)
                .commit()
                .await
                .unwrap();

            let metadata2 = RepoBuilder::new(&local)
                .root_keys(vec![&KEYS[0]])
                .targets_keys(vec![&KEYS[0]])
                .snapshot_keys(vec![&KEYS[0]])
                .timestamp_keys(vec![&KEYS[0]])
                .with_root_builder(|bld| {
                    bld.version(2)
                        .consistent_snapshot(true)
                        .expires(Utc.ymd(1970, 1, 1).and_hms(0, 0, 0))
                        .root_key(KEYS[0].public().clone())
                        .snapshot_key(KEYS[0].public().clone())
                        .targets_key(KEYS[0].public().clone())
                        .timestamp_key(KEYS[0].public().clone())
                })
                .targets_version(2)
                .snapshot_version(2)
                .timestamp_version(2)
                .commit()
                .await
                .unwrap();

            // Now, make sure that the local metadata got version 1.
            let track_local = TrackRepository::new(&local);
            let track_remote = TrackRepository::new(&remote);

            let mut client = Client::with_trusted_root(
                Config::default(),
                &metadata1.root,
                &track_local,
                &track_remote,
            )
            .await
            .unwrap();

            assert_eq!(client.tuf.trusted_root().version(), 2);

            // We shouldn't fetch metadata.
            assert_eq!(track_remote.take_tracks(), vec![]);

            // We should only load the root metadata, but because it's expired we don't try
            // fetching the other local metadata.
            assert_eq!(
                track_local.take_tracks(),
                vec![
                    Track::fetch_meta_found(&MetadataVersion::Number(2), &metadata2.root),
                    Track::FetchErr(
                        MetadataPath::from_role(&Role::Root),
                        MetadataVersion::Number(3)
                    )
                ],
            );

            // An update should succeed.
            let _metadata3 = RepoBuilder::new(&remote)
                .root_keys(vec![&KEYS[0]])
                .targets_keys(vec![&KEYS[0]])
                .snapshot_keys(vec![&KEYS[0]])
                .timestamp_keys(vec![&KEYS[0]])
                .with_root_builder(|bld| {
                    bld.version(3)
                        .consistent_snapshot(true)
                        .expires(Utc.ymd(2038, 1, 1).and_hms(0, 0, 0))
                        .root_key(KEYS[0].public().clone())
                        .snapshot_key(KEYS[0].public().clone())
                        .targets_key(KEYS[0].public().clone())
                        .timestamp_key(KEYS[0].public().clone())
                })
                .targets_version(3)
                .snapshot_version(3)
                .timestamp_version(3)
                .commit()
                .await
                .unwrap();

            assert_matches!(client.update().await, Ok(true));
            assert_eq!(client.tuf.trusted_root().version(), 3);
        })
    }

    #[test]
    fn constructor_succeeds_with_malformed_metadata() {
        block_on(async {
            // Store a malformed timestamp in the local repository.
            let local = EphemeralRepository::<Json>::new();
            let junk_timestamp = "junk timestamp";

            local
                .store_metadata(
                    &MetadataPath::from_role(&Role::Timestamp),
                    &MetadataVersion::None,
                    &mut junk_timestamp.as_bytes(),
                )
                .await
                .unwrap();

            // Create a normal repository on the remote server.
            let remote = EphemeralRepository::<Json>::new();
            let metadata1 = RepoBuilder::new(&remote)
                .root_keys(vec![&KEYS[0]])
                .targets_keys(vec![&KEYS[0]])
                .snapshot_keys(vec![&KEYS[0]])
                .timestamp_keys(vec![&KEYS[0]])
                .with_root_builder(|bld| {
                    bld.expires(Utc.ymd(2038, 1, 1).and_hms(0, 0, 0))
                        .root_key(KEYS[0].public().clone())
                        .snapshot_key(KEYS[0].public().clone())
                        .targets_key(KEYS[0].public().clone())
                        .timestamp_key(KEYS[0].public().clone())
                })
                .targets_version(1)
                .commit()
                .await
                .unwrap();

            // Create the client. It should ignore the malformed timestamp.
            let track_local = TrackRepository::new(&local);
            let track_remote = TrackRepository::new(&remote);

            let mut client = Client::with_trusted_root(
                Config::default(),
                &metadata1.root,
                &track_local,
                &track_remote,
            )
            .await
            .unwrap();

            assert_eq!(client.tuf.trusted_root().version(), 1);

            // We shouldn't fetch metadata.
            assert_eq!(track_remote.take_tracks(), vec![]);

            // We should only load the root metadata, but because it's expired we don't try
            // fetching the other local metadata.
            assert_eq!(
                track_local.take_tracks(),
                vec![
                    Track::FetchErr(
                        MetadataPath::from_role(&Role::Root),
                        MetadataVersion::Number(2)
                    ),
                    Track::FetchFound {
                        path: MetadataPath::from_role(&Role::Timestamp),
                        version: MetadataVersion::None,
                        metadata: junk_timestamp.into(),
                    },
                ],
            );

            // An update should work.
            assert_matches!(client.update().await, Ok(true));
        })
    }

    #[test]
    fn root_chain_update_consistent_snapshot_false() {
        block_on(root_chain_update(false))
    }

    #[test]
    fn root_chain_update_consistent_snapshot_true() {
        block_on(root_chain_update(true))
    }

    async fn root_chain_update(consistent_snapshot: bool) {
        let repo = EphemeralRepository::<Json>::new();

        // First, create the initial metadata.
        let metadata1 = RepoBuilder::new(&repo)
            .root_keys(vec![&KEYS[0]])
            .targets_keys(vec![&KEYS[0], &KEYS[1], &KEYS[2]])
            .snapshot_keys(vec![&KEYS[0], &KEYS[1], &KEYS[2]])
            .timestamp_keys(vec![&KEYS[0], &KEYS[1], &KEYS[2]])
            .with_root_builder(|bld| {
                bld.version(1)
                    .consistent_snapshot(consistent_snapshot)
                    .expires(Utc.ymd(2038, 1, 1).and_hms(0, 0, 0))
                    .root_key(KEYS[0].public().clone())
                    .snapshot_key(KEYS[0].public().clone())
                    .targets_key(KEYS[0].public().clone())
                    .timestamp_key(KEYS[0].public().clone())
            })
            .targets_version(1)
            .commit()
            .await
            .unwrap();

        let root_path = MetadataPath::from_role(&Role::Root);
        let timestamp_path = MetadataPath::from_role(&Role::Timestamp);

        let targets_version;
        let snapshot_version;
        if consistent_snapshot {
            targets_version = MetadataVersion::Number(1);
            snapshot_version = MetadataVersion::Number(1);
        } else {
            targets_version = MetadataVersion::None;
            snapshot_version = MetadataVersion::None;
        };

        // Now, make sure that the local metadata got version 1.
        let track_local = TrackRepository::new(EphemeralRepository::new());
        let track_remote = TrackRepository::new(&repo);

        let mut client = Client::with_trusted_root_keys(
            Config::default(),
            &MetadataVersion::Number(1),
            1,
            once(&KEYS[0].public().clone()),
            &track_local,
            &track_remote,
        )
        .await
        .unwrap();

        // Check that we tried to load metadata from the local repository.
        assert_eq!(
            track_remote.take_tracks(),
            vec![Track::fetch_found(
                &root_path,
                &MetadataVersion::Number(1),
                metadata1.root.as_bytes()
            ),]
        );
        assert_eq!(
            track_local.take_tracks(),
            vec![
                Track::FetchErr(root_path.clone(), MetadataVersion::Number(1)),
                Track::store_meta(&MetadataVersion::Number(1), &metadata1.root),
                Track::FetchErr(root_path.clone(), MetadataVersion::Number(2)),
                Track::FetchErr(timestamp_path.clone(), MetadataVersion::None),
            ]
        );

        assert_matches!(client.update().await, Ok(true));
        assert_eq!(client.tuf.trusted_root().version(), 1);

        // Make sure we fetched the metadata in the right order.
        assert_eq!(
            track_remote.take_tracks(),
            vec![
                Track::FetchErr(root_path.clone(), MetadataVersion::Number(2)),
                Track::fetch_meta_found(
                    &MetadataVersion::None,
                    &metadata1.timestamp.as_ref().unwrap()
                ),
                Track::fetch_meta_found(&snapshot_version, &metadata1.snapshot.as_ref().unwrap()),
                Track::fetch_meta_found(&targets_version, metadata1.targets.as_ref().unwrap()),
            ]
        );
        assert_eq!(
            track_local.take_tracks(),
            vec![
                Track::store_meta(
                    &MetadataVersion::None,
                    metadata1.timestamp.as_ref().unwrap()
                ),
                Track::store_meta(&MetadataVersion::None, metadata1.snapshot.as_ref().unwrap()),
                Track::store_meta(&MetadataVersion::None, metadata1.targets.as_ref().unwrap()),
            ],
        );

        // Another update should not fetch anything.
        assert_matches!(client.update().await, Ok(false));
        assert_eq!(client.tuf.trusted_root().version(), 1);

        // Make sure we only fetched the next root and timestamp, and didn't store anything.
        assert_eq!(
            track_remote.take_tracks(),
            vec![
                Track::FetchErr(root_path.clone(), MetadataVersion::Number(2)),
                Track::fetch_meta_found(
                    &MetadataVersion::None,
                    metadata1.timestamp.as_ref().unwrap(),
                ),
            ]
        );
        assert_eq!(track_local.take_tracks(), vec![]);

        ////
        // Now bump the root to version 3

        // Make sure the version 2 is also signed by version 1's keys.
        let metadata2 = RepoBuilder::new(&repo)
            .root_keys(vec![&KEYS[0], &KEYS[1]])
            .targets_keys(vec![&KEYS[0], &KEYS[1], &KEYS[2]])
            .snapshot_keys(vec![&KEYS[0], &KEYS[1], &KEYS[2]])
            .timestamp_keys(vec![&KEYS[0], &KEYS[1], &KEYS[2]])
            .with_root_builder(|bld| {
                bld.version(2)
                    .consistent_snapshot(consistent_snapshot)
                    .expires(Utc.ymd(2038, 1, 1).and_hms(0, 0, 0))
                    .root_key(KEYS[1].public().clone())
                    .snapshot_key(KEYS[1].public().clone())
                    .targets_key(KEYS[1].public().clone())
                    .timestamp_key(KEYS[1].public().clone())
            })
            .commit()
            .await
            .unwrap();

        // Make sure the version 3 is also signed by version 2's keys.
        let metadata3 = RepoBuilder::new(&repo)
            .root_keys(vec![&KEYS[1], &KEYS[2]])
            .targets_keys(vec![&KEYS[0], &KEYS[1], &KEYS[2]])
            .snapshot_keys(vec![&KEYS[0], &KEYS[1], &KEYS[2]])
            .timestamp_keys(vec![&KEYS[0], &KEYS[1], &KEYS[2]])
            .with_root_builder(|bld| {
                bld.version(3)
                    .consistent_snapshot(consistent_snapshot)
                    .expires(Utc.ymd(2038, 1, 1).and_hms(0, 0, 0))
                    .root_key(KEYS[2].public().clone())
                    .snapshot_key(KEYS[2].public().clone())
                    .targets_key(KEYS[2].public().clone())
                    .timestamp_key(KEYS[2].public().clone())
            })
            .commit()
            .await
            .unwrap();

        ////
        // Finally, check that the update brings us to version 3.

        assert_matches!(client.update().await, Ok(true));
        assert_eq!(client.tuf.trusted_root().version(), 3);

        // Make sure we fetched and stored the metadata in the expected order. Note that we
        // re-fetch snapshot and targets because we rotated keys, which caused `tuf::Tuf` to delete
        // the metadata.
        assert_eq!(
            track_remote.take_tracks(),
            vec![
                Track::fetch_meta_found(&MetadataVersion::Number(2), &metadata2.root,),
                Track::fetch_meta_found(&MetadataVersion::Number(3), &metadata3.root,),
                Track::FetchErr(root_path.clone(), MetadataVersion::Number(4)),
                Track::fetch_meta_found(
                    &MetadataVersion::None,
                    metadata1.timestamp.as_ref().unwrap(),
                ),
                Track::fetch_meta_found(&snapshot_version, metadata1.snapshot.as_ref().unwrap(),),
                Track::fetch_meta_found(&targets_version, metadata1.targets.as_ref().unwrap(),),
            ]
        );
        assert_eq!(
            track_local.take_tracks(),
            vec![
                Track::store_meta(&MetadataVersion::None, &metadata2.root,),
                Track::store_meta(&MetadataVersion::Number(2), &metadata2.root,),
                Track::store_meta(&MetadataVersion::None, &metadata3.root),
                Track::store_meta(&MetadataVersion::Number(3), &metadata3.root,),
                Track::store_meta(
                    &MetadataVersion::None,
                    metadata1.timestamp.as_ref().unwrap(),
                ),
                Track::store_meta(&MetadataVersion::None, metadata1.snapshot.as_ref().unwrap(),),
                Track::store_meta(&MetadataVersion::None, metadata1.targets.as_ref().unwrap(),),
            ],
        );
    }

    #[test]
    fn test_fetch_target_description_standard() {
        block_on(test_fetch_target_description(
            "standard/metadata".to_string(),
            TargetDescription::from_reader(
                "target with no custom metadata".as_bytes(),
                &[HashAlgorithm::Sha256],
            )
            .unwrap(),
        ));
    }

    #[test]
    fn test_fetch_target_description_custom_empty() {
        block_on(test_fetch_target_description(
            "custom-empty".to_string(),
            TargetDescription::from_reader_with_custom(
                "target with empty custom metadata".as_bytes(),
                &[HashAlgorithm::Sha256],
                hashmap!(),
            )
            .unwrap(),
        ));
    }

    #[test]
    fn test_fetch_target_description_custom() {
        block_on(test_fetch_target_description(
            "custom/metadata".to_string(),
            TargetDescription::from_reader_with_custom(
                "target with lots of custom metadata".as_bytes(),
                &[HashAlgorithm::Sha256],
                hashmap!(
                    "string".to_string() => json!("string"),
                    "bool".to_string() => json!(true),
                    "int".to_string() => json!(42),
                    "object".to_string() => json!({
                        "string": json!("string"),
                        "bool": json!(true),
                        "int": json!(42),
                    }),
                    "array".to_string() => json!([1, 2, 3]),
                ),
            )
            .unwrap(),
        ));
    }

    async fn test_fetch_target_description(path: String, expected_description: TargetDescription) {
        // Generate an ephemeral repository with a single target.
        let remote = EphemeralRepository::<Json>::new();

        let metadata = RepoBuilder::new(&remote)
            .root_keys(vec![&KEYS[0]])
            .targets_keys(vec![&KEYS[0]])
            .snapshot_keys(vec![&KEYS[0]])
            .timestamp_keys(vec![&KEYS[0]])
            .with_root_builder(|bld| {
                bld.expires(Utc.ymd(2038, 1, 1).and_hms(0, 0, 0))
                    .root_key(KEYS[0].public().clone())
                    .snapshot_key(KEYS[0].public().clone())
                    .targets_key(KEYS[0].public().clone())
                    .timestamp_key(KEYS[0].public().clone())
            })
            .with_targets_builder(|bld| {
                bld.insert_target_description(
                    VirtualTargetPath::new(path.clone()).unwrap(),
                    expected_description.clone(),
                )
            })
            .commit()
            .await
            .unwrap();

        // Initialize and update client.
        let mut client = Client::with_trusted_root(
            Config::default(),
            &metadata.root,
            EphemeralRepository::new(),
            remote,
        )
        .await
        .unwrap();

        assert_matches!(client.update().await, Ok(true));

        // Verify fetch_target_description returns expected target metadata
        let description = client
            .fetch_target_description(&TargetPath::new(path).unwrap())
            .await
            .unwrap();

        assert_eq!(description, expected_description);
    }

    #[test]
    fn update_fails_if_cannot_write_to_repo() {
        block_on(async {
            let remote = EphemeralRepository::<Json>::new();

            // First, create the metadata.
            let _ = RepoBuilder::new(&remote)
                .root_keys(vec![&KEYS[0]])
                .targets_keys(vec![&KEYS[0]])
                .snapshot_keys(vec![&KEYS[0]])
                .timestamp_keys(vec![&KEYS[0]])
                .with_root_builder(|bld| {
                    bld.expires(Utc.ymd(2038, 1, 1).and_hms(0, 0, 0))
                        .root_key(KEYS[0].public().clone())
                        .snapshot_key(KEYS[0].public().clone())
                        .targets_key(KEYS[0].public().clone())
                        .timestamp_key(KEYS[0].public().clone())
                })
                .targets_version(1)
                .commit()
                .await
                .unwrap();

            // Now, make sure that the local metadata got version 1.
            let local = ErrorRepository::new(EphemeralRepository::new());
            let mut client = Client::with_trusted_root_keys(
                Config::default(),
                &MetadataVersion::Number(1),
                1,
                once(&KEYS[0].public().clone()),
                &local,
                &remote,
            )
            .await
            .unwrap();

            // The first update should succeed.
            assert_matches!(client.update().await, Ok(true));

            // Publish new metadata.
            let _ = RepoBuilder::new(&remote)
                .root_keys(vec![&KEYS[0]])
                .targets_keys(vec![&KEYS[0]])
                .snapshot_keys(vec![&KEYS[0]])
                .timestamp_keys(vec![&KEYS[0]])
                .with_root_builder(|bld| {
                    bld.expires(Utc.ymd(2038, 1, 1).and_hms(0, 0, 0))
                        .root_key(KEYS[0].public().clone())
                        .snapshot_key(KEYS[0].public().clone())
                        .targets_key(KEYS[0].public().clone())
                        .timestamp_key(KEYS[0].public().clone())
                })
                .targets_version(2)
                .snapshot_version(2)
                .timestamp_version(2)
                .commit()
                .await
                .unwrap();

            local.fail_metadata_stores(true);

            // The second update should fail.
            assert_matches!(client.update().await, Err(Error::Encoding(_)));

            // However, due to https://github.com/theupdateframework/specification/issues/131, if
            // the update is retried a few times it will still succeed.
            assert_matches!(client.update().await, Err(Error::Encoding(_)));
            assert_matches!(client.update().await, Err(Error::Encoding(_)));
            assert_matches!(client.update().await, Ok(false));
        });
    }

    #[test]
    fn with_trusted_methods_return_correct_metadata() {
        block_on(async {
            let local = EphemeralRepository::<Json>::new();
            let remote = EphemeralRepository::<Json>::new();

            // Store an expired root in the local store.
            let metadata1 = RepoBuilder::new(&local)
                .root_keys(vec![&KEYS[0]])
                .targets_keys(vec![&KEYS[0]])
                .snapshot_keys(vec![&KEYS[0]])
                .timestamp_keys(vec![&KEYS[0]])
                .with_root_builder(|bld| {
                    bld.version(1)
                        .consistent_snapshot(true)
                        .expires(Utc.ymd(2038, 1, 1).and_hms(0, 0, 0))
                        .root_key(KEYS[0].public().clone())
                        .snapshot_key(KEYS[0].public().clone())
                        .targets_key(KEYS[0].public().clone())
                        .timestamp_key(KEYS[0].public().clone())
                })
                .targets_version(1)
                .commit()
                .await
                .unwrap();

            let track_local = TrackRepository::new(&local);
            let track_remote = TrackRepository::new(&remote);

            let client = Client::with_trusted_root(
                Config::default(),
                &metadata1.root,
                &track_local,
                &track_remote,
            )
            .await
            .unwrap();

            assert_eq!(client.trusted_targets(), client.tuf.trusted_targets());
            assert_eq!(client.trusted_snapshot(), client.tuf.trusted_snapshot());
            assert_eq!(client.trusted_timestamp(), client.tuf.trusted_timestamp());
            assert_eq!(
                client.trusted_delegations(),
                client.tuf.trusted_delegations()
            );
        })
    }
}
