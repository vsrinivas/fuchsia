//! Clients for high level interactions with TUF repositories.
//!
//! # Example
//!
//! ```no_run
//! # use futures_executor::block_on;
//! # use hyper::client::Client as HttpClient;
//! # use std::path::PathBuf;
//! # use std::str::FromStr;
//! # use tuf::{Result, Database};
//! # use tuf::crypto::PublicKey;
//! # use tuf::client::{Client, Config};
//! # use tuf::metadata::{RootMetadata, Role, MetadataPath, MetadataVersion};
//! # use tuf::pouf::Pouf1;
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
//! let local = FileSystemRepository::<Pouf1>::new(PathBuf::from("~/.rustup"));
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
//!     MetadataVersion::Number(1),
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

use chrono::{offset::Utc, DateTime};
use futures_io::AsyncRead;
use log::{error, warn};
use std::future::Future;
use std::pin::Pin;

use crate::crypto::{self, HashAlgorithm, HashValue, PublicKey};
use crate::database::Database;
use crate::error::{Error, Result};
use crate::metadata::{
    Metadata, MetadataPath, MetadataVersion, RawSignedMetadata, RootMetadata, SnapshotMetadata,
    TargetDescription, TargetPath, TargetsMetadata,
};
use crate::pouf::Pouf;
use crate::repository::{Repository, RepositoryProvider, RepositoryStorage};
use crate::verify::Verified;

/// A client that interacts with TUF repositories.
#[derive(Debug)]
pub struct Client<D, L, R>
where
    D: Pouf,
    L: RepositoryProvider<D> + RepositoryStorage<D>,
    R: RepositoryProvider<D>,
{
    config: Config,
    tuf: Database<D>,
    local: Repository<L, D>,
    remote: Repository<R, D>,
}

impl<D, L, R> Client<D, L, R>
where
    D: Pouf,
    L: RepositoryProvider<D> + RepositoryStorage<D>,
    R: RepositoryProvider<D>,
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
    /// #     pouf::Pouf1,
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
    /// let mut local = EphemeralRepository::<Pouf1>::new();
    /// let remote = EphemeralRepository::<Pouf1>::new();
    ///
    /// let root_version = 1;
    /// let root = RootMetadataBuilder::new()
    ///     .version(root_version)
    ///     .expires(Utc.ymd(2038, 1, 1).and_hms(0, 0, 0))
    ///     .root_key(public_key.clone())
    ///     .snapshot_key(public_key.clone())
    ///     .targets_key(public_key.clone())
    ///     .timestamp_key(public_key.clone())
    ///     .signed::<Pouf1>(&private_key)?;
    ///
    /// let root_path = MetadataPath::root();
    /// let root_version = MetadataVersion::Number(root_version);
    ///
    /// local.store_metadata(
    ///     &root_path,
    ///     root_version,
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
    pub async fn with_trusted_local(config: Config, local: L, remote: R) -> Result<Self> {
        let (local, remote) = (Repository::new(local), Repository::new(remote));
        let root_path = MetadataPath::root();

        // FIXME should this be MetadataVersion::None so we bootstrap with the latest version?
        let root_version = MetadataVersion::Number(1);

        let raw_root: RawSignedMetadata<_, RootMetadata> = local
            .fetch_metadata(&root_path, root_version, config.max_root_length, vec![])
            .await?;

        let tuf = Database::from_trusted_root(&raw_root)?;

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
    /// #     pouf::Pouf1,
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
    /// let local = EphemeralRepository::<Pouf1>::new();
    /// let remote = EphemeralRepository::<Pouf1>::new();
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
    ///     .signed::<Pouf1>(&private_key)
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
        config: Config,
        trusted_root: &RawSignedMetadata<D, RootMetadata>,
        local: L,
        remote: R,
    ) -> Result<Self> {
        let (local, remote) = (Repository::new(local), Repository::new(remote));
        let tuf = Database::from_trusted_root(trusted_root)?;

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
    /// #     pouf::Pouf1,
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
    /// let local = EphemeralRepository::<Pouf1>::new();
    /// let mut remote = EphemeralRepository::<Pouf1>::new();
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
    ///     .signed::<Pouf1>(&private_key)?;
    ///
    /// let root_path = MetadataPath::root();
    /// let root_version = MetadataVersion::Number(root_version);
    ///
    /// remote.store_metadata(
    ///     &root_path,
    ///     root_version,
    ///     &mut root.to_raw().unwrap().as_bytes()
    /// ).await?;
    ///
    /// let client = Client::with_trusted_root_keys(
    ///     Config::default(),
    ///     root_version,
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
        config: Config,
        root_version: MetadataVersion,
        root_threshold: u32,
        trusted_root_keys: I,
        local: L,
        remote: R,
    ) -> Result<Self>
    where
        I: IntoIterator<Item = &'a PublicKey>,
    {
        let (mut local, remote) = (Repository::new(local), Repository::new(remote));

        let root_path = MetadataPath::root();
        let (fetched, raw_root) = fetch_metadata_from_local_or_else_remote(
            &root_path,
            root_version,
            config.max_root_length,
            vec![],
            &local,
            &remote,
        )
        .await?;

        let tuf =
            Database::from_root_with_trusted_keys(&raw_root, root_threshold, trusted_root_keys)?;

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
                .store_metadata(&root_path, root_version, &raw_root)
                .await?;

            // FIXME: should we also store the root as `MetadataVersion::None`?
        }

        Self::new(config, tuf, local, remote).await
    }

    /// Create a new TUF client. It will trust and update the TUF database.
    pub fn from_database(config: Config, tuf: Database<D>, local: L, remote: R) -> Self {
        Self {
            config,
            tuf,
            local: Repository::new(local),
            remote: Repository::new(remote),
        }
    }

    /// Construct a client with the given parts.
    ///
    /// Note: Since this was created by a prior [Client], it does not try to load
    /// metadata from the included local repository, since we would have done
    /// that when the prior [Client] was constructed.
    pub fn from_parts(parts: Parts<D, L, R>) -> Self {
        let Parts {
            config,
            database,
            local,
            remote,
        } = parts;
        Self {
            config,
            tuf: database,
            local: Repository::new(local),
            remote: Repository::new(remote),
        }
    }

    /// Create a new TUF client. It will trust this TUF database.
    async fn new(
        config: Config,
        mut tuf: Database<D>,
        local: Repository<L, D>,
        remote: Repository<R, D>,
    ) -> Result<Self> {
        let start_time = Utc::now();

        let res = async {
            let _r =
                Self::update_root_with_repos(&start_time, &config, &mut tuf, None, &local).await?;
            let _ts =
                Self::update_timestamp_with_repos(&start_time, &config, &mut tuf, None, &local)
                    .await?;
            let _sn = Self::update_snapshot_with_repos(
                &start_time,
                &config,
                &mut tuf,
                None,
                &local,
                false,
            )
            .await?;
            let _ta = Self::update_targets_with_repos(
                &start_time,
                &config,
                &mut tuf,
                None,
                &local,
                false,
            )
            .await?;

            Ok(())
        }
        .await;

        match res {
            Ok(()) | Err(Error::MetadataNotFound { .. }) => {}
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
        self.update_with_start_time(&Utc::now()).await
    }

    /// Update TUF metadata from the remote repository, using the specified time to determine if
    /// the metadata is expired.
    ///
    /// Returns `true` if an update occurred and `false` otherwise.
    ///
    /// **WARNING**: Using an older time opens up users to a freeze attack.
    pub async fn update_with_start_time(&mut self, start_time: &DateTime<Utc>) -> Result<bool> {
        let r = self.update_root(start_time).await?;
        let ts = self.update_timestamp(start_time).await?;
        let sn = self.update_snapshot(start_time).await?;
        let ta = self.update_targets(start_time).await?;

        Ok(r || ts || sn || ta)
    }

    /// Consumes the [Client] and returns the inner [Database] and other parts.
    pub fn into_parts(self) -> Parts<D, L, R> {
        let Client {
            config,
            tuf,
            local,
            remote,
        } = self;
        Parts {
            config,
            database: tuf,
            local: local.into_inner(),
            remote: remote.into_inner(),
        }
    }

    /// Returns a reference to the TUF database.
    pub fn database(&self) -> &Database<D> {
        &self.tuf
    }

    /// Returns a mutable reference to the TUF database.
    pub fn database_mut(&mut self) -> &mut Database<D> {
        &mut self.tuf
    }

    /// Returns a refrerence to the local repository.
    pub fn local_repo(&self) -> &L {
        self.local.as_inner()
    }

    /// Returns a mutable reference to the local repository.
    pub fn local_repo_mut(&mut self) -> &mut L {
        self.local.as_inner_mut()
    }

    /// Returns a refrerence to the remote repository.
    pub fn remote_repo(&self) -> &R {
        self.remote.as_inner()
    }

    /// Returns a mutable reference to the remote repository.
    pub fn remote_repo_mut(&mut self) -> &mut R {
        self.remote.as_inner_mut()
    }

    /// Update TUF root metadata from the remote repository.
    ///
    /// Returns `true` if an update occurred and `false` otherwise.
    pub async fn update_root(&mut self, start_time: &DateTime<Utc>) -> Result<bool> {
        Self::update_root_with_repos(
            start_time,
            &self.config,
            &mut self.tuf,
            Some(&mut self.local),
            &self.remote,
        )
        .await
    }

    async fn update_root_with_repos<Remote>(
        start_time: &DateTime<Utc>,
        config: &Config,
        tuf: &mut Database<D>,
        mut local: Option<&mut Repository<L, D>>,
        remote: &Repository<Remote, D>,
    ) -> Result<bool>
    where
        Remote: RepositoryProvider<D>,
    {
        let root_path = MetadataPath::root();

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
                .fetch_metadata(&root_path, next_version, config.max_root_length, vec![])
                .await;

            let raw_signed_root = match res {
                Ok(raw_signed_root) => raw_signed_root,
                Err(Error::MetadataNotFound { .. }) => {
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

            if let Some(ref mut local) = local {
                local
                    .store_metadata(&root_path, MetadataVersion::None, &raw_signed_root)
                    .await?;

                // NOTE(#301): See the comment in `Client::with_trusted_root_keys`.
                local
                    .store_metadata(&root_path, next_version, &raw_signed_root)
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

        // TODO: Consider moving the root metadata expiration check into `tuf::Database`, since that's
        // where we check timestamp/snapshot/targets/delegations for expiration.
        if tuf.trusted_root().expires() <= start_time {
            error!("Root metadata expired, potential freeze attack");
            return Err(Error::ExpiredMetadata(MetadataPath::root()));
        }

        /////////////////////////////////////////
        // TUF-1.0.5 §5.1.10:
        //
        //     Set whether consistent snapshots are used as per the trusted root metadata file (see
        //     Section 4.3).

        Ok(updated)
    }

    /// Returns `true` if an update occurred and `false` otherwise.
    async fn update_timestamp(&mut self, start_time: &DateTime<Utc>) -> Result<bool> {
        Self::update_timestamp_with_repos(
            start_time,
            &self.config,
            &mut self.tuf,
            Some(&mut self.local),
            &self.remote,
        )
        .await
    }

    async fn update_timestamp_with_repos<Remote>(
        start_time: &DateTime<Utc>,
        config: &Config,
        tuf: &mut Database<D>,
        local: Option<&mut Repository<L, D>>,
        remote: &Repository<Remote, D>,
    ) -> Result<bool>
    where
        Remote: RepositoryProvider<D>,
    {
        let timestamp_path = MetadataPath::timestamp();

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
                MetadataVersion::None,
                config.max_timestamp_length,
                vec![],
            )
            .await?;

        if tuf
            .update_timestamp(start_time, &raw_signed_timestamp)?
            .is_some()
        {
            /////////////////////////////////////////
            // TUF-1.0.9 §5.2.4:
            //
            //     Persist timestamp metadata. The client MUST write the file to non-volatile
            //     storage as FILENAME.EXT (e.g. timestamp.json).

            if let Some(local) = local {
                local
                    .store_metadata(
                        &timestamp_path,
                        MetadataVersion::None,
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
    async fn update_snapshot(&mut self, start_time: &DateTime<Utc>) -> Result<bool> {
        let consistent_snapshot = self.tuf.trusted_root().consistent_snapshot();
        Self::update_snapshot_with_repos(
            start_time,
            &self.config,
            &mut self.tuf,
            Some(&mut self.local),
            &self.remote,
            consistent_snapshot,
        )
        .await
    }

    async fn update_snapshot_with_repos<Remote>(
        start_time: &DateTime<Utc>,
        config: &Config,
        tuf: &mut Database<D>,
        local: Option<&mut Repository<L, D>>,
        remote: &Repository<Remote, D>,
        consistent_snapshots: bool,
    ) -> Result<bool>
    where
        Remote: RepositoryProvider<D>,
    {
        let snapshot_description = match tuf.trusted_timestamp() {
            Some(ts) => Ok(ts.snapshot()),
            None => Err(Error::MetadataNotFound {
                path: MetadataPath::timestamp(),
                version: MetadataVersion::None,
            }),
        }?
        .clone();

        if snapshot_description.version()
            <= tuf.trusted_snapshot().map(|s| s.version()).unwrap_or(0)
        {
            return Ok(false);
        }

        let version = if consistent_snapshots {
            MetadataVersion::Number(snapshot_description.version())
        } else {
            MetadataVersion::None
        };

        let snapshot_path = MetadataPath::snapshot();

        // https://theupdateframework.github.io/specification/v1.0.26/#update-snapshot 5.5.1:

        // Download snapshot metadata file, up to either the number of bytes specified in the
        // timestamp metadata file, or some Y number of bytes.
        let snapshot_length = snapshot_description.length().or(config.max_snapshot_length);

        // https://theupdateframework.github.io/specification/v1.0.26/#update-snapshot 5.5.2:
        //
        // [...] The hashes of the new snapshot metadata file MUST match the hashes, if any, listed
        // in the trusted timestamp metadata.
        let snapshot_hashes = crypto::retain_supported_hashes(snapshot_description.hashes());

        let raw_signed_snapshot = remote
            .fetch_metadata(&snapshot_path, version, snapshot_length, snapshot_hashes)
            .await?;

        // https://theupdateframework.github.io/specification/v1.0.26/#update-snapshot 5.5.3 through
        // 5.5.6 are checked in [Database].
        if tuf.update_snapshot(start_time, &raw_signed_snapshot)? {
            // https://theupdateframework.github.io/specification/v1.0.26/#update-snapshot 5.5.7:
            //
            // Persist snapshot metadata. The client MUST write the file to non-volatile storage as
            // FILENAME.EXT (e.g. snapshot.json).
            if let Some(local) = local {
                local
                    .store_metadata(&snapshot_path, MetadataVersion::None, &raw_signed_snapshot)
                    .await?;
            }

            Ok(true)
        } else {
            Ok(false)
        }
    }

    /// Returns `true` if an update occurred and `false` otherwise.
    async fn update_targets(&mut self, start_time: &DateTime<Utc>) -> Result<bool> {
        let consistent_snapshot = self.tuf.trusted_root().consistent_snapshot();
        Self::update_targets_with_repos(
            start_time,
            &self.config,
            &mut self.tuf,
            Some(&mut self.local),
            &self.remote,
            consistent_snapshot,
        )
        .await
    }

    async fn update_targets_with_repos<Remote>(
        start_time: &DateTime<Utc>,
        config: &Config,
        tuf: &mut Database<D>,
        local: Option<&mut Repository<L, D>>,
        remote: &Repository<Remote, D>,
        consistent_snapshot: bool,
    ) -> Result<bool>
    where
        Remote: RepositoryProvider<D>,
    {
        let targets_description = match tuf.trusted_snapshot() {
            Some(sn) => match sn.meta().get(&MetadataPath::targets()) {
                Some(d) => Ok(d),
                None => Err(Error::MissingMetadataDescription {
                    parent_role: MetadataPath::snapshot(),
                    child_role: MetadataPath::targets(),
                }),
            },
            None => Err(Error::MetadataNotFound {
                path: MetadataPath::snapshot(),
                version: MetadataVersion::None,
            }),
        }?
        .clone();

        if targets_description.version() <= tuf.trusted_targets().map(|t| t.version()).unwrap_or(0)
        {
            return Ok(false);
        }

        let version = if consistent_snapshot {
            MetadataVersion::Number(targets_description.version())
        } else {
            MetadataVersion::None
        };

        let targets_path = MetadataPath::targets();

        // https://theupdateframework.github.io/specification/v1.0.26/#update-targets 5.6.1:
        //
        // Download the top-level targets metadata file, up to either the number of bytes specified
        // in the snapshot metadata file, or some Z number of bytes. [...]
        let targets_length = targets_description.length().or(config.max_targets_length);

        // https://theupdateframework.github.io/specification/v1.0.26/#update-targets 5.6.2:
        //
        // Check against snapshot role’s targets hash. The hashes of the new targets metadata file
        // MUST match the hashes, if any, listed in the trusted snapshot metadata. [...]
        let target_hashes = crypto::retain_supported_hashes(targets_description.hashes());

        let raw_signed_targets = remote
            .fetch_metadata(&targets_path, version, targets_length, target_hashes)
            .await?;

        if tuf.update_targets(start_time, &raw_signed_targets)? {
            /////////////////////////////////////////
            // TUF-1.0.9 §5.4.4:
            //
            //     Persist targets metadata. The client MUST write the file to non-volatile storage
            //     as FILENAME.EXT (e.g. targets.json).

            if let Some(local) = local {
                local
                    .store_metadata(&targets_path, MetadataVersion::None, &raw_signed_targets)
                    .await?;
            }

            Ok(true)
        } else {
            Ok(false)
        }
    }

    /// Fetch a target from the remote repo.
    ///
    /// It is **critical** that none of the bytes written to the `write` are used until this future
    /// returns `Ok`, as the hash of the target is not verified until all bytes are read from the
    /// repository.
    pub async fn fetch_target(
        &mut self,
        target: &TargetPath,
    ) -> Result<impl AsyncRead + Send + Unpin + '_> {
        self.fetch_target_with_start_time(target, &Utc::now()).await
    }

    /// Fetch a target from the remote repo.
    ///
    /// It is **critical** that none of the bytes written to the `write` are used until this future
    /// returns `Ok`, as the hash of the target is not verified until all bytes are read from the
    /// repository.
    pub async fn fetch_target_with_start_time(
        &mut self,
        target: &TargetPath,
        start_time: &DateTime<Utc>,
    ) -> Result<impl AsyncRead + Send + Unpin + '_> {
        let target_description = self
            .fetch_target_description_with_start_time(target, start_time)
            .await?;

        // TODO: Check the local repository to see if it already has the target.
        self.remote
            .fetch_target(
                self.tuf.trusted_root().consistent_snapshot(),
                target,
                target_description,
            )
            .await
    }

    /// Fetch a target from the remote repo and write it to the local repo.
    ///
    /// It is **critical** that none of the bytes written to the `write` are used until this future
    /// returns `Ok`, as the hash of the target is not verified until all bytes are read from the
    /// repository.
    pub async fn fetch_target_to_local(&mut self, target: &TargetPath) -> Result<()> {
        self.fetch_target_to_local_with_start_time(target, &Utc::now())
            .await
    }

    /// Fetch a target from the remote repo and write it to the local repo.
    ///
    /// It is **critical** that none of the bytes written to the `write` are used until this future
    /// returns `Ok`, as the hash of the target is not verified until all bytes are read from the
    /// repository.
    pub async fn fetch_target_to_local_with_start_time(
        &mut self,
        target: &TargetPath,
        start_time: &DateTime<Utc>,
    ) -> Result<()> {
        let target_description = self
            .fetch_target_description_with_start_time(target, start_time)
            .await?;

        // Since the async read we fetch from the remote repository has internal
        // lifetimes, we need to break up client into sub-objects so that rust
        // won't complain about trying to borrow `&self` for the fetch, and
        // `&mut self` for the store.
        let Client {
            tuf, local, remote, ..
        } = self;

        // TODO: Check the local repository to see if it already has the target.
        let mut read = remote
            .fetch_target(
                tuf.trusted_root().consistent_snapshot(),
                target,
                target_description,
            )
            .await?;

        local.store_target(target, &mut read).await
    }

    /// Fetch a target description from the remote repo and return it.
    pub async fn fetch_target_description(
        &mut self,
        target: &TargetPath,
    ) -> Result<TargetDescription> {
        self.fetch_target_description_with_start_time(target, &Utc::now())
            .await
    }

    /// Fetch a target description from the remote repo and return it.
    pub async fn fetch_target_description_with_start_time(
        &mut self,
        target: &TargetPath,
        start_time: &DateTime<Utc>,
    ) -> Result<TargetDescription> {
        let snapshot = self
            .tuf
            .trusted_snapshot()
            .ok_or_else(|| Error::MetadataNotFound {
                path: MetadataPath::snapshot(),
                version: MetadataVersion::None,
            })?
            .clone();

        /////////////////////////////////////////
        // https://theupdateframework.github.io/specification/v1.0.30/#update-targets:
        //
        //     7. **Perform a pre-order depth-first search for metadata about the
        //     desired target, beginning with the top-level targets role.** Note: If
        //     any metadata requested in steps 5.6.7.1 - 5.6.7.2 cannot be downloaded nor
        //     validated, end the search and report that the target cannot be found.

        let (_, target_description) = self
            .lookup_target_description(start_time, false, 0, target, &snapshot, None)
            .await;

        target_description
    }

    async fn lookup_target_description(
        &mut self,
        start_time: &DateTime<Utc>,
        default_terminate: bool,
        current_depth: u32,
        target: &TargetPath,
        snapshot: &SnapshotMetadata,
        targets: Option<(&Verified<TargetsMetadata>, MetadataPath)>,
    ) -> (bool, Result<TargetDescription>) {
        if current_depth > self.config.max_delegation_depth {
            warn!(
                "Walking the delegation graph would have exceeded the configured max depth: {}",
                self.config.max_delegation_depth
            );
            return (
                default_terminate,
                Err(Error::TargetNotFound(target.clone())),
            );
        }

        // these clones are dumb, but we need immutable values and not references for update
        // tuf in the loop below
        let (targets, targets_role) = match targets {
            Some((t, role)) => (t.clone(), role),
            None => match self.tuf.trusted_targets() {
                Some(t) => (t.clone(), MetadataPath::targets()),
                None => {
                    return (
                        default_terminate,
                        Err(Error::MetadataNotFound {
                            path: MetadataPath::targets(),
                            version: MetadataVersion::None,
                        }),
                    );
                }
            },
        };

        if let Some(t) = targets.targets().get(target) {
            return (default_terminate, Ok(t.clone()));
        }

        for delegation in targets.delegations().roles() {
            if !delegation.paths().iter().any(|p| target.is_child(p)) {
                if delegation.terminating() {
                    return (true, Err(Error::TargetNotFound(target.clone())));
                } else {
                    continue;
                }
            }

            let role_meta = match snapshot.meta().get(delegation.name()) {
                Some(m) => m,
                None if delegation.terminating() => {
                    return (true, Err(Error::TargetNotFound(target.clone())));
                }
                None => {
                    continue;
                }
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

            let role_length = role_meta.length().or(self.config.max_targets_length);

            // https://theupdateframework.github.io/specification/v1.0.26/#update-targets
            //
            //     [...] The hashes of the new targets metadata file MUST match the hashes, if
            //      any, listed in the trusted snapshot metadata.
            let role_hashes = crypto::retain_supported_hashes(role_meta.hashes());

            let raw_signed_meta = match self
                .remote
                .fetch_metadata(delegation.name(), version, role_length, role_hashes)
                .await
            {
                Ok(m) => m,
                Err(e) => {
                    warn!("Failed to fetch metadata {:?}: {:?}", delegation.name(), e);
                    if delegation.terminating() {
                        return (true, Err(e));
                    } else {
                        continue;
                    }
                }
            };

            match self.tuf.update_delegated_targets(
                start_time,
                &targets_role,
                delegation.name(),
                &raw_signed_meta,
            ) {
                Ok(_) => {
                    /////////////////////////////////////////
                    // TUF-1.0.9 §5.4.4:
                    //
                    //     Persist targets metadata. The client MUST write the file to non-volatile
                    //     storage as FILENAME.EXT (e.g. targets.json).

                    match self
                        .local
                        .store_metadata(delegation.name(), MetadataVersion::None, &raw_signed_meta)
                        .await
                    {
                        Ok(_) => (),
                        Err(e) => {
                            warn!(
                                "Error storing metadata {:?} locally: {:?}",
                                delegation.name(),
                                e
                            )
                        }
                    }

                    let meta = self
                        .tuf
                        .trusted_delegations()
                        .get(delegation.name())
                        .unwrap()
                        .clone();
                    let f: Pin<Box<dyn Future<Output = _>>> =
                        Box::pin(self.lookup_target_description(
                            start_time,
                            delegation.terminating(),
                            current_depth + 1,
                            target,
                            snapshot,
                            Some((&meta, delegation.name().clone())),
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

        (
            default_terminate,
            Err(Error::TargetNotFound(target.clone())),
        )
    }
}

/// Deconstructed parts of a [Client].
///
/// This allows taking apart a [Client] in order to reclaim the [Database],
/// local, and remote repositories.
#[non_exhaustive]
#[derive(Debug)]
pub struct Parts<D, L, R>
where
    D: Pouf,
    L: RepositoryProvider<D> + RepositoryStorage<D>,
    R: RepositoryProvider<D>,
{
    /// The client configuration.
    pub config: Config,

    /// The Tuf database, which is updated by the [Client].
    pub database: Database<D>,

    /// The local repository, which is used to initialize the database, and
    /// is updated by the [Client].
    pub local: L,

    /// The remote repository, which is used by the client to update the database.
    pub remote: R,
}

/// Helper function that first tries to fetch the metadata from the local store, and if it doesn't
/// exist or does and fails to parse, try fetching it from the remote store.
async fn fetch_metadata_from_local_or_else_remote<'a, D, L, R, M>(
    path: &'a MetadataPath,
    version: MetadataVersion,
    max_length: Option<usize>,
    hashes: Vec<(&'static HashAlgorithm, HashValue)>,
    local: &'a Repository<L, D>,
    remote: &'a Repository<R, D>,
) -> Result<(bool, RawSignedMetadata<D, M>)>
where
    D: Pouf,
    L: RepositoryProvider<D> + RepositoryStorage<D>,
    R: RepositoryProvider<D>,
    M: Metadata + 'static,
{
    match local
        .fetch_metadata(path, version, max_length, hashes.clone())
        .await
    {
        Ok(raw_meta) => Ok((false, raw_meta)),
        Err(Error::MetadataNotFound { .. }) => {
            let raw_meta = remote
                .fetch_metadata(path, version, max_length, hashes)
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
/// # use tuf::client::{Config};
/// let config = Config::default();
/// assert_eq!(config.max_root_length(), &Some(500 * 1024));
/// assert_eq!(config.max_timestamp_length(), &Some(16 * 1024));
/// assert_eq!(config.max_snapshot_length(), &Some(2000000));
/// assert_eq!(config.max_targets_length(), &Some(5000000));
/// assert_eq!(config.max_delegation_depth(), 8);
/// ```
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Config {
    max_root_length: Option<usize>,
    max_timestamp_length: Option<usize>,
    max_snapshot_length: Option<usize>,
    max_targets_length: Option<usize>,
    max_delegation_depth: u32,
}

impl Config {
    /// Initialize a `ConfigBuilder` with the default values.
    pub fn build() -> ConfigBuilder {
        ConfigBuilder::default()
    }

    /// Return the optional maximum root metadata length.
    pub fn max_root_length(&self) -> &Option<usize> {
        &self.max_root_length
    }

    /// Return the optional maximum timestamp metadata size.
    pub fn max_timestamp_length(&self) -> &Option<usize> {
        &self.max_timestamp_length
    }

    /// Return the optional maximum snapshot metadata size.
    pub fn max_snapshot_length(&self) -> &Option<usize> {
        &self.max_snapshot_length
    }

    /// Return the optional maximum targets metadata size.
    pub fn max_targets_length(&self) -> &Option<usize> {
        &self.max_targets_length
    }

    /// The maximum number of steps used when walking the delegation graph.
    pub fn max_delegation_depth(&self) -> u32 {
        self.max_delegation_depth
    }
}

impl Default for Config {
    fn default() -> Self {
        Config {
            max_root_length: Some(500 * 1024),
            max_timestamp_length: Some(16 * 1024),
            max_snapshot_length: Some(2000000),
            max_targets_length: Some(5000000),
            max_delegation_depth: 8,
        }
    }
}

/// Helper for building and validating a TUF client `Config`.
#[derive(Debug, Default, PartialEq, Eq)]
pub struct ConfigBuilder {
    cfg: Config,
}

impl ConfigBuilder {
    /// Validate this builder return a `Config` if validation succeeds.
    pub fn finish(self) -> Result<Config> {
        Ok(self.cfg)
    }

    /// Set the optional maximum download length for root metadata.
    pub fn max_root_length(mut self, max: Option<usize>) -> Self {
        self.cfg.max_root_length = max;
        self
    }

    /// Set the optional maximum download length for timestamp metadata.
    pub fn max_timestamp_length(mut self, max: Option<usize>) -> Self {
        self.cfg.max_timestamp_length = max;
        self
    }

    /// Set the optional maximum download length for snapshot metadata.
    pub fn max_snapshot_length(mut self, max: Option<usize>) -> Self {
        self.cfg.max_snapshot_length = max;
        self
    }

    /// Set the optional maximum download length for targets metadata.
    pub fn max_targets_length(mut self, max: Option<usize>) -> Self {
        self.cfg.max_targets_length = max;
        self
    }

    /// Set the maximum number of steps used when walking the delegation graph.
    pub fn max_delegation_depth(mut self, max: u32) -> Self {
        self.cfg.max_delegation_depth = max;
        self
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::crypto::{Ed25519PrivateKey, HashAlgorithm, PrivateKey};
    use crate::metadata::{
        MetadataDescription, MetadataPath, MetadataVersion, RootMetadataBuilder,
        SnapshotMetadataBuilder, TargetsMetadataBuilder, TimestampMetadataBuilder,
    };
    use crate::pouf::Pouf1;
    use crate::repo_builder::RepoBuilder;
    use crate::repository::{
        fetch_metadata_to_string, EphemeralRepository, ErrorRepository, Track, TrackRepository,
    };
    use assert_matches::assert_matches;
    use chrono::prelude::*;
    use futures_executor::block_on;
    use lazy_static::lazy_static;
    use maplit::hashmap;
    use pretty_assertions::assert_eq;
    use serde_json::json;
    use std::collections::HashMap;
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

    #[allow(clippy::enum_variant_names)]
    enum ConstructorMode {
        WithTrustedLocal,
        WithTrustedRoot,
        WithTrustedRootKeys,
        FromDatabase,
    }

    #[test]
    fn client_constructors_err_with_not_found() {
        block_on(async {
            let mut local = EphemeralRepository::<Pouf1>::new();
            let remote = EphemeralRepository::<Pouf1>::new();

            let private_key =
                Ed25519PrivateKey::from_pkcs8(&Ed25519PrivateKey::pkcs8().unwrap()).unwrap();
            let public_key = private_key.public().clone();

            assert_matches!(
                Client::with_trusted_local(Config::default(), &mut local, &remote).await,
                Err(Error::MetadataNotFound { path, version })
                if path == MetadataPath::root() && version == MetadataVersion::Number(1)
            );

            assert_matches!(
                Client::with_trusted_root_keys(
                    Config::default(),
                    MetadataVersion::Number(1),
                    1,
                    once(&public_key),
                    local,
                    &remote,
                )
                .await,
                Err(Error::MetadataNotFound { path, version })
                if path == MetadataPath::root() && version == MetadataVersion::Number(1)
            );
        })
    }

    #[test]
    fn client_constructors_err_with_invalid_keys() {
        block_on(async {
            let mut remote = EphemeralRepository::<Pouf1>::new();

            let good_private_key = &KEYS[0];
            let bad_private_key = &KEYS[1];

            let _ = RepoBuilder::create(&mut remote)
                .trusted_root_keys(&[good_private_key])
                .trusted_targets_keys(&[good_private_key])
                .trusted_snapshot_keys(&[good_private_key])
                .trusted_timestamp_keys(&[good_private_key])
                .commit()
                .await
                .unwrap();

            assert_matches!(
                Client::with_trusted_root_keys(
                    Config::default(),
                    MetadataVersion::Number(1),
                    1,
                    once(bad_private_key.public()),
                    EphemeralRepository::new(),
                    &remote,
                )
                .await,
                Err(Error::MetadataMissingSignatures { role, number_of_valid_signatures: 0, threshold: 1 })
                if role == MetadataPath::root()
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

    #[test]
    fn from_database_loads_metadata_from_local_repo() {
        block_on(constructors_load_metadata_from_local_repo(
            ConstructorMode::FromDatabase,
        ))
    }

    async fn constructors_load_metadata_from_local_repo(constructor_mode: ConstructorMode) {
        // Store an expired root in the local store.
        let mut local = EphemeralRepository::<Pouf1>::new();
        let metadata1 = RepoBuilder::create(&mut local)
            .current_time(Utc.timestamp(0, 0))
            .trusted_root_keys(&[&KEYS[0]])
            .trusted_targets_keys(&[&KEYS[0]])
            .trusted_snapshot_keys(&[&KEYS[0]])
            .trusted_timestamp_keys(&[&KEYS[0]])
            .stage_root_with_builder(|bld| bld.consistent_snapshot(true))
            .unwrap()
            .commit()
            .await
            .unwrap();

        // Remote repo has unexpired metadata.
        let mut remote = EphemeralRepository::<Pouf1>::new();
        let metadata2 = RepoBuilder::create(&mut remote)
            .trusted_root_keys(&[&KEYS[0]])
            .trusted_targets_keys(&[&KEYS[0]])
            .trusted_snapshot_keys(&[&KEYS[0]])
            .trusted_timestamp_keys(&[&KEYS[0]])
            .stage_root_with_builder(|bld| bld.version(2).consistent_snapshot(true))
            .unwrap()
            .stage_targets_with_builder(|bld| bld.version(2))
            .unwrap()
            .stage_snapshot_with_builder(|bld| bld.version(2))
            .unwrap()
            .stage_timestamp_with_builder(|bld| bld.version(2))
            .unwrap()
            .commit()
            .await
            .unwrap();

        // Now, make sure that the local metadata got version 1.
        let track_local = TrackRepository::new(local);
        let track_remote = TrackRepository::new(remote);

        // Make sure the client initialized metadata in the right order. Each has a slightly
        // different usage of the local repository.
        let mut client = match constructor_mode {
            ConstructorMode::WithTrustedLocal => {
                Client::with_trusted_local(Config::default(), track_local, track_remote)
                    .await
                    .unwrap()
            }
            ConstructorMode::WithTrustedRoot => Client::with_trusted_root(
                Config::default(),
                metadata1.root().unwrap(),
                track_local,
                track_remote,
            )
            .await
            .unwrap(),
            ConstructorMode::WithTrustedRootKeys => Client::with_trusted_root_keys(
                Config::default(),
                MetadataVersion::Number(1),
                1,
                once(&KEYS[0].public().clone()),
                track_local,
                track_remote,
            )
            .await
            .unwrap(),
            ConstructorMode::FromDatabase => Client::from_database(
                Config::default(),
                Database::from_trusted_root(metadata1.root().unwrap()).unwrap(),
                track_local,
                track_remote,
            ),
        };

        assert_eq!(client.tuf.trusted_root().version(), 1);
        assert_eq!(client.remote_repo().take_tracks(), vec![]);

        // According to [1], "Check for freeze attack", only the root should be
        // fetched since it has expired.
        //
        // [1]: https://theupdateframework.github.io/specification/latest/#update-root
        match constructor_mode {
            ConstructorMode::WithTrustedLocal => {
                assert_eq!(
                    client.local_repo().take_tracks(),
                    vec![
                        Track::fetch_meta_found(
                            MetadataVersion::Number(1),
                            metadata1.root().unwrap()
                        ),
                        Track::FetchErr(MetadataPath::root(), MetadataVersion::Number(2)),
                    ],
                );
            }
            ConstructorMode::WithTrustedRoot => {
                assert_eq!(
                    client.local_repo().take_tracks(),
                    vec![Track::FetchErr(
                        MetadataPath::root(),
                        MetadataVersion::Number(2)
                    )],
                );
            }
            ConstructorMode::WithTrustedRootKeys => {
                assert_eq!(
                    client.local_repo().take_tracks(),
                    vec![
                        Track::fetch_meta_found(
                            MetadataVersion::Number(1),
                            metadata1.root().unwrap()
                        ),
                        Track::FetchErr(MetadataPath::root(), MetadataVersion::Number(2)),
                    ],
                );
            }
            ConstructorMode::FromDatabase => {
                assert_eq!(client.local_repo().take_tracks(), vec![],);
            }
        };

        assert_matches!(client.update().await, Ok(true));
        assert_eq!(client.tuf.trusted_root().version(), 2);

        // We should only fetch metadata from the remote repository and write it to the local
        // repository.
        assert_eq!(
            client.remote_repo().take_tracks(),
            vec![
                Track::fetch_meta_found(MetadataVersion::Number(2), metadata2.root().unwrap()),
                Track::FetchErr(MetadataPath::root(), MetadataVersion::Number(3)),
                Track::fetch_meta_found(MetadataVersion::None, metadata2.timestamp().unwrap()),
                Track::fetch_meta_found(MetadataVersion::Number(2), metadata2.snapshot().unwrap()),
                Track::fetch_meta_found(MetadataVersion::Number(2), metadata2.targets().unwrap()),
            ],
        );
        assert_eq!(
            client.local_repo().take_tracks(),
            vec![
                Track::store_meta(MetadataVersion::None, metadata2.root().unwrap()),
                Track::store_meta(MetadataVersion::Number(2), metadata2.root().unwrap()),
                Track::store_meta(MetadataVersion::None, metadata2.timestamp().unwrap()),
                Track::store_meta(MetadataVersion::None, metadata2.snapshot().unwrap()),
                Track::store_meta(MetadataVersion::None, metadata2.targets().unwrap()),
            ],
        );

        // Another update should not fetch anything.
        assert_matches!(client.update().await, Ok(false));
        assert_eq!(client.tuf.trusted_root().version(), 2);

        // Make sure we only fetched the next root and timestamp, and didn't store anything.
        assert_eq!(
            client.remote_repo().take_tracks(),
            vec![
                Track::FetchErr(MetadataPath::root(), MetadataVersion::Number(3)),
                Track::fetch_meta_found(MetadataVersion::None, metadata2.timestamp().unwrap()),
            ]
        );
        assert_eq!(client.local_repo().take_tracks(), vec![]);
    }

    #[test]
    fn constructor_succeeds_with_missing_metadata() {
        block_on(async {
            let mut local = EphemeralRepository::<Pouf1>::new();
            let remote = EphemeralRepository::<Pouf1>::new();

            // Store only a root in the local store.
            let metadata1 = RepoBuilder::create(&mut local)
                .trusted_root_keys(&[&KEYS[0]])
                .trusted_targets_keys(&[&KEYS[0]])
                .trusted_snapshot_keys(&[&KEYS[0]])
                .trusted_timestamp_keys(&[&KEYS[0]])
                .stage_root_with_builder(|bld| bld.consistent_snapshot(true))
                .unwrap()
                .skip_targets()
                .skip_snapshot()
                .skip_timestamp()
                .commit()
                .await
                .unwrap();

            let track_local = TrackRepository::new(local);
            let track_remote = TrackRepository::new(remote);

            // Create a client, which should try to fetch metadata from the local store.
            let client = Client::with_trusted_root(
                Config::default(),
                metadata1.root().unwrap(),
                track_local,
                track_remote,
            )
            .await
            .unwrap();

            assert_eq!(client.tuf.trusted_root().version(), 1);

            // We shouldn't fetch metadata.
            assert_eq!(client.remote_repo().take_tracks(), vec![]);

            // We should have tried fetching a new timestamp, but it shouldn't exist in the
            // repository.
            assert_eq!(
                client.local_repo().take_tracks(),
                vec![
                    Track::FetchErr(MetadataPath::root(), MetadataVersion::Number(2)),
                    Track::FetchErr(MetadataPath::timestamp(), MetadataVersion::None)
                ],
            );

            // An update should succeed.
            let mut parts = client.into_parts();
            let metadata2 = RepoBuilder::create(parts.remote.as_inner_mut())
                .trusted_root_keys(&[&KEYS[0]])
                .trusted_targets_keys(&[&KEYS[0]])
                .trusted_snapshot_keys(&[&KEYS[0]])
                .trusted_timestamp_keys(&[&KEYS[0]])
                .stage_root_with_builder(|bld| bld.version(2).consistent_snapshot(true))
                .unwrap()
                .stage_targets_with_builder(|bld| bld.version(2))
                .unwrap()
                .stage_snapshot_with_builder(|bld| bld.version(2))
                .unwrap()
                .stage_timestamp_with_builder(|bld| bld.version(2))
                .unwrap()
                .commit()
                .await
                .unwrap();

            let mut client = Client::from_parts(parts);
            assert_matches!(client.update().await, Ok(true));
            assert_eq!(client.tuf.trusted_root().version(), 2);

            // We should have fetched the metadata, and written it to the local database.
            assert_eq!(
                client.remote_repo().take_tracks(),
                vec![
                    Track::fetch_meta_found(MetadataVersion::Number(2), metadata2.root().unwrap()),
                    Track::FetchErr(MetadataPath::root(), MetadataVersion::Number(3)),
                    Track::fetch_meta_found(MetadataVersion::None, metadata2.timestamp().unwrap()),
                    Track::fetch_meta_found(
                        MetadataVersion::Number(2),
                        metadata2.snapshot().unwrap()
                    ),
                    Track::fetch_meta_found(
                        MetadataVersion::Number(2),
                        metadata2.targets().unwrap()
                    ),
                ],
            );
            assert_eq!(
                client.local_repo().take_tracks(),
                vec![
                    Track::store_meta(MetadataVersion::None, metadata2.root().unwrap()),
                    Track::store_meta(MetadataVersion::Number(2), metadata2.root().unwrap()),
                    Track::store_meta(MetadataVersion::None, metadata2.timestamp().unwrap()),
                    Track::store_meta(MetadataVersion::None, metadata2.snapshot().unwrap()),
                    Track::store_meta(MetadataVersion::None, metadata2.targets().unwrap()),
                ],
            );
        })
    }

    #[test]
    fn constructor_succeeds_with_expired_metadata() {
        block_on(async {
            let mut local = EphemeralRepository::<Pouf1>::new();
            let remote = EphemeralRepository::<Pouf1>::new();

            // Store an expired root in the local store.
            let metadata1 = RepoBuilder::create(&mut local)
                .current_time(Utc.timestamp(0, 0))
                .trusted_root_keys(&[&KEYS[0]])
                .trusted_targets_keys(&[&KEYS[0]])
                .trusted_snapshot_keys(&[&KEYS[0]])
                .trusted_timestamp_keys(&[&KEYS[0]])
                .stage_root_with_builder(|bld| bld.version(1).consistent_snapshot(true))
                .unwrap()
                .commit()
                .await
                .unwrap();

            let metadata2 = RepoBuilder::create(&mut local)
                .current_time(Utc.timestamp(0, 0))
                .trusted_root_keys(&[&KEYS[0]])
                .trusted_targets_keys(&[&KEYS[0]])
                .trusted_snapshot_keys(&[&KEYS[0]])
                .trusted_timestamp_keys(&[&KEYS[0]])
                .stage_root_with_builder(|bld| bld.version(2).consistent_snapshot(true))
                .unwrap()
                .stage_targets_with_builder(|bld| bld.version(2))
                .unwrap()
                .stage_snapshot_with_builder(|bld| bld.version(2))
                .unwrap()
                .stage_timestamp_with_builder(|bld| bld.version(2))
                .unwrap()
                .commit()
                .await
                .unwrap();

            // Now, make sure that the local metadata got version 1.
            let track_local = TrackRepository::new(local);
            let track_remote = TrackRepository::new(remote);

            let client = Client::with_trusted_root(
                Config::default(),
                metadata1.root().unwrap(),
                track_local,
                track_remote,
            )
            .await
            .unwrap();

            assert_eq!(client.tuf.trusted_root().version(), 2);

            // We shouldn't fetch metadata.
            assert_eq!(client.remote_repo().take_tracks(), vec![]);

            // We should only load the root metadata, but because it's expired we don't try
            // fetching the other local metadata.
            assert_eq!(
                client.local_repo().take_tracks(),
                vec![
                    Track::fetch_meta_found(MetadataVersion::Number(2), metadata2.root().unwrap()),
                    Track::FetchErr(MetadataPath::root(), MetadataVersion::Number(3))
                ],
            );

            // An update should succeed.
            let mut parts = client.into_parts();
            let _metadata3 = RepoBuilder::create(&mut parts.remote)
                .trusted_root_keys(&[&KEYS[0]])
                .trusted_targets_keys(&[&KEYS[0]])
                .trusted_snapshot_keys(&[&KEYS[0]])
                .trusted_timestamp_keys(&[&KEYS[0]])
                .stage_root_with_builder(|bld| {
                    bld.version(3)
                        .consistent_snapshot(true)
                        .expires(Utc.ymd(2038, 1, 1).and_hms(0, 0, 0))
                })
                .unwrap()
                .stage_targets_with_builder(|bld| bld.version(2))
                .unwrap()
                .stage_snapshot_with_builder(|bld| bld.version(2))
                .unwrap()
                .stage_timestamp_with_builder(|bld| bld.version(2))
                .unwrap()
                .commit()
                .await
                .unwrap();

            let mut client = Client::from_parts(parts);
            assert_matches!(client.update().await, Ok(true));
            assert_eq!(client.tuf.trusted_root().version(), 3);
        })
    }

    #[test]
    fn constructor_succeeds_with_malformed_metadata() {
        block_on(async {
            // Store a malformed timestamp in the local repository.
            let local = EphemeralRepository::<Pouf1>::new();
            let junk_timestamp = "junk timestamp";

            local
                .store_metadata(
                    &MetadataPath::timestamp(),
                    MetadataVersion::None,
                    &mut junk_timestamp.as_bytes(),
                )
                .await
                .unwrap();

            // Create a normal repository on the remote server.
            let mut remote = EphemeralRepository::<Pouf1>::new();
            let metadata1 = RepoBuilder::create(&mut remote)
                .trusted_root_keys(&[&KEYS[0]])
                .trusted_targets_keys(&[&KEYS[0]])
                .trusted_snapshot_keys(&[&KEYS[0]])
                .trusted_timestamp_keys(&[&KEYS[0]])
                .commit()
                .await
                .unwrap();

            // Create the client. It should ignore the malformed timestamp.
            let track_local = TrackRepository::new(local);
            let track_remote = TrackRepository::new(remote);

            let mut client = Client::with_trusted_root(
                Config::default(),
                metadata1.root().unwrap(),
                track_local,
                track_remote,
            )
            .await
            .unwrap();

            assert_eq!(client.tuf.trusted_root().version(), 1);

            // We shouldn't fetch metadata.
            assert_eq!(client.remote_repo().take_tracks(), vec![]);

            // We should only load the root metadata, but because it's expired we don't try
            // fetching the other local metadata.
            assert_eq!(
                client.local_repo().take_tracks(),
                vec![
                    Track::FetchErr(MetadataPath::root(), MetadataVersion::Number(2)),
                    Track::FetchFound {
                        path: MetadataPath::timestamp(),
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
        let mut repo = EphemeralRepository::<Pouf1>::new();

        // First, create the initial metadata. We want to use the same non-root
        // metadata, so sign it with all the keys.
        let metadata1 = RepoBuilder::create(&mut repo)
            .trusted_root_keys(&[&KEYS[0]])
            .signing_targets_keys(&[&KEYS[1], &KEYS[2]])
            .trusted_targets_keys(&[&KEYS[0]])
            .signing_snapshot_keys(&[&KEYS[1], &KEYS[2]])
            .trusted_snapshot_keys(&[&KEYS[0]])
            .signing_timestamp_keys(&[&KEYS[1], &KEYS[2]])
            .trusted_timestamp_keys(&[&KEYS[0]])
            .stage_root_with_builder(|bld| bld.consistent_snapshot(consistent_snapshot))
            .unwrap()
            .commit()
            .await
            .unwrap();

        let root_path = MetadataPath::root();
        let timestamp_path = MetadataPath::timestamp();

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
        let track_remote = TrackRepository::new(repo);

        let mut client = Client::with_trusted_root_keys(
            Config::default(),
            MetadataVersion::Number(1),
            1,
            once(&KEYS[0].public().clone()),
            track_local,
            track_remote,
        )
        .await
        .unwrap();

        // Check that we tried to load metadata from the local repository.
        assert_eq!(
            client.remote_repo().take_tracks(),
            vec![Track::fetch_found(
                &root_path,
                MetadataVersion::Number(1),
                metadata1.root().unwrap().as_bytes()
            ),]
        );
        assert_eq!(
            client.local_repo().take_tracks(),
            vec![
                Track::FetchErr(root_path.clone(), MetadataVersion::Number(1)),
                Track::store_meta(MetadataVersion::Number(1), metadata1.root().unwrap()),
                Track::FetchErr(root_path.clone(), MetadataVersion::Number(2)),
                Track::FetchErr(timestamp_path.clone(), MetadataVersion::None),
            ]
        );

        assert_matches!(client.update().await, Ok(true));
        assert_eq!(client.tuf.trusted_root().version(), 1);

        // Make sure we fetched the metadata in the right order.
        assert_eq!(
            client.remote_repo().take_tracks(),
            vec![
                Track::FetchErr(root_path.clone(), MetadataVersion::Number(2)),
                Track::fetch_meta_found(MetadataVersion::None, metadata1.timestamp().unwrap()),
                Track::fetch_meta_found(snapshot_version, metadata1.snapshot().unwrap()),
                Track::fetch_meta_found(targets_version, metadata1.targets().unwrap()),
            ]
        );
        assert_eq!(
            client.local_repo().take_tracks(),
            vec![
                Track::store_meta(MetadataVersion::None, metadata1.timestamp().unwrap()),
                Track::store_meta(MetadataVersion::None, metadata1.snapshot().unwrap()),
                Track::store_meta(MetadataVersion::None, metadata1.targets().unwrap()),
            ],
        );

        // Another update should not fetch anything.
        assert_matches!(client.update().await, Ok(false));
        assert_eq!(client.tuf.trusted_root().version(), 1);

        // Make sure we only fetched the next root and timestamp, and didn't store anything.
        assert_eq!(
            client.remote_repo().take_tracks(),
            vec![
                Track::FetchErr(root_path.clone(), MetadataVersion::Number(2)),
                Track::fetch_meta_found(MetadataVersion::None, metadata1.timestamp().unwrap()),
            ]
        );
        assert_eq!(client.local_repo().take_tracks(), vec![]);

        ////
        // Now bump the root to version 3

        // Make sure the version 2 is also signed by version 1's keys.
        //
        // Note that we write to the underlying store so TrackRepo doesn't track
        // this new metadata.
        let mut parts = client.into_parts();
        let metadata2 = RepoBuilder::create(parts.remote.as_inner_mut())
            .signing_root_keys(&[&KEYS[0]])
            .trusted_root_keys(&[&KEYS[1]])
            .trusted_targets_keys(&[&KEYS[1]])
            .trusted_snapshot_keys(&[&KEYS[1]])
            .trusted_timestamp_keys(&[&KEYS[1]])
            .stage_root_with_builder(|bld| bld.version(2).consistent_snapshot(consistent_snapshot))
            .unwrap()
            .skip_targets()
            .skip_snapshot()
            .skip_timestamp()
            .commit()
            .await
            .unwrap();

        // Make sure the version 3 is also signed by version 2's keys.
        let metadata3 = RepoBuilder::create(parts.remote.as_inner_mut())
            .signing_root_keys(&[&KEYS[1]])
            .trusted_root_keys(&[&KEYS[2]])
            .trusted_targets_keys(&[&KEYS[2]])
            .trusted_snapshot_keys(&[&KEYS[2]])
            .trusted_timestamp_keys(&[&KEYS[2]])
            .stage_root_with_builder(|bld| bld.version(3).consistent_snapshot(consistent_snapshot))
            .unwrap()
            .skip_targets()
            .skip_snapshot()
            .skip_timestamp()
            .commit()
            .await
            .unwrap();

        ////
        // Finally, check that the update brings us to version 3.
        let mut client = Client::from_parts(parts);
        assert_matches!(client.update().await, Ok(true));
        assert_eq!(client.tuf.trusted_root().version(), 3);

        // Make sure we fetched and stored the metadata in the expected order. Note that we
        // re-fetch snapshot and targets because we rotated keys, which caused `tuf::Database` to delete
        // the metadata.
        assert_eq!(
            client.remote_repo().take_tracks(),
            vec![
                Track::fetch_meta_found(MetadataVersion::Number(2), metadata2.root().unwrap()),
                Track::fetch_meta_found(MetadataVersion::Number(3), metadata3.root().unwrap()),
                Track::FetchErr(root_path.clone(), MetadataVersion::Number(4)),
                Track::fetch_meta_found(MetadataVersion::None, metadata1.timestamp().unwrap()),
                Track::fetch_meta_found(snapshot_version, metadata1.snapshot().unwrap()),
                Track::fetch_meta_found(targets_version, metadata1.targets().unwrap()),
            ]
        );
        assert_eq!(
            client.local_repo().take_tracks(),
            vec![
                Track::store_meta(MetadataVersion::None, metadata2.root().unwrap()),
                Track::store_meta(MetadataVersion::Number(2), metadata2.root().unwrap()),
                Track::store_meta(MetadataVersion::None, metadata3.root().unwrap()),
                Track::store_meta(MetadataVersion::Number(3), metadata3.root().unwrap()),
                Track::store_meta(MetadataVersion::None, metadata1.timestamp().unwrap()),
                Track::store_meta(MetadataVersion::None, metadata1.snapshot().unwrap()),
                Track::store_meta(MetadataVersion::None, metadata1.targets().unwrap()),
            ],
        );
    }

    #[test]
    fn test_fetch_target_description_standard() {
        block_on(test_fetch_target_description(
            "standard/metadata".to_string(),
            TargetDescription::from_slice(
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
            TargetDescription::from_slice_with_custom(
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
            TargetDescription::from_slice_with_custom(
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
        let mut remote = EphemeralRepository::<Pouf1>::new();

        let metadata = RepoBuilder::create(&mut remote)
            .trusted_root_keys(&[&KEYS[0]])
            .trusted_targets_keys(&[&KEYS[0]])
            .trusted_snapshot_keys(&[&KEYS[0]])
            .trusted_timestamp_keys(&[&KEYS[0]])
            .stage_root()
            .unwrap()
            .stage_targets_with_builder(|bld| {
                bld.insert_target_description(
                    TargetPath::new(path.clone()).unwrap(),
                    expected_description.clone(),
                )
            })
            .unwrap()
            .commit()
            .await
            .unwrap();

        // Initialize and update client.
        let mut client = Client::with_trusted_root(
            Config::default(),
            metadata.root().unwrap(),
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
    fn update_eventually_succeeds_if_cannot_write_to_repo() {
        block_on(async {
            let mut remote = EphemeralRepository::<Pouf1>::new();

            // First, create the metadata.
            let _ = RepoBuilder::create(&mut remote)
                .trusted_root_keys(&[&KEYS[0]])
                .trusted_targets_keys(&[&KEYS[0]])
                .trusted_snapshot_keys(&[&KEYS[0]])
                .trusted_timestamp_keys(&[&KEYS[0]])
                .commit()
                .await
                .unwrap();

            // Now, make sure that the local metadata got version 1.
            let local = ErrorRepository::new(EphemeralRepository::new());
            let mut client = Client::with_trusted_root_keys(
                Config::default(),
                MetadataVersion::Number(1),
                1,
                once(&KEYS[0].public().clone()),
                local,
                remote,
            )
            .await
            .unwrap();

            // The first update should succeed.
            assert_matches!(client.update().await, Ok(true));

            // Make sure the database is correct.
            let mut parts = client.into_parts();
            assert_eq!(parts.database.trusted_root().version(), 1);
            assert_eq!(parts.database.trusted_timestamp().unwrap().version(), 1);
            assert_eq!(parts.database.trusted_snapshot().unwrap().version(), 1);
            assert_eq!(parts.database.trusted_targets().unwrap().version(), 1);

            // Publish new metadata.
            let _ = RepoBuilder::create(&mut parts.remote)
                .trusted_root_keys(&[&KEYS[0]])
                .trusted_targets_keys(&[&KEYS[0]])
                .trusted_snapshot_keys(&[&KEYS[0]])
                .trusted_timestamp_keys(&[&KEYS[0]])
                .stage_root_with_builder(|bld| bld.version(2))
                .unwrap()
                .stage_targets_with_builder(|bld| bld.version(2))
                .unwrap()
                .stage_snapshot_with_builder(|bld| bld.version(2))
                .unwrap()
                .stage_timestamp_with_builder(|bld| bld.version(2))
                .unwrap()
                .commit()
                .await
                .unwrap();

            // Make sure we fail to write metadata to the local store.
            parts.local.fail_metadata_stores(true);

            // The second update should fail.
            let mut client = Client::from_parts(parts);
            assert_matches!(client.update().await, Err(Error::Encoding(_)));

            // FIXME(#297): rust-tuf diverges from the spec by throwing away the
            // metadata if the root is updated.
            assert_eq!(client.database().trusted_root().version(), 2);
            assert_eq!(client.database().trusted_timestamp(), None);
            assert_eq!(client.database().trusted_snapshot(), None);
            assert_eq!(client.database().trusted_targets(), None);

            // However, due to https://github.com/theupdateframework/specification/issues/131, if
            // the update is retried a few times it will still succeed.
            assert_matches!(client.update().await, Err(Error::Encoding(_)));
            assert_eq!(client.database().trusted_root().version(), 2);
            assert_eq!(client.database().trusted_timestamp().unwrap().version(), 2);
            assert_eq!(client.database().trusted_snapshot(), None);
            assert_eq!(client.database().trusted_targets(), None);

            assert_matches!(client.update().await, Err(Error::Encoding(_)));
            assert_eq!(client.database().trusted_root().version(), 2);
            assert_eq!(client.database().trusted_timestamp().unwrap().version(), 2);
            assert_eq!(client.database().trusted_snapshot().unwrap().version(), 2);
            assert_eq!(client.database().trusted_targets(), None);

            assert_matches!(client.update().await, Err(Error::Encoding(_)));
            assert_eq!(client.database().trusted_root().version(), 2);
            assert_eq!(client.database().trusted_timestamp().unwrap().version(), 2);
            assert_eq!(client.database().trusted_snapshot().unwrap().version(), 2);
            assert_eq!(client.database().trusted_targets().unwrap().version(), 2);

            assert_matches!(client.update().await, Ok(false));
            assert_eq!(client.database().trusted_root().version(), 2);
            assert_eq!(client.database().trusted_timestamp().unwrap().version(), 2);
            assert_eq!(client.database().trusted_snapshot().unwrap().version(), 2);
            assert_eq!(client.database().trusted_targets().unwrap().version(), 2);
        });
    }

    #[test]
    fn test_local_and_remote_repo_methods() {
        block_on(async {
            let local = EphemeralRepository::<Pouf1>::new();
            let mut remote = EphemeralRepository::<Pouf1>::new();

            let metadata1 = RepoBuilder::create(&mut remote)
                .trusted_root_keys(&[&KEYS[0]])
                .trusted_targets_keys(&[&KEYS[0]])
                .trusted_snapshot_keys(&[&KEYS[0]])
                .trusted_timestamp_keys(&[&KEYS[0]])
                .stage_root()
                .unwrap()
                .stage_targets()
                .unwrap()
                .stage_snapshot()
                .unwrap()
                .stage_timestamp_with_builder(|bld| bld.version(1))
                .unwrap()
                .commit()
                .await
                .unwrap();

            let mut client = Client::with_trusted_root(
                Config::default(),
                metadata1.root().unwrap(),
                local,
                remote,
            )
            .await
            .unwrap();

            client.update().await.unwrap();

            // Generate some new metadata.
            let metadata2 = RepoBuilder::from_database(
                &mut EphemeralRepository::<Pouf1>::new(),
                client.database(),
            )
            .trusted_root_keys(&[&KEYS[0]])
            .trusted_targets_keys(&[&KEYS[0]])
            .trusted_snapshot_keys(&[&KEYS[0]])
            .trusted_timestamp_keys(&[&KEYS[0]])
            .skip_root()
            .skip_targets()
            .skip_snapshot()
            .stage_timestamp_with_builder(|bld| bld.version(2))
            .unwrap()
            .commit()
            .await
            .unwrap();

            // Make sure we can update the local and remote store through the client.
            client
                .local_repo_mut()
                .store_metadata(
                    &MetadataPath::timestamp(),
                    MetadataVersion::None,
                    &mut metadata2.timestamp().unwrap().as_bytes(),
                )
                .await
                .unwrap();

            client
                .remote_repo_mut()
                .store_metadata(
                    &MetadataPath::timestamp(),
                    MetadataVersion::None,
                    &mut metadata2.timestamp().unwrap().as_bytes(),
                )
                .await
                .unwrap();

            // Make sure we can read it back.
            let timestamp2 =
                String::from_utf8(metadata2.timestamp().unwrap().as_bytes().to_vec()).unwrap();

            assert_eq!(
                &timestamp2,
                &fetch_metadata_to_string(
                    client.local_repo(),
                    &MetadataPath::timestamp(),
                    MetadataVersion::None,
                )
                .await
                .unwrap(),
            );

            assert_eq!(
                &timestamp2,
                &fetch_metadata_to_string(
                    client.remote_repo(),
                    &MetadataPath::timestamp(),
                    MetadataVersion::None,
                )
                .await
                .unwrap(),
            );

            // Finally, make sure we can update the database through the client as well.
            client.database_mut().update_metadata(&metadata2).unwrap();
            assert_eq!(client.database().trusted_timestamp().unwrap().version(), 2);
        })
    }

    #[test]
    fn client_can_update_with_unknown_len_and_hashes() {
        block_on(async {
            let repo = EphemeralRepository::<Pouf1>::new();

            let root = RootMetadataBuilder::new()
                .consistent_snapshot(true)
                .root_key(KEYS[0].public().clone())
                .targets_key(KEYS[1].public().clone())
                .snapshot_key(KEYS[2].public().clone())
                .timestamp_key(KEYS[3].public().clone())
                .signed::<Pouf1>(&KEYS[0])
                .unwrap()
                .to_raw()
                .unwrap();

            repo.store_metadata(
                &MetadataPath::root(),
                MetadataVersion::Number(1),
                &mut root.as_bytes(),
            )
            .await
            .unwrap();

            let targets = TargetsMetadataBuilder::new()
                .signed::<Pouf1>(&KEYS[1])
                .unwrap()
                .to_raw()
                .unwrap();

            repo.store_metadata(
                &MetadataPath::targets(),
                MetadataVersion::Number(1),
                &mut targets.as_bytes(),
            )
            .await
            .unwrap();

            // Create a targets metadata description, and deliberately don't set the metadata length
            // or hashes.
            let targets_description = MetadataDescription::new(1, None, HashMap::new()).unwrap();

            let snapshot = SnapshotMetadataBuilder::new()
                .insert_metadata_description(MetadataPath::targets(), targets_description)
                .signed::<Pouf1>(&KEYS[2])
                .unwrap()
                .to_raw()
                .unwrap();

            repo.store_metadata(
                &MetadataPath::snapshot(),
                MetadataVersion::Number(1),
                &mut snapshot.as_bytes(),
            )
            .await
            .unwrap();

            // Create a snapshot metadata description, and deliberately don't set the metadata length
            // or hashes.
            let snapshot_description = MetadataDescription::new(1, None, HashMap::new()).unwrap();

            let timestamp =
                TimestampMetadataBuilder::from_metadata_description(snapshot_description)
                    .signed::<Pouf1>(&KEYS[3])
                    .unwrap()
                    .to_raw()
                    .unwrap();

            repo.store_metadata(
                &MetadataPath::timestamp(),
                MetadataVersion::None,
                &mut timestamp.as_bytes(),
            )
            .await
            .unwrap();

            let mut client = Client::with_trusted_root_keys(
                Config::default(),
                MetadataVersion::Number(1),
                1,
                once(&KEYS[0].public().clone()),
                EphemeralRepository::new(),
                repo,
            )
            .await
            .unwrap();

            assert_matches!(client.update().await, Ok(true));
        })
    }
}
