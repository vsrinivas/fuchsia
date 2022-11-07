//! Components needed to verify TUF metadata and targets.

use chrono::{offset::Utc, DateTime};
use std::cmp::Ordering;
use std::collections::{HashMap, HashSet};
use std::marker::PhantomData;

use crate::crypto::PublicKey;
use crate::error::Error;
use crate::metadata::{
    Delegations, Metadata, MetadataPath, MetadataVersion, RawSignedMetadata, RawSignedMetadataSet,
    RootMetadata, SnapshotMetadata, TargetDescription, TargetPath, TargetsMetadata,
    TimestampMetadata,
};
use crate::pouf::Pouf;
use crate::verify::{self, Verified};
use crate::Result;

/// Contains trusted TUF metadata and can be used to verify other metadata and targets.
#[derive(Debug)]
pub struct Database<D: Pouf> {
    trusted_root: Verified<RootMetadata>,
    trusted_targets: Option<Verified<TargetsMetadata>>,
    trusted_snapshot: Option<Verified<SnapshotMetadata>>,
    trusted_timestamp: Option<Verified<TimestampMetadata>>,
    trusted_delegations: HashMap<MetadataPath, Verified<TargetsMetadata>>,
    pouf: PhantomData<D>,
}

impl<D: Pouf> Database<D> {
    /// Create a new [`Database`] struct from a set of trusted root keys that are used to verify
    /// the signed metadata. The signed root metadata must be signed with at least a
    /// `root_threshold` of the provided root_keys. It is not necessary for the root metadata to
    /// contain these keys.
    pub fn from_root_with_trusted_keys<'a, I>(
        raw_root: &RawSignedMetadata<D, RootMetadata>,
        root_threshold: u32,
        root_keys: I,
    ) -> Result<Self>
    where
        I: IntoIterator<Item = &'a PublicKey>,
    {
        let verified_root = {
            // Make sure the keys signed the root.
            let new_root = verify::verify_signatures(
                &MetadataPath::root(),
                raw_root,
                root_threshold,
                root_keys,
            )?;

            // Make sure the root signed itself.
            verify::verify_signatures(
                &MetadataPath::root(),
                raw_root,
                new_root.root().threshold(),
                new_root.keys().iter().filter_map(|(k, v)| {
                    if new_root.root().key_ids().contains(k) {
                        Some(v)
                    } else {
                        None
                    }
                }),
            )?
        };

        Ok(Database {
            trusted_root: verified_root,
            trusted_snapshot: None,
            trusted_targets: None,
            trusted_timestamp: None,
            trusted_delegations: HashMap::new(),
            pouf: PhantomData,
        })
    }

    /// Create a new [`Database`] struct from a piece of metadata that is assumed to be trusted.
    ///
    /// **WARNING**: This is trust-on-first-use (TOFU) and offers weaker security guarantees than
    /// the related method [`Database::from_root_with_trusted_keys`] because this method needs to
    /// deserialize `raw_root` before we have verified it has been signed properly. This exposes us
    /// to potential parser exploits. This method should only be used if the metadata is loaded from
    /// a trusted source.
    pub fn from_trusted_root(raw_root: &RawSignedMetadata<D, RootMetadata>) -> Result<Self> {
        let verified_root = {
            // **WARNING**: By deserializing the metadata before verification, we are exposing us
            // to parser exploits.
            let unverified_root = raw_root.parse_untrusted()?.assume_valid()?;

            // Make sure the root signed itself.
            verify::verify_signatures(
                &MetadataPath::root(),
                raw_root,
                unverified_root.root().threshold(),
                unverified_root.root_keys(),
            )?
        };

        Ok(Database {
            trusted_root: verified_root,
            trusted_snapshot: None,
            trusted_targets: None,
            trusted_timestamp: None,
            trusted_delegations: HashMap::new(),
            pouf: PhantomData,
        })
    }

    /// Create a new [`Database`] struct from a set of metadata that is assumed to be trusted. The
    /// signed root metadata in the `metadata_set` must be signed with at least a `root_threshold`
    /// of the provided root_keys. It is not necessary for the root metadata to contain these keys.
    pub fn from_metadata_with_trusted_keys<'a, I>(
        metadata_set: &RawSignedMetadataSet<D>,
        root_threshold: u32,
        root_keys: I,
    ) -> Result<Self>
    where
        I: IntoIterator<Item = &'a PublicKey>,
    {
        Self::from_metadata_with_trusted_keys_and_start_time(
            &Utc::now(),
            metadata_set,
            root_threshold,
            root_keys,
        )
    }

    /// Create a new [`Database`] struct from a set of metadata that is assumed to be trusted. The
    /// signed root metadata in the `metadata_set` must be signed with at least a `root_threshold`
    /// of the provided root_keys. It is not necessary for the root metadata to contain these keys.
    pub fn from_metadata_with_trusted_keys_and_start_time<'a, I>(
        start_time: &DateTime<Utc>,
        metadata_set: &RawSignedMetadataSet<D>,
        root_threshold: u32,
        root_keys: I,
    ) -> Result<Self>
    where
        I: IntoIterator<Item = &'a PublicKey>,
    {
        let mut db = if let Some(root) = metadata_set.root() {
            Database::from_root_with_trusted_keys(root, root_threshold, root_keys)?
        } else {
            return Err(Error::MetadataNotFound {
                path: MetadataPath::root(),
                version: MetadataVersion::None,
            });
        };

        db.update_metadata_after_root(start_time, metadata_set)?;

        Ok(db)
    }

    /// Create a new [`Database`] struct from a set of metadata that is assumed to be trusted.
    ///
    /// **WARNING**: This is trust-on-first-use (TOFU) and offers weaker security guarantees than
    /// the related method [`Database::from_metadata_with_trusted_keys`] because this method needs
    /// to deserialize the root metadata from `metadata_set` before we have verified it has been
    /// signed properly. This exposes us to potential parser exploits. This method should only be
    /// used if the metadata is loaded from a trusted source.
    pub fn from_trusted_metadata(metadata_set: &RawSignedMetadataSet<D>) -> Result<Self> {
        Self::from_trusted_metadata_with_start_time(metadata_set, &Utc::now())
    }

    /// Create a new [`Database`] struct from a set of metadata that is assumed to be trusted.
    ///
    /// **WARNING**: This is trust-on-first-use (TOFU) and offers weaker security guarantees than
    /// the related method [`Database::from_metadata_with_trusted_keys`] because this method needs
    /// to deserialize the root metadata from `metadata_set` before we have verified it has been
    /// signed properly. This exposes us to potential parser exploits. This method should only be
    /// used if the metadata is loaded from a trusted source.
    pub fn from_trusted_metadata_with_start_time(
        metadata_set: &RawSignedMetadataSet<D>,
        start_time: &DateTime<Utc>,
    ) -> Result<Self> {
        let mut db = if let Some(root) = metadata_set.root() {
            Database::from_trusted_root(root)?
        } else {
            return Err(Error::MetadataNotFound {
                path: MetadataPath::root(),
                version: MetadataVersion::None,
            });
        };

        db.update_metadata_after_root(start_time, metadata_set)?;

        Ok(db)
    }

    /// An immutable reference to the root metadata.
    pub fn trusted_root(&self) -> &Verified<RootMetadata> {
        &self.trusted_root
    }

    /// An immutable reference to the optional targets metadata.
    pub fn trusted_targets(&self) -> Option<&Verified<TargetsMetadata>> {
        self.trusted_targets.as_ref()
    }

    /// An immutable reference to the optional snapshot metadata.
    pub fn trusted_snapshot(&self) -> Option<&Verified<SnapshotMetadata>> {
        self.trusted_snapshot.as_ref()
    }

    /// An immutable reference to the optional timestamp metadata.
    pub fn trusted_timestamp(&self) -> Option<&Verified<TimestampMetadata>> {
        self.trusted_timestamp.as_ref()
    }

    /// An immutable reference to the delegated metadata.
    pub fn trusted_delegations(&self) -> &HashMap<MetadataPath, Verified<TargetsMetadata>> {
        &self.trusted_delegations
    }

    /// Verify and update metadata. Returns true if any of the metadata was updated.
    pub fn update_metadata(&mut self, metadata: &RawSignedMetadataSet<D>) -> Result<bool> {
        self.update_metadata_with_start_time(metadata, &Utc::now())
    }

    /// Verify and update metadata. Returns true if any of the metadata was updated.
    pub fn update_metadata_with_start_time(
        &mut self,
        metadata: &RawSignedMetadataSet<D>,
        start_time: &DateTime<Utc>,
    ) -> Result<bool> {
        let updated = if let Some(root) = metadata.root() {
            self.update_root(root)?;
            true
        } else {
            false
        };

        if self.update_metadata_after_root(start_time, metadata)? {
            Ok(true)
        } else {
            Ok(updated)
        }
    }

    fn update_metadata_after_root(
        &mut self,
        start_time: &DateTime<Utc>,
        metadata_set: &RawSignedMetadataSet<D>,
    ) -> Result<bool> {
        let mut updated = false;
        if let Some(timestamp) = metadata_set.timestamp() {
            if self.update_timestamp(start_time, timestamp)?.is_some() {
                updated = true;
            }
        }

        if let Some(snapshot) = metadata_set.snapshot() {
            if self.update_snapshot(start_time, snapshot)? {
                updated = true;
            }
        }

        if let Some(targets) = metadata_set.targets() {
            if self.update_targets(start_time, targets)? {
                updated = true;
            }
        }

        Ok(updated)
    }

    /// Verify and update the root metadata.
    pub fn update_root(&mut self, raw_root: &RawSignedMetadata<D, RootMetadata>) -> Result<()> {
        let verified = {
            let trusted_root = &self.trusted_root;

            /////////////////////////////////////////
            // TUF-1.0.5 §5.1.3:
            //
            //     Check signatures. Version N+1 of the root metadata file MUST have been signed
            //     by: (1) a threshold of keys specified in the trusted root metadata file (version
            //     N), and (2) a threshold of keys specified in the new root metadata file being
            //     validated (version N+1). If version N+1 is not signed as required, discard it,
            //     abort the update cycle, and report the signature failure. On the next update
            //     cycle, begin at step 0 and version N of the root metadata file.  Verify the
            //     trusted root signed the new root.
            let new_root = verify::verify_signatures(
                &MetadataPath::root(),
                raw_root,
                trusted_root.root().threshold(),
                trusted_root.root_keys(),
            )?;

            // Verify the new root signed itself.
            let new_root = verify::verify_signatures(
                &MetadataPath::root(),
                raw_root,
                new_root.root().threshold(),
                new_root.root_keys(),
            )?;

            /////////////////////////////////////////
            // TUF-1.0.5 §5.1.4:
            //
            //     Check for a rollback attack. The version number of the trusted root metadata
            //     file (version N) must be less than or equal to the version number of the new
            //     root metadata file (version N+1). Effectively, this means checking that the
            //     version number signed in the new root metadata file is indeed N+1. If the
            //     version of the new root metadata file is less than the trusted metadata file,
            //     discard it, abort the update cycle, and report the rollback attack. On the next
            //     update cycle, begin at step 0 and version N of the root metadata file.

            let next_root_version = trusted_root.version().checked_add(1).ok_or_else(|| {
                Error::MetadataVersionMustBeSmallerThanMaxU32(MetadataPath::root())
            })?;

            if new_root.version() != next_root_version {
                return Err(Error::AttemptedMetadataRollBack {
                    role: MetadataPath::root(),
                    trusted_version: trusted_root.version(),
                    new_version: new_root.version(),
                });
            }

            /////////////////////////////////////////
            // TUF-1.0.5 §5.1.5:
            //
            //     Note that the expiration of the new (intermediate) root metadata file does not matter yet, because we will check for it in step 1.8.

            /////////////////////////////////////////
            // TUF-1.0.5 §5.1.8:
            //
            //     Check for a freeze attack. The latest known time should be lower than the
            //     expiration timestamp in the trusted root metadata file (version N). If the
            //     trusted root metadata file has expired, abort the update cycle, report the
            //     potential freeze attack. On the next update cycle, begin at step 0 and version N
            //     of the root metadata file.

            // FIXME: root metadata expiration is performed in Client. We should restructure things
            // such that it is performed here.

            new_root
        };

        /////////////////////////////////////////
        // TUF-1.0.5 §5.1.9:
        //
        //     If the timestamp and / or snapshot keys have been rotated, then delete the
        //     trusted timestamp and snapshot metadata files. This is done in order to recover
        //     from fast-forward attacks after the repository has been compromised and
        //     recovered. A fast-forward attack happens when attackers arbitrarily increase the
        //     version numbers of: (1) the timestamp metadata, (2) the snapshot metadata, and /
        //     or (3) the targets, or a delegated targets, metadata file in the snapshot
        //     metadata. Please see the Mercury paper for more details.

        self.purge_metadata();

        /////////////////////////////////////////
        // TUF-1.0.5 §5.1.6:
        //
        //     1.6. Set the trusted root metadata file to the new root metadata file.

        self.trusted_root = verified;

        Ok(())
    }

    /// Verify and update the timestamp metadata.
    ///
    /// Returns a reference to the parsed metadata if the metadata was newer.
    pub fn update_timestamp(
        &mut self,
        start_time: &DateTime<Utc>,
        raw_timestamp: &RawSignedMetadata<D, TimestampMetadata>,
    ) -> Result<Option<&Verified<TimestampMetadata>>> {
        let verified = {
            // FIXME(https://github.com/theupdateframework/specification/issues/113) Should we
            // check if the root metadata is expired here? We do that in the other `Database::update_*`
            // methods, but not here.
            let trusted_root = &self.trusted_root;

            /////////////////////////////////////////
            // TUF-1.0.5 §5.2.1:
            //
            //     Check signatures. The new timestamp metadata file must have been signed by a
            //     threshold of keys specified in the trusted root metadata file. If the new
            //     timestamp metadata file is not properly signed, discard it, abort the update
            //     cycle, and report the signature failure.

            let new_timestamp = verify::verify_signatures(
                &MetadataPath::timestamp(),
                raw_timestamp,
                trusted_root.timestamp().threshold(),
                trusted_root.timestamp_keys(),
            )?;

            /////////////////////////////////////////
            // TUF-1.0.5 §5.2.2: Check for a rollback attack.

            /////////////////////////////////////////
            // TUF-1.0.5 §5.2.2.1:
            //
            //     The version number of the trusted timestamp metadata file, if any, must be less
            //     than or equal to the version number of the new timestamp metadata file. If the
            //     new timestamp metadata file is older than the trusted timestamp metadata file,
            //     discard it, abort the update cycle, and report the potential rollback attack.

            if let Some(trusted_timestamp) = &self.trusted_timestamp {
                match new_timestamp.version().cmp(&trusted_timestamp.version()) {
                    Ordering::Less => {
                        return Err(Error::AttemptedMetadataRollBack {
                            role: MetadataPath::timestamp(),
                            trusted_version: trusted_timestamp.version(),
                            new_version: new_timestamp.version(),
                        });
                    }
                    Ordering::Equal => {
                        return Ok(None);
                    }
                    Ordering::Greater => {}
                }
            }

            /////////////////////////////////////////
            // TUF-1.0.5 §5.2.2.2:
            //
            //     The version number of the snapshot metadata file in the trusted timestamp
            //     metadata file, if any, MUST be less than or equal to its version number in the
            //     new timestamp metadata file. If not, discard the new timestamp metadadata file,
            //     abort the update cycle, and report the failure.

            // FIXME(#294): Implement this section.

            /////////////////////////////////////////
            // FIXME(#297): forgetting the trusted snapshot here is not part of the spec. Do we need to
            // do it?

            if let Some(trusted_snapshot) = &self.trusted_snapshot {
                if trusted_snapshot.version() != new_timestamp.snapshot().version() {
                    self.trusted_snapshot = None;
                }
            }

            /////////////////////////////////////////
            // TUF-1.0.5 §5.2.3:
            //
            //     Check for a freeze attack. The latest known time should be lower than the
            //     expiration timestamp in the new timestamp metadata file. If so, the new
            //     timestamp metadata file becomes the trusted timestamp metadata file. If the new
            //     timestamp metadata file has expired, discard it, abort the update cycle, and
            //     report the potential freeze attack.

            if new_timestamp.expires() <= start_time {
                return Err(Error::ExpiredMetadata(MetadataPath::timestamp()));
            }

            new_timestamp
        };

        self.trusted_timestamp = Some(verified);
        Ok(self.trusted_timestamp.as_ref())
    }

    /// Verify and update the snapshot metadata.
    pub fn update_snapshot(
        &mut self,
        start_time: &DateTime<Utc>,
        raw_snapshot: &RawSignedMetadata<D, SnapshotMetadata>,
    ) -> Result<bool> {
        let verified = {
            /////////////////////////////////////////
            // FIXME(https://github.com/theupdateframework/specification/issues/113) Checking if
            // this metadata expired isn't part of the spec. Do we actually want to do this?
            let trusted_root = self.trusted_root_unexpired(start_time)?;
            let trusted_timestamp = self.trusted_timestamp_unexpired(start_time)?;

            if let Some(trusted_snapshot) = &self.trusted_snapshot {
                match trusted_timestamp
                    .snapshot()
                    .version()
                    .cmp(&trusted_snapshot.version())
                {
                    Ordering::Less => {
                        return Err(Error::AttemptedMetadataRollBack {
                            role: MetadataPath::snapshot(),
                            trusted_version: trusted_snapshot.version(),
                            new_version: trusted_timestamp.snapshot().version(),
                        });
                    }
                    Ordering::Equal => {
                        return Ok(false);
                    }
                    Ordering::Greater => {}
                }
            }

            /////////////////////////////////////////
            // TUF-1.0.5 §5.3.1:
            //
            //     Check against timestamp metadata. The hashes and version number of the new
            //     snapshot metadata file MUST match the hashes (if any) and version number listed
            //     in the trusted timestamp metadata. If hashes and version do not match, discard
            //     the new snapshot metadata, abort the update cycle, and report the failure.

            // FIXME: rust-tuf checks the hash during download, but it would be better if we
            // checked the hash here to make it easier to validate we've correctly implemented the
            // spec.

            // NOTE(https://github.com/theupdateframework/specification/pull/112): Technically
            // we're supposed to check the version before checking the signature, but we do it
            // afterwards. That PR proposes formally moving the version check to after signature
            // verification.

            /////////////////////////////////////////
            // TUF-1.0.5 §5.3.2:
            //
            //     The new snapshot metadata file MUST have been signed by a threshold of keys
            //     specified in the trusted root metadata file. If the new snapshot metadata file
            //     is not signed as required, discard it, abort the update cycle, and report the
            //     signature failure.

            let new_snapshot = verify::verify_signatures(
                &MetadataPath::snapshot(),
                raw_snapshot,
                trusted_root.snapshot().threshold(),
                trusted_root.snapshot_keys(),
            )?;

            /////////////////////////////////////////
            // FIXME(https://github.com/theupdateframework/specification/pull/112): Actually check
            // the version.

            if new_snapshot.version() != trusted_timestamp.snapshot().version() {
                return Err(Error::WrongMetadataVersion {
                    parent_role: MetadataPath::timestamp(),
                    child_role: MetadataPath::snapshot(),
                    expected_version: trusted_timestamp.snapshot().version(),
                    new_version: new_snapshot.version(),
                });
            }

            /////////////////////////////////////////
            // TUF-1.0.5 §5.3.3: Check for a rollback attack.

            /////////////////////////////////////////
            // TUF-1.0.5 §5.3.3.1:
            //
            //     The version number of the trusted snapshot metadata file, if any, MUST be less
            //     than or equal to the version number of the new snapshot metadata file. If the
            //     new snapshot metadata file is older than the trusted metadata file, discard it,
            //     abort the update cycle, and report the potential rollback attack.

            if let Some(trusted_snapshot) = &self.trusted_snapshot {
                if new_snapshot.version() < trusted_snapshot.version() {
                    return Err(Error::AttemptedMetadataRollBack {
                        role: MetadataPath::snapshot(),
                        trusted_version: trusted_snapshot.version(),
                        new_version: new_snapshot.version(),
                    });
                }
            }

            /////////////////////////////////////////
            // TUF-1.0.5 §5.3.3.2:
            //
            //     The version number of the targets metadata file, and all delegated targets
            //     metadata files (if any), in the trusted snapshot metadata file, if any, MUST be
            //     less than or equal to its version number in the new snapshot metadata file.
            //     Furthermore, any targets metadata filename that was listed in the trusted
            //     snapshot metadata file, if any, MUST continue to be listed in the new snapshot
            //     metadata file. If any of these conditions are not met, discard the new snapshot
            //     metadadata file, abort the update cycle, and report the failure.

            // FIXME(#295): Implement this section.

            /////////////////////////////////////////
            // TUF-1.0.5 §5.3.4:
            //
            //     Check for a freeze attack. The latest known time should be lower than the
            //     expiration timestamp in the new snapshot metadata file. If so, the new snapshot
            //     metadata file becomes the trusted snapshot metadata file. If the new snapshot
            //     metadata file is expired, discard it, abort the update cycle, and report the
            //     potential freeze attack.

            /////////////////////////////////////////
            // FIXME(#297): Verify why we don't check expiration here:
            // Note: this doesn't check the expiration because we need to be able to update it
            // regardless so we can prevent rollback attacks againsts targets/delegations.

            new_snapshot
        };

        // FIXME(#297): purging targets is not part of the spec. Do we need to do it?
        if self
            .trusted_targets
            .as_ref()
            .map(|s| s.version())
            .unwrap_or(0)
            != verified
                .meta()
                .get(&MetadataPath::targets())
                .map(|m| m.version())
                .unwrap_or(0)
        {
            self.trusted_targets = None;
        }

        self.trusted_snapshot = Some(verified);

        // FIXME(#297): purging delegates is not part of the spec. Do we need to do it?
        self.purge_delegations();

        Ok(true)
    }

    fn purge_delegations(&mut self) {
        let purge = {
            let trusted_snapshot = match self.trusted_snapshot() {
                Some(s) => s,
                None => return,
            };
            let mut purge = HashSet::new();
            for (role, trusted_definition) in trusted_snapshot.meta().iter() {
                let trusted_delegation = match self.trusted_delegations.get(role) {
                    Some(d) => d,
                    None => continue,
                };

                if trusted_delegation.version() > trusted_definition.version() {
                    let _ = purge.insert(role.clone());
                    continue;
                }
            }

            purge
        };

        for role in &purge {
            let _ = self.trusted_delegations.remove(role);
        }
    }

    /// Verify and update the targets metadata.
    pub fn update_targets(
        &mut self,
        start_time: &DateTime<Utc>,
        raw_targets: &RawSignedMetadata<D, TargetsMetadata>,
    ) -> Result<bool> {
        let verified = {
            // FIXME(https://github.com/theupdateframework/specification/issues/113) Checking if
            // this metadata expired isn't part of the spec. Do we actually want to do this?
            let trusted_root = self.trusted_root_unexpired(start_time)?;
            let trusted_targets_version = self.trusted_targets.as_ref().map(|t| t.version());

            self.verify_target_or_delegated_target(
                start_time,
                &MetadataPath::targets(),
                raw_targets,
                trusted_root.targets().threshold(),
                trusted_root.targets_keys(),
                trusted_targets_version,
            )?
        };

        if let Some(verified) = verified {
            self.trusted_targets = Some(verified);
            Ok(true)
        } else {
            Ok(false)
        }
    }

    /// Verify and update a delegation metadata.
    pub fn update_delegated_targets(
        &mut self,
        start_time: &DateTime<Utc>,
        parent_role: &MetadataPath,
        role: &MetadataPath,
        raw_delegated_targets: &RawSignedMetadata<D, TargetsMetadata>,
    ) -> Result<bool> {
        let verified = {
            // FIXME(https://github.com/theupdateframework/specification/issues/113) Checking if
            // this metadata expired isn't part of the spec. Do we actually want to do this?
            let _ = self.trusted_root_unexpired(start_time)?;
            let _ = self.trusted_snapshot_unexpired(start_time)?;
            let trusted_targets = self.trusted_targets_unexpired(start_time)?;

            if trusted_targets.delegations().is_empty() {
                return Err(Error::UnauthorizedDelegation {
                    parent_role: parent_role.clone(),
                    child_role: role.clone(),
                });
            };

            let (threshold, keys) = self
                .find_delegation_threshold_and_keys(parent_role, role)?
                .ok_or_else(|| Error::UnauthorizedDelegation {
                    parent_role: parent_role.clone(),
                    child_role: role.clone(),
                })?;

            let trusted_delegated_targets_version =
                self.trusted_delegations.get(role).map(|t| t.version());

            self.verify_target_or_delegated_target(
                start_time,
                role,
                raw_delegated_targets,
                threshold,
                keys.into_iter(),
                trusted_delegated_targets_version,
            )?
        };

        if let Some(verified) = verified {
            let _ = self.trusted_delegations.insert(role.clone(), verified);
            Ok(true)
        } else {
            Ok(false)
        }
    }

    fn verify_target_or_delegated_target<'a>(
        &self,
        start_time: &DateTime<Utc>,
        role: &MetadataPath,
        raw_targets: &RawSignedMetadata<D, TargetsMetadata>,
        trusted_targets_threshold: u32,
        trusted_targets_keys: impl Iterator<Item = &'a PublicKey>,
        trusted_targets_version: Option<u32>,
    ) -> Result<Option<Verified<TargetsMetadata>>> {
        // FIXME(https://github.com/theupdateframework/specification/issues/113) Checking if
        // this metadata expired isn't part of the spec. Do we actually want to do this?
        let trusted_snapshot = self.trusted_snapshot_unexpired(start_time)?;

        let trusted_targets_description =
            trusted_snapshot
                .meta()
                .get(role)
                .ok_or_else(|| Error::MissingMetadataDescription {
                    parent_role: MetadataPath::snapshot(),
                    child_role: role.clone(),
                })?;

        /////////////////////////////////////////
        // TUF-1.0.5 §5.4.1:
        //
        //     Check against snapshot metadata. The hashes and version number of the new
        //     targets metadata file MUST match the hashes (if any) and version number listed
        //     in the trusted snapshot metadata. This is done, in part, to prevent a
        //     mix-and-match attack by man-in-the-middle attackers. If the new targets metadata
        //     file does not match, discard it, abort the update cycle, and report the failure.

        // FIXME: rust-tuf checks the hash during download, but it would be better if we
        // checked the hash here to make it easier to validate we've correctly implemented the
        // spec.

        // NOTE(https://github.com/theupdateframework/specification/pull/112): Technically
        // we're supposed to check the version before checking the signature, but we do it
        // afterwards. That PR proposes formally moving the version check to after signature
        // verification.

        /////////////////////////////////////////
        // TUF-1.0.5 §5.4.2:
        //
        //     Check for an arbitrary software attack. The new targets metadata file MUST have
        //     been signed by a threshold of keys specified in the trusted root metadata file.
        //     If the new targets metadata file is not signed as required, discard it, abort
        //     the update cycle, and report the failure.

        let new_targets = verify::verify_signatures(
            role,
            raw_targets,
            trusted_targets_threshold,
            trusted_targets_keys,
        )?;

        /////////////////////////////////////////
        // FIXME(https://github.com/theupdateframework/specification/pull/112): Actually check
        // the version.

        // FIXME(#295): TUF-1.0.5 §5.3.3.2 says this check should be done when updating the
        // snapshot, not here.
        if new_targets.version() != trusted_targets_description.version() {
            return Err(Error::WrongMetadataVersion {
                parent_role: MetadataPath::snapshot(),
                child_role: role.clone(),
                expected_version: trusted_targets_description.version(),
                new_version: new_targets.version(),
            });
        }

        if let Some(trusted_targets_version) = trusted_targets_version {
            match new_targets.version().cmp(&trusted_targets_version) {
                Ordering::Less => {
                    return Err(Error::AttemptedMetadataRollBack {
                        role: role.clone(),
                        trusted_version: trusted_targets_version,
                        new_version: new_targets.version(),
                    });
                }
                Ordering::Equal => {
                    return Ok(None);
                }
                Ordering::Greater => {}
            }
        }

        /////////////////////////////////////////
        // TUF-1.0.5 §5.4.3:
        //
        //     Check for a freeze attack. The latest known time should be lower than the
        //     expiration timestamp in the new targets metadata file. If so, the new targets
        //     metadata file becomes the trusted targets metadata file. If the new targets
        //     metadata file is expired, discard it, abort the update cycle, and report the
        //     potential freeze attack.

        if new_targets.expires() <= start_time {
            return Err(Error::ExpiredMetadata(role.clone()));
        }

        Ok(Some(new_targets))
    }

    /// Find the signing keys and metadata for the delegation given by `role`, as seen from the
    /// point of view of `parent_role`.
    fn find_delegation_threshold_and_keys(
        &self,
        parent_role: &MetadataPath,
        role: &MetadataPath,
    ) -> Result<Option<(u32, Vec<&PublicKey>)>> {
        // Find the parent TargetsMetadata that is expected to refer to `role`.
        let trusted_parent = if parent_role == &MetadataPath::targets() {
            if let Some(trusted_targets) = self.trusted_targets() {
                trusted_targets
            } else {
                return Err(Error::MetadataNotFound {
                    path: parent_role.clone(),
                    version: MetadataVersion::None,
                });
            }
        } else if let Some(trusted_parent) = self.trusted_delegations.get(parent_role) {
            trusted_parent
        } else {
            return Err(Error::MetadataNotFound {
                path: parent_role.clone(),
                version: MetadataVersion::None,
            });
        };

        // Only consider targets metadata that define delegations.
        let trusted_delegations = trusted_parent.delegations();

        for trusted_delegation in trusted_delegations.roles() {
            if trusted_delegation.name() != role {
                continue;
            }

            // Filter the delegations keys to just the ones for this delegation.
            let authorized_keys = trusted_delegations
                .keys()
                .iter()
                .filter_map(|(k, v)| {
                    if trusted_delegation.key_ids().contains(k) {
                        Some(v)
                    } else {
                        None
                    }
                })
                .collect();

            return Ok(Some((trusted_delegation.threshold(), authorized_keys)));
        }

        Ok(None)
    }

    /// Get a reference to the description needed to verify the target defined by the given
    /// `TargetPath`. Returns an `Error` if the target is not defined in the trusted
    /// metadata. This may mean the target exists somewhere in the metadata, but the chain of trust
    /// to that target may be invalid or incomplete.
    pub fn target_description(&self, target_path: &TargetPath) -> Result<TargetDescription> {
        self.target_description_with_start_time(&Utc::now(), target_path)
    }

    /// Get a reference to the description needed to verify the target defined by the given
    /// `TargetPath`. Returns an `Error` if the target is not defined in the trusted
    /// metadata. This may mean the target exists somewhere in the metadata, but the chain of trust
    /// to that target may be invalid or incomplete.
    pub fn target_description_with_start_time(
        &self,
        start_time: &DateTime<Utc>,
        target_path: &TargetPath,
    ) -> Result<TargetDescription> {
        let _ = self.trusted_root_unexpired(start_time)?;
        let _ = self.trusted_snapshot_unexpired(start_time)?;
        let targets = self.trusted_targets_unexpired(start_time)?;

        if let Some(d) = targets.targets().get(target_path) {
            return Ok(d.clone());
        }

        fn lookup<'a, D: Pouf>(
            start_time: &DateTime<Utc>,
            tuf: &'a Database<D>,
            default_terminate: bool,
            current_depth: u32,
            target_path: &TargetPath,
            delegations: &'a Delegations,
            parents: &[HashSet<TargetPath>],
            visited: &mut HashSet<&'a MetadataPath>,
        ) -> (bool, Option<TargetDescription>) {
            for delegation in delegations.roles() {
                if visited.contains(delegation.name()) {
                    return (delegation.terminating(), None);
                }
                let _ = visited.insert(delegation.name());

                let mut new_parents = parents.to_owned();
                new_parents.push(delegation.paths().clone());

                if current_depth > 0 && !target_path.matches_chain(parents) {
                    return (delegation.terminating(), None);
                }

                let trusted_delegation = match tuf.trusted_delegations.get(delegation.name()) {
                    Some(trusted_delegation) => trusted_delegation,
                    None => return (delegation.terminating(), None),
                };

                if trusted_delegation.expires() <= start_time {
                    return (delegation.terminating(), None);
                }

                if let Some(target) = trusted_delegation.targets().get(target_path) {
                    return (delegation.terminating(), Some(target.clone()));
                }

                let trusted_child_delegations = trusted_delegation.delegations();

                // We only need to check the child delegations if it delegates to any child roles.
                if !trusted_child_delegations.roles().is_empty() {
                    let mut new_parents = parents.to_vec();
                    new_parents.push(delegation.paths().clone());
                    let (term, res) = lookup(
                        start_time,
                        tuf,
                        delegation.terminating(),
                        current_depth + 1,
                        target_path,
                        trusted_child_delegations,
                        &new_parents,
                        visited,
                    );
                    if term {
                        return (true, res);
                    } else if res.is_some() {
                        return (term, res);
                    }
                }
            }
            (default_terminate, None)
        }

        let delegations = targets.delegations();
        if delegations.roles().is_empty() {
            Err(Error::TargetNotFound(target_path.clone()))
        } else {
            let mut visited = HashSet::new();
            lookup(
                start_time,
                self,
                false,
                0,
                target_path,
                delegations,
                &[],
                &mut visited,
            )
            .1
            .ok_or_else(|| Error::TargetNotFound(target_path.clone()))
        }
    }

    fn purge_metadata(&mut self) {
        self.trusted_snapshot = None;
        self.trusted_targets = None;
        self.trusted_timestamp = None;
        self.trusted_delegations.clear();
    }

    fn trusted_root_unexpired(&self, start_time: &DateTime<Utc>) -> Result<&RootMetadata> {
        let trusted_root = &self.trusted_root;
        if trusted_root.expires() <= start_time {
            return Err(Error::ExpiredMetadata(MetadataPath::root()));
        }
        Ok(trusted_root)
    }

    fn trusted_timestamp_unexpired(
        &self,
        start_time: &DateTime<Utc>,
    ) -> Result<&TimestampMetadata> {
        match self.trusted_timestamp {
            Some(ref trusted_timestamp) => {
                if trusted_timestamp.expires() <= start_time {
                    return Err(Error::ExpiredMetadata(MetadataPath::timestamp()));
                }
                Ok(trusted_timestamp)
            }
            None => Err(Error::MetadataNotFound {
                path: MetadataPath::timestamp(),
                version: MetadataVersion::None,
            }),
        }
    }

    fn trusted_snapshot_unexpired(&self, start_time: &DateTime<Utc>) -> Result<&SnapshotMetadata> {
        match self.trusted_snapshot {
            Some(ref trusted_snapshot) => {
                if trusted_snapshot.expires() <= start_time {
                    return Err(Error::ExpiredMetadata(MetadataPath::snapshot()));
                }
                Ok(trusted_snapshot)
            }
            None => Err(Error::MetadataNotFound {
                path: MetadataPath::snapshot(),
                version: MetadataVersion::None,
            }),
        }
    }

    fn trusted_targets_unexpired(&self, start_time: &DateTime<Utc>) -> Result<&TargetsMetadata> {
        match self.trusted_targets {
            Some(ref trusted_targets) => {
                if trusted_targets.expires() <= start_time {
                    return Err(Error::ExpiredMetadata(MetadataPath::targets()));
                }
                Ok(trusted_targets)
            }
            None => Err(Error::MetadataNotFound {
                path: MetadataPath::targets(),
                version: MetadataVersion::None,
            }),
        }
    }
}

impl<D: Pouf> Clone for Database<D> {
    fn clone(&self) -> Self {
        Self {
            trusted_root: self.trusted_root.clone(),
            trusted_targets: self.trusted_targets.clone(),
            trusted_snapshot: self.trusted_snapshot.clone(),
            trusted_timestamp: self.trusted_timestamp.clone(),
            trusted_delegations: self.trusted_delegations.clone(),
            pouf: PhantomData,
        }
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::crypto::{Ed25519PrivateKey, HashAlgorithm, PrivateKey};
    use crate::metadata::{
        RawSignedMetadataSetBuilder, RootMetadataBuilder, SnapshotMetadataBuilder,
        TargetsMetadataBuilder, TimestampMetadataBuilder,
    };
    use crate::pouf::Pouf1;
    use assert_matches::assert_matches;
    use lazy_static::lazy_static;
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

    #[test]
    fn root_trusted_keys_success() {
        let root = RootMetadataBuilder::new()
            .root_key(KEYS[0].public().clone())
            .snapshot_key(KEYS[0].public().clone())
            .targets_key(KEYS[0].public().clone())
            .timestamp_key(KEYS[0].public().clone())
            .signed::<Pouf1>(&KEYS[0])
            .unwrap();
        let raw_root = root.to_raw().unwrap();

        assert_matches!(
            Database::from_root_with_trusted_keys(&raw_root, 1, once(KEYS[0].public())),
            Ok(_)
        );
    }

    #[test]
    fn root_trusted_keys_failure() {
        let root = RootMetadataBuilder::new()
            .root_key(KEYS[0].public().clone())
            .snapshot_key(KEYS[0].public().clone())
            .targets_key(KEYS[0].public().clone())
            .timestamp_key(KEYS[0].public().clone())
            .signed::<Pouf1>(&KEYS[0])
            .unwrap();
        let raw_root = root.to_raw().unwrap();

        assert_matches!(
            Database::from_root_with_trusted_keys(&raw_root, 1, once(KEYS[1].public())),
            Err(Error::MetadataMissingSignatures {
                role,
                number_of_valid_signatures: 0,
                threshold: 1,
            })
            if role == MetadataPath::root()
        );
    }

    #[test]
    fn from_trusted_metadata_success() {
        let root = RootMetadataBuilder::new()
            .root_key(KEYS[0].public().clone())
            .snapshot_key(KEYS[0].public().clone())
            .targets_key(KEYS[0].public().clone())
            .timestamp_key(KEYS[0].public().clone())
            .signed::<Pouf1>(&KEYS[0])
            .unwrap()
            .to_raw()
            .unwrap();

        let metadata = RawSignedMetadataSetBuilder::new().root(root).build();

        assert_matches!(Database::from_trusted_metadata(&metadata), Ok(_));
    }

    #[test]
    fn from_trusted_metadata_failure() {
        let root = RootMetadataBuilder::new()
            .root_key(KEYS[0].public().clone())
            .snapshot_key(KEYS[0].public().clone())
            .targets_key(KEYS[0].public().clone())
            .timestamp_key(KEYS[0].public().clone())
            .signed::<Pouf1>(&KEYS[1])
            .unwrap()
            .to_raw()
            .unwrap();

        let metadata = RawSignedMetadataSetBuilder::new().root(root).build();

        assert_matches!(
            Database::from_trusted_metadata(&metadata),
            Err(Error::MetadataMissingSignatures {
                role,
                number_of_valid_signatures: 0,
                threshold: 1,
            })
            if role == MetadataPath::root()
        );
    }

    #[test]
    fn from_metadata_with_trusted_keys_success() {
        let root = RootMetadataBuilder::new()
            .root_key(KEYS[0].public().clone())
            .snapshot_key(KEYS[0].public().clone())
            .targets_key(KEYS[0].public().clone())
            .timestamp_key(KEYS[0].public().clone())
            .signed::<Pouf1>(&KEYS[0])
            .unwrap()
            .to_raw()
            .unwrap();

        let metadata = RawSignedMetadataSetBuilder::new().root(root).build();

        assert_matches!(
            Database::from_metadata_with_trusted_keys(&metadata, 1, once(KEYS[0].public())),
            Ok(_)
        );
    }

    #[test]
    fn from_metadata_with_trusted_keys_failure() {
        let root = RootMetadataBuilder::new()
            .root_key(KEYS[0].public().clone())
            .snapshot_key(KEYS[0].public().clone())
            .targets_key(KEYS[0].public().clone())
            .timestamp_key(KEYS[0].public().clone())
            .signed::<Pouf1>(&KEYS[0])
            .unwrap()
            .to_raw()
            .unwrap();

        let metadata = RawSignedMetadataSetBuilder::new().root(root).build();

        assert_matches!(
            Database::from_metadata_with_trusted_keys(&metadata, 1, once(KEYS[1].public())),
            Err(Error::MetadataMissingSignatures {
                role,
                number_of_valid_signatures: 0,
                threshold: 1,
            })
            if role == MetadataPath::root()
        );
    }

    #[test]
    fn good_root_rotation() {
        let raw_root = RootMetadataBuilder::new()
            .root_key(KEYS[0].public().clone())
            .snapshot_key(KEYS[0].public().clone())
            .targets_key(KEYS[0].public().clone())
            .timestamp_key(KEYS[0].public().clone())
            .signed::<Pouf1>(&KEYS[0])
            .unwrap()
            .to_raw()
            .unwrap();

        let mut tuf = Database::from_trusted_root(&raw_root).unwrap();

        let mut root = RootMetadataBuilder::new()
            .version(2)
            .root_key(KEYS[1].public().clone())
            .snapshot_key(KEYS[1].public().clone())
            .targets_key(KEYS[1].public().clone())
            .timestamp_key(KEYS[1].public().clone())
            .signed::<Pouf1>(&KEYS[1])
            .unwrap();

        // add the original key's signature to make it cross signed
        root.add_signature(&KEYS[0]).unwrap();
        let raw_root = root.to_raw().unwrap();

        assert_matches!(tuf.update_root(&raw_root), Ok(()));

        // second update with the same metadata should fail.
        assert_matches!(
            tuf.update_root(&raw_root),
            Err(Error::AttemptedMetadataRollBack { role, trusted_version: 2, new_version: 2 })
            if role == MetadataPath::root()
        );
    }

    #[test]
    fn no_cross_sign_root_rotation() {
        let raw_root = RootMetadataBuilder::new()
            .root_key(KEYS[0].public().clone())
            .snapshot_key(KEYS[0].public().clone())
            .targets_key(KEYS[0].public().clone())
            .timestamp_key(KEYS[0].public().clone())
            .signed::<Pouf1>(&KEYS[0])
            .unwrap()
            .to_raw()
            .unwrap();

        let mut tuf = Database::from_trusted_root(&raw_root).unwrap();

        let raw_root = RootMetadataBuilder::new()
            .root_key(KEYS[1].public().clone())
            .snapshot_key(KEYS[1].public().clone())
            .targets_key(KEYS[1].public().clone())
            .timestamp_key(KEYS[1].public().clone())
            .signed::<Pouf1>(&KEYS[1])
            .unwrap()
            .to_raw()
            .unwrap();

        assert!(tuf.update_root(&raw_root).is_err());
    }

    #[test]
    fn good_timestamp_update() {
        let now = Utc::now();

        let raw_root = RootMetadataBuilder::new()
            .root_key(KEYS[0].public().clone())
            .snapshot_key(KEYS[1].public().clone())
            .targets_key(KEYS[1].public().clone())
            .timestamp_key(KEYS[1].public().clone())
            .signed::<Pouf1>(&KEYS[0])
            .unwrap()
            .to_raw()
            .unwrap();

        let mut tuf = Database::from_trusted_root(&raw_root).unwrap();

        let snapshot = SnapshotMetadataBuilder::new()
            .signed::<Pouf1>(&KEYS[1])
            .unwrap();

        let timestamp =
            TimestampMetadataBuilder::from_snapshot(&snapshot, &[HashAlgorithm::Sha256])
                .unwrap()
                .signed::<Pouf1>(&KEYS[1])
                .unwrap();
        let raw_timestamp = timestamp.to_raw().unwrap();

        assert_matches!(
            tuf.update_timestamp(&now, &raw_timestamp),
            Ok(Some(_parsed_timestamp))
        );

        // second update should do nothing
        assert_matches!(tuf.update_timestamp(&now, &raw_timestamp), Ok(None))
    }

    #[test]
    fn bad_timestamp_update_wrong_key() {
        let now = Utc::now();

        let raw_root = RootMetadataBuilder::new()
            .root_key(KEYS[0].public().clone())
            .snapshot_key(KEYS[1].public().clone())
            .targets_key(KEYS[1].public().clone())
            .timestamp_key(KEYS[1].public().clone())
            .signed::<Pouf1>(&KEYS[0])
            .unwrap()
            .to_raw()
            .unwrap();

        let mut tuf = Database::from_trusted_root(&raw_root).unwrap();

        let snapshot = SnapshotMetadataBuilder::new()
            .signed::<Pouf1>(&KEYS[1])
            .unwrap();

        let raw_timestamp =
            TimestampMetadataBuilder::from_snapshot(&snapshot, &[HashAlgorithm::Sha256])
                .unwrap()
                // sign it with the root key
                .signed::<Pouf1>(&KEYS[0])
                .unwrap()
                .to_raw()
                .unwrap();

        assert!(tuf.update_timestamp(&now, &raw_timestamp).is_err())
    }

    #[test]
    fn good_snapshot_update() {
        let now = Utc::now();

        let raw_root = RootMetadataBuilder::new()
            .root_key(KEYS[0].public().clone())
            .snapshot_key(KEYS[1].public().clone())
            .targets_key(KEYS[2].public().clone())
            .timestamp_key(KEYS[2].public().clone())
            .signed::<Pouf1>(&KEYS[0])
            .unwrap()
            .to_raw()
            .unwrap();

        let mut tuf = Database::from_trusted_root(&raw_root).unwrap();

        let snapshot = SnapshotMetadataBuilder::new().signed(&KEYS[1]).unwrap();
        let raw_snapshot = snapshot.to_raw().unwrap();

        let raw_timestamp =
            TimestampMetadataBuilder::from_snapshot(&snapshot, &[HashAlgorithm::Sha256])
                .unwrap()
                .signed::<Pouf1>(&KEYS[2])
                .unwrap()
                .to_raw()
                .unwrap();

        tuf.update_timestamp(&now, &raw_timestamp).unwrap();

        assert_matches!(tuf.update_snapshot(&now, &raw_snapshot), Ok(true));

        // second update should do nothing
        assert_matches!(tuf.update_snapshot(&now, &raw_snapshot), Ok(false));
    }

    #[test]
    fn bad_snapshot_update_wrong_key() {
        let now = Utc::now();

        let raw_root = RootMetadataBuilder::new()
            .root_key(KEYS[0].public().clone())
            .snapshot_key(KEYS[1].public().clone())
            .targets_key(KEYS[2].public().clone())
            .timestamp_key(KEYS[2].public().clone())
            .signed::<Pouf1>(&KEYS[0])
            .unwrap()
            .to_raw()
            .unwrap();

        let mut tuf = Database::from_trusted_root(&raw_root).unwrap();

        let snapshot = SnapshotMetadataBuilder::new()
            .signed::<Pouf1>(&KEYS[2])
            .unwrap();
        let raw_snapshot = snapshot.to_raw().unwrap();

        let raw_timestamp =
            TimestampMetadataBuilder::from_snapshot(&snapshot, &[HashAlgorithm::Sha256])
                .unwrap()
                // sign it with the targets key
                .signed::<Pouf1>(&KEYS[2])
                .unwrap()
                .to_raw()
                .unwrap();

        tuf.update_timestamp(&now, &raw_timestamp).unwrap();

        assert!(tuf.update_snapshot(&now, &raw_snapshot).is_err());
    }

    #[test]
    fn bad_snapshot_update_wrong_version() {
        let now = Utc::now();

        let raw_root = RootMetadataBuilder::new()
            .root_key(KEYS[0].public().clone())
            .snapshot_key(KEYS[1].public().clone())
            .targets_key(KEYS[2].public().clone())
            .timestamp_key(KEYS[2].public().clone())
            .signed::<Pouf1>(&KEYS[0])
            .unwrap()
            .to_raw()
            .unwrap();

        let mut tuf = Database::from_trusted_root(&raw_root).unwrap();

        let snapshot = SnapshotMetadataBuilder::new()
            .version(2)
            .signed::<Pouf1>(&KEYS[2])
            .unwrap();

        let raw_timestamp =
            TimestampMetadataBuilder::from_snapshot(&snapshot, &[HashAlgorithm::Sha256])
                .unwrap()
                .signed::<Pouf1>(&KEYS[2])
                .unwrap()
                .to_raw()
                .unwrap();

        tuf.update_timestamp(&now, &raw_timestamp).unwrap();

        let raw_snapshot = SnapshotMetadataBuilder::new()
            .version(1)
            .signed::<Pouf1>(&KEYS[1])
            .unwrap()
            .to_raw()
            .unwrap();

        assert!(tuf.update_snapshot(&now, &raw_snapshot).is_err());
    }

    #[test]
    fn good_targets_update() {
        let now = Utc::now();

        let raw_root = RootMetadataBuilder::new()
            .root_key(KEYS[0].public().clone())
            .snapshot_key(KEYS[1].public().clone())
            .targets_key(KEYS[2].public().clone())
            .timestamp_key(KEYS[3].public().clone())
            .signed::<Pouf1>(&KEYS[0])
            .unwrap()
            .to_raw()
            .unwrap();

        let mut tuf = Database::from_trusted_root(&raw_root).unwrap();

        let signed_targets = TargetsMetadataBuilder::new()
            .signed::<Pouf1>(&KEYS[2])
            .unwrap();
        let raw_targets = signed_targets.to_raw().unwrap();

        let snapshot = SnapshotMetadataBuilder::new()
            .insert_metadata(&signed_targets, &[HashAlgorithm::Sha256])
            .unwrap()
            .signed::<Pouf1>(&KEYS[1])
            .unwrap();
        let raw_snapshot = snapshot.to_raw().unwrap();

        let raw_timestamp =
            TimestampMetadataBuilder::from_snapshot(&snapshot, &[HashAlgorithm::Sha256])
                .unwrap()
                .signed::<Pouf1>(&KEYS[3])
                .unwrap()
                .to_raw()
                .unwrap();

        tuf.update_timestamp(&now, &raw_timestamp).unwrap();
        tuf.update_snapshot(&now, &raw_snapshot).unwrap();

        assert_matches!(tuf.update_targets(&now, &raw_targets), Ok(true));

        // second update should do nothing
        assert_matches!(tuf.update_targets(&now, &raw_targets), Ok(false));
    }

    #[test]
    fn bad_targets_update_wrong_key() {
        let now = Utc::now();

        let raw_root = RootMetadataBuilder::new()
            .root_key(KEYS[0].public().clone())
            .snapshot_key(KEYS[1].public().clone())
            .targets_key(KEYS[2].public().clone())
            .timestamp_key(KEYS[3].public().clone())
            .signed::<Pouf1>(&KEYS[0])
            .unwrap()
            .to_raw()
            .unwrap();

        let mut tuf = Database::from_trusted_root(&raw_root).unwrap();

        let signed_targets = TargetsMetadataBuilder::new()
            // sign it with the timestamp key
            .signed::<Pouf1>(&KEYS[3])
            .unwrap();
        let raw_targets = signed_targets.to_raw().unwrap();

        let snapshot = SnapshotMetadataBuilder::new()
            .insert_metadata(&signed_targets, &[HashAlgorithm::Sha256])
            .unwrap()
            .signed::<Pouf1>(&KEYS[1])
            .unwrap();
        let raw_snapshot = snapshot.to_raw().unwrap();

        let raw_timestamp =
            TimestampMetadataBuilder::from_snapshot(&snapshot, &[HashAlgorithm::Sha256])
                .unwrap()
                .signed::<Pouf1>(&KEYS[3])
                .unwrap()
                .to_raw()
                .unwrap();

        tuf.update_timestamp(&now, &raw_timestamp).unwrap();
        tuf.update_snapshot(&now, &raw_snapshot).unwrap();

        assert!(tuf.update_targets(&now, &raw_targets).is_err());
    }

    #[test]
    fn bad_targets_update_wrong_version() {
        let now = Utc::now();

        let raw_root = RootMetadataBuilder::new()
            .root_key(KEYS[0].public().clone())
            .snapshot_key(KEYS[1].public().clone())
            .targets_key(KEYS[2].public().clone())
            .timestamp_key(KEYS[3].public().clone())
            .signed::<Pouf1>(&KEYS[0])
            .unwrap()
            .to_raw()
            .unwrap();

        let mut tuf = Database::from_trusted_root(&raw_root).unwrap();

        let signed_targets = TargetsMetadataBuilder::new()
            .version(2)
            .signed::<Pouf1>(&KEYS[2])
            .unwrap();

        let snapshot = SnapshotMetadataBuilder::new()
            .insert_metadata(&signed_targets, &[HashAlgorithm::Sha256])
            .unwrap()
            .signed::<Pouf1>(&KEYS[1])
            .unwrap();
        let raw_snapshot = snapshot.to_raw().unwrap();

        let raw_timestamp =
            TimestampMetadataBuilder::from_snapshot(&snapshot, &[HashAlgorithm::Sha256])
                .unwrap()
                .signed::<Pouf1>(&KEYS[3])
                .unwrap()
                .to_raw()
                .unwrap();

        tuf.update_timestamp(&now, &raw_timestamp).unwrap();
        tuf.update_snapshot(&now, &raw_snapshot).unwrap();

        let raw_targets = TargetsMetadataBuilder::new()
            .version(1)
            .signed::<Pouf1>(&KEYS[2])
            .unwrap()
            .to_raw()
            .unwrap();

        assert!(tuf.update_targets(&now, &raw_targets).is_err());
    }

    #[test]
    fn test_update_metadata_succeeds_with_good_metadata() {
        let raw_root1 = RootMetadataBuilder::new()
            .root_key(KEYS[0].public().clone())
            .targets_key(KEYS[1].public().clone())
            .snapshot_key(KEYS[2].public().clone())
            .timestamp_key(KEYS[3].public().clone())
            .signed::<Pouf1>(&KEYS[0])
            .unwrap()
            .to_raw()
            .unwrap();

        let signed_targets1 = TargetsMetadataBuilder::new()
            .signed::<Pouf1>(&KEYS[1])
            .unwrap();
        let raw_targets1 = signed_targets1.to_raw().unwrap();

        let snapshot1 = SnapshotMetadataBuilder::new()
            .insert_metadata(&signed_targets1, &[HashAlgorithm::Sha256])
            .unwrap()
            .signed::<Pouf1>(&KEYS[2])
            .unwrap();
        let raw_snapshot1 = snapshot1.to_raw().unwrap();

        let raw_timestamp1 =
            TimestampMetadataBuilder::from_snapshot(&snapshot1, &[HashAlgorithm::Sha256])
                .unwrap()
                .signed::<Pouf1>(&KEYS[3])
                .unwrap()
                .to_raw()
                .unwrap();

        let metadata1 = RawSignedMetadataSetBuilder::new()
            .root(raw_root1)
            .targets(raw_targets1)
            .snapshot(raw_snapshot1)
            .timestamp(raw_timestamp1)
            .build();

        let mut tuf = Database::from_trusted_metadata(&metadata1).unwrap();

        let raw_root2 = RootMetadataBuilder::new()
            .version(2)
            .root_key(KEYS[0].public().clone())
            .targets_key(KEYS[1].public().clone())
            .snapshot_key(KEYS[2].public().clone())
            .timestamp_key(KEYS[3].public().clone())
            .signed::<Pouf1>(&KEYS[0])
            .unwrap()
            .to_raw()
            .unwrap();

        let signed_targets2 = TargetsMetadataBuilder::new()
            .version(2)
            .signed::<Pouf1>(&KEYS[1])
            .unwrap();
        let raw_targets2 = signed_targets2.to_raw().unwrap();

        let snapshot2 = SnapshotMetadataBuilder::new()
            .version(2)
            .insert_metadata(&signed_targets2, &[HashAlgorithm::Sha256])
            .unwrap()
            .signed::<Pouf1>(&KEYS[2])
            .unwrap();
        let raw_snapshot2 = snapshot2.to_raw().unwrap();

        let raw_timestamp2 =
            TimestampMetadataBuilder::from_snapshot(&snapshot2, &[HashAlgorithm::Sha256])
                .unwrap()
                .version(2)
                .signed::<Pouf1>(&KEYS[3])
                .unwrap()
                .to_raw()
                .unwrap();

        let metadata2 = RawSignedMetadataSetBuilder::new()
            .root(raw_root2)
            .targets(raw_targets2)
            .snapshot(raw_snapshot2)
            .timestamp(raw_timestamp2)
            .build();

        assert_matches!(tuf.update_metadata(&metadata2), Ok(true));
    }

    #[test]
    fn test_update_metadata_fails_with_bad_metadata() {
        let raw_root1 = RootMetadataBuilder::new()
            .root_key(KEYS[0].public().clone())
            .targets_key(KEYS[1].public().clone())
            .snapshot_key(KEYS[2].public().clone())
            .timestamp_key(KEYS[3].public().clone())
            .signed::<Pouf1>(&KEYS[0])
            .unwrap()
            .to_raw()
            .unwrap();

        let signed_targets1 = TargetsMetadataBuilder::new()
            .signed::<Pouf1>(&KEYS[1])
            .unwrap();
        let raw_targets1 = signed_targets1.to_raw().unwrap();

        let snapshot1 = SnapshotMetadataBuilder::new()
            .insert_metadata(&signed_targets1, &[HashAlgorithm::Sha256])
            .unwrap()
            .signed::<Pouf1>(&KEYS[2])
            .unwrap();
        let raw_snapshot1 = snapshot1.to_raw().unwrap();

        let raw_timestamp1 =
            TimestampMetadataBuilder::from_snapshot(&snapshot1, &[HashAlgorithm::Sha256])
                .unwrap()
                .signed::<Pouf1>(&KEYS[3])
                .unwrap()
                .to_raw()
                .unwrap();

        let metadata1 = RawSignedMetadataSetBuilder::new()
            .root(raw_root1)
            .targets(raw_targets1)
            .snapshot(raw_snapshot1)
            .timestamp(raw_timestamp1)
            .build();

        let mut tuf = Database::from_trusted_metadata(&metadata1).unwrap();

        let raw_root2 = RootMetadataBuilder::new()
            .version(2)
            .root_key(KEYS[1].public().clone())
            .targets_key(KEYS[2].public().clone())
            .snapshot_key(KEYS[3].public().clone())
            .timestamp_key(KEYS[4].public().clone())
            .signed::<Pouf1>(&KEYS[1])
            .unwrap()
            .to_raw()
            .unwrap();

        let signed_targets2 = TargetsMetadataBuilder::new()
            .version(2)
            .signed::<Pouf1>(&KEYS[1])
            .unwrap();
        let raw_targets2 = signed_targets2.to_raw().unwrap();

        let snapshot2 = SnapshotMetadataBuilder::new()
            .version(2)
            .insert_metadata(&signed_targets2, &[HashAlgorithm::Sha256])
            .unwrap()
            .signed::<Pouf1>(&KEYS[2])
            .unwrap();
        let raw_snapshot2 = snapshot2.to_raw().unwrap();

        let raw_timestamp2 =
            TimestampMetadataBuilder::from_snapshot(&snapshot2, &[HashAlgorithm::Sha256])
                .unwrap()
                .version(2)
                .signed::<Pouf1>(&KEYS[3])
                .unwrap()
                .to_raw()
                .unwrap();

        let metadata2 = RawSignedMetadataSetBuilder::new()
            .root(raw_root2)
            .targets(raw_targets2)
            .snapshot(raw_snapshot2)
            .timestamp(raw_timestamp2)
            .build();

        assert_matches!(
            tuf.update_metadata(&metadata2),
            Err(Error::MetadataMissingSignatures {
                role,
                number_of_valid_signatures: 0,
                threshold: 1,
            })
            if role == MetadataPath::root()
        );
    }
}
