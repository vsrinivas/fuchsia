// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        fsck::{
            errors::{FsckError, FsckFatal, FsckIssue, FsckWarning},
            Fsck,
        },
        lsm_tree::types::{Item, ItemRef, LayerIterator, MutableLayer},
        object_handle::INVALID_OBJECT_ID,
        object_store::{
            allocator::{self, AllocatorKey, AllocatorValue},
            graveyard::Graveyard,
            AttributeKey, ExtentKey, ExtentValue, ObjectDescriptor, ObjectKey, ObjectKeyData,
            ObjectKind, ObjectStore, ObjectValue, DEFAULT_DATA_ATTRIBUTE_ID,
        },
        range::RangeExt,
        round::round_up,
    },
    anyhow::{self, Error},
    std::{
        cell::UnsafeCell, collections::btree_map::BTreeMap, convert::TryInto, iter::Iterator,
        ops::Bound,
    },
};

#[derive(Debug)]
struct ScannedFile {
    object_id: u64,
    // Set when the Object record is processed for the file.  (The object might appear in another
    // record before its Object record appears, e.g. a Child record, hence this is an Option.)
    kind: Option<ObjectKind>,
    // A list of attribute IDs found for the file, along with their logical size.
    attributes: Vec<(u64, u64)>,
    // A list of parent object IDs for the file.  INVALID_OBJECT_ID indicates a reference from
    // outside the object store (either the graveyard, or because the object is a root object of the
    // store and probably has a reference to it in e.g. the StoreInfo or superblock).
    parents: Vec<u64>,
    // The allocated size of the file (computed by summing up the extents for the file).
    allocated_size: u64,
}

#[derive(Debug)]
struct ScannedDir {
    // This is stored in an UnsafeCell because we will use it later to mark the directory as visited
    // when doing cycle detection.
    // Safety: This is safe to access and modify as long as the reference to the ScannedDir is
    // unique.
    object_id: UnsafeCell<u64>,
    // See ScannedFile::kind.
    kind: Option<ObjectKind>,
    // A list of all children object IDs found for the directory, and a boolean indicating if the
    // child is a directory.
    children: Vec<(u64, bool)>,
    // The parent object of the directory.  See ScannedFile::parents.  Note that directories can
    // only have one parent, hence this is just an Option (not a Vec).
    parent: Option<u64>,
}

#[derive(Debug)]
enum ScannedObject {
    File(ScannedFile),
    Directory(ScannedDir),
    // Other objects that we don't have special logic for (e.g. graveyard, volumes, ...), which
    // we'll just track the object ID of.
    Etc(u64),
    // A tombstoned object, which should have no other records associated with it.
    Tombstone,
}

struct ScannedStore<'a, F: Fn(&FsckIssue)> {
    fsck: &'a Fsck<'a, F>,
    objects: BTreeMap<u64, ScannedObject>,
    root_objects: Vec<u64>,
    store_id: u64,
    is_root_store: bool,
}

impl<'a, F: Fn(&FsckIssue)> ScannedStore<'a, F> {
    fn new(
        fsck: &'a Fsck<'a, F>,
        root_objects: impl AsRef<[u64]>,
        store_id: u64,
        is_root_store: bool,
    ) -> Self {
        Self {
            fsck,
            objects: BTreeMap::new(),
            root_objects: root_objects.as_ref().into(),
            store_id,
            is_root_store,
        }
    }

    // Process an object store record, adding or updating any objects known to the ScannedStore.
    fn process(&mut self, key: &ObjectKey, value: &ObjectValue) -> Result<(), Error> {
        match key.data {
            ObjectKeyData::Object => {
                match value {
                    ObjectValue::None => {
                        if self.objects.insert(key.object_id, ScannedObject::Tombstone).is_some() {
                            self.fsck.error(FsckError::TombstonedObjectHasRecords(
                                self.store_id,
                                key.object_id,
                            ))?;
                        }
                    }
                    ObjectValue::Some => {
                        self.fsck.error(FsckError::UnexpectedRecordInObjectStore(
                            self.store_id,
                            key.into(),
                            value.into(),
                        ))?;
                    }
                    ObjectValue::Object {
                        kind: ObjectKind::File { refs, allocated_size }, ..
                    } => {
                        let kind =
                            ObjectKind::File { refs: *refs, allocated_size: *allocated_size };
                        match self.objects.get_mut(&key.object_id) {
                            Some(ScannedObject::File(ScannedFile { kind: obj_kind, .. })) => {
                                // This should be the only Object record we encounter for this
                                // object.
                                assert!(obj_kind.is_none());
                                *obj_kind = Some(kind);
                            }
                            Some(ScannedObject::Directory(..)) => {
                                self.fsck.error(FsckError::ConflictingTypeForLink(
                                    self.store_id,
                                    key.object_id,
                                    ObjectDescriptor::File.into(),
                                    ObjectDescriptor::Directory.into(),
                                ))?;
                            }
                            Some(ScannedObject::Etc(..)) => { /* NOP */ }
                            Some(ScannedObject::Tombstone) => {
                                self.fsck.error(FsckError::TombstonedObjectHasRecords(
                                    self.store_id,
                                    key.object_id,
                                ))?;
                            }
                            None => {
                                let parents = if self.root_objects.contains(&key.object_id) {
                                    vec![INVALID_OBJECT_ID]
                                } else {
                                    vec![]
                                };
                                self.objects.insert(
                                    key.object_id,
                                    ScannedObject::File(ScannedFile {
                                        object_id: key.object_id,
                                        kind: Some(kind),
                                        attributes: vec![],
                                        parents,
                                        allocated_size: 0,
                                    }),
                                );
                            }
                        }
                    }
                    ObjectValue::Object { kind: ObjectKind::Directory { sub_dirs }, .. } => {
                        let kind = ObjectKind::Directory { sub_dirs: *sub_dirs };
                        match self.objects.get_mut(&key.object_id) {
                            Some(ScannedObject::File(..)) => {
                                self.fsck.error(FsckError::ConflictingTypeForLink(
                                    self.store_id,
                                    key.object_id,
                                    ObjectDescriptor::Directory.into(),
                                    ObjectDescriptor::File.into(),
                                ))?;
                            }
                            Some(ScannedObject::Directory(ScannedDir {
                                kind: obj_kind, ..
                            })) => {
                                // This should be the only Object record we encounter for this
                                // object.
                                assert!(obj_kind.is_none());
                                *obj_kind = Some(kind);
                            }
                            Some(ScannedObject::Etc(..)) => { /* NOP */ }
                            Some(ScannedObject::Tombstone) => {
                                // Arguably this could also be a mismatched object type, since
                                // directories shouldn't be tombstoned.
                                self.fsck.error(FsckError::TombstonedObjectHasRecords(
                                    self.store_id,
                                    key.object_id,
                                ))?;
                            }
                            None => {
                                let parent = if self.root_objects.contains(&key.object_id) {
                                    Some(INVALID_OBJECT_ID)
                                } else {
                                    None
                                };
                                // We've verified no duplicate keys, and Object records come first,
                                // so this should always be the first time we encounter this object.
                                self.objects.insert(
                                    key.object_id,
                                    ScannedObject::Directory(ScannedDir {
                                        object_id: UnsafeCell::new(key.object_id),
                                        kind: Some(kind),
                                        children: vec![],
                                        parent,
                                    }),
                                );
                            }
                        }
                    }
                    ObjectValue::Object { kind: ObjectKind::Graveyard, .. } => {
                        self.objects.insert(key.object_id, ScannedObject::Etc(key.object_id));
                    }
                    _ => {
                        self.fsck.error(FsckError::MalformedObjectRecord(
                            self.store_id,
                            key.into(),
                            value.into(),
                        ))?;
                    }
                }
            }
            ObjectKeyData::Keys => {
                // TODO(fxbug.dev/101467): Check encryption keys.
                if let ObjectValue::Keys(_) = value {
                } else {
                    self.fsck.error(FsckError::MalformedObjectRecord(
                        self.store_id,
                        key.into(),
                        value.into(),
                    ))?;
                }
            }
            ObjectKeyData::Attribute(attribute_id, AttributeKey::Size) => {
                match value {
                    ObjectValue::Attribute { size } => {
                        match self.objects.get_mut(&key.object_id) {
                            Some(ScannedObject::File(ScannedFile { attributes, .. })) => {
                                attributes.push((attribute_id, *size));
                            }
                            Some(ScannedObject::Directory(..)) => {
                                self.fsck.error(FsckError::AttributeOnDirectory(
                                    self.store_id,
                                    key.object_id,
                                ))?;
                            }
                            Some(ScannedObject::Etc(..)) => { /* NOP */ }
                            Some(ScannedObject::Tombstone) => {
                                self.fsck.error(FsckError::TombstonedObjectHasRecords(
                                    self.store_id,
                                    key.object_id,
                                ))?;
                            }
                            None => {
                                // We verify key ordering elsewhere, and Object records come before
                                // Attribute records, so we should never find an attribute without
                                // its object already encountered.  Thus, this is an orphaned
                                // attribute.
                                self.fsck.warning(FsckWarning::OrphanedAttribute(
                                    self.store_id,
                                    key.object_id,
                                    attribute_id,
                                ))?;
                            }
                        }
                    }
                    _ => {
                        self.fsck.error(FsckError::MalformedObjectRecord(
                            self.store_id,
                            key.into(),
                            value.into(),
                        ))?;
                    }
                }
            }
            // Ignore extents on this pass. We'll process them later.
            ObjectKeyData::Attribute(_, AttributeKey::Extent(_)) => {
                match value {
                    // Regular extent record.
                    ObjectValue::Extent(ExtentValue::Some { .. }) => {}
                    // Deleted extent.
                    ObjectValue::Extent(ExtentValue::None) => {}
                    _ => {
                        self.fsck.error(FsckError::MalformedObjectRecord(
                            self.store_id,
                            key.into(),
                            value.into(),
                        ))?;
                    }
                }
            }
            ObjectKeyData::Child { name: ref _name } => {
                match value {
                    ObjectValue::None => {}
                    ObjectValue::Child { object_id: child_id, object_descriptor } => {
                        if *child_id == INVALID_OBJECT_ID {
                            self.fsck.warning(FsckWarning::InvalidObjectIdInStore(
                                self.store_id,
                                key.into(),
                                value.into(),
                            ))?;
                        }
                        if self.root_objects.contains(child_id) {
                            self.fsck.error(FsckError::RootObjectHasParent(
                                self.store_id,
                                *child_id,
                                key.object_id,
                            ))?;
                        }
                        match self.objects.get_mut(child_id) {
                            Some(ScannedObject::File(ScannedFile { parents, .. })) => {
                                match object_descriptor {
                                    ObjectDescriptor::File => {}
                                    ObjectDescriptor::Directory => {
                                        self.fsck.error(FsckError::ConflictingTypeForLink(
                                            self.store_id,
                                            key.object_id,
                                            ObjectDescriptor::File.into(),
                                            ObjectDescriptor::Directory.into(),
                                        ))?;
                                    }
                                    ObjectDescriptor::Volume => unreachable!(),
                                }
                                parents.push(key.object_id);
                            }
                            Some(ScannedObject::Directory(ScannedDir { parent, .. })) => {
                                match object_descriptor {
                                    ObjectDescriptor::File => {
                                        self.fsck.error(FsckError::ConflictingTypeForLink(
                                            self.store_id,
                                            key.object_id,
                                            ObjectDescriptor::Directory.into(),
                                            ObjectDescriptor::File.into(),
                                        ))?;
                                    }
                                    ObjectDescriptor::Directory => {}
                                    ObjectDescriptor::Volume => unreachable!(),
                                }
                                if parent.is_some() {
                                    // TODO(fxbug.dev/87381): Accumulating and reporting all parents
                                    // might be useful.
                                    self.fsck.error(FsckError::MultipleLinksToDirectory(
                                        self.store_id,
                                        *child_id,
                                    ))?;
                                }
                                *parent = Some(key.object_id);
                            }
                            Some(ScannedObject::Etc(..)) => {
                                // TODO(fxbug.dev/87381): Verify the ObjectDescriptor matches the
                                // other metadata associated with the object.
                            }
                            Some(ScannedObject::Tombstone) => {
                                self.fsck.error(FsckError::TombstonedObjectHasRecords(
                                    self.store_id,
                                    key.object_id,
                                ))?;
                            }
                            None => {
                                let node = match object_descriptor {
                                    ObjectDescriptor::File => ScannedObject::File(ScannedFile {
                                        object_id: *child_id,
                                        kind: None,
                                        attributes: vec![],
                                        parents: vec![key.object_id],
                                        allocated_size: 0,
                                    }),
                                    ObjectDescriptor::Directory => {
                                        ScannedObject::Directory(ScannedDir {
                                            object_id: UnsafeCell::new(*child_id),
                                            kind: None,
                                            children: vec![],
                                            parent: Some(key.object_id),
                                        })
                                    }
                                    ObjectDescriptor::Volume => {
                                        if !self.is_root_store {
                                            self.fsck.error(FsckError::VolumeInChildStore(
                                                self.store_id,
                                                *child_id,
                                            ))?;
                                        }
                                        ScannedObject::Etc(*child_id)
                                    }
                                };
                                self.objects.insert(*child_id, node);
                            }
                        };
                        match self.objects.get_mut(&key.object_id) {
                            Some(ScannedObject::File(..)) => {
                                self.fsck.error(FsckError::FileHasChildren(
                                    self.store_id,
                                    key.object_id,
                                ))?;
                            }
                            Some(ScannedObject::Directory(ScannedDir { children, .. })) => {
                                // Sort order of records was verified in check_child_store, so this
                                // pushes in order.
                                // There shouldn't be any duplicates added since (a) we ensure no
                                // dupe records are processed and (b) we check that we don't have
                                // conflicting types for two links to a given object ID just above
                                // here.
                                children.push((
                                    *child_id,
                                    *object_descriptor == ObjectDescriptor::Directory,
                                ));
                            }
                            Some(ScannedObject::Etc(..)) => { /* NOP */ }
                            Some(ScannedObject::Tombstone) => {
                                self.fsck.error(FsckError::TombstonedObjectHasRecords(
                                    self.store_id,
                                    key.object_id,
                                ))?;
                            }
                            None => {
                                self.objects.insert(
                                    key.object_id,
                                    ScannedObject::Directory(ScannedDir {
                                        object_id: UnsafeCell::new(key.object_id),
                                        kind: None,
                                        children: vec![(
                                            *child_id,
                                            *object_descriptor == ObjectDescriptor::Directory,
                                        )],
                                        parent: None,
                                    }),
                                );
                            }
                        }
                    }
                    _ => {
                        self.fsck.error(FsckError::MalformedObjectRecord(
                            self.store_id,
                            key.into(),
                            value.into(),
                        ))?;
                    }
                }
            }
            ObjectKeyData::GraveyardEntry { .. } => {}
        }
        Ok(())
    }

    fn insert_graveyard_file(&mut self, object_id: u64) -> Result<(), Error> {
        match self.objects.get_mut(&object_id) {
            Some(ScannedObject::File(ScannedFile { parents, .. })) => {
                parents.push(INVALID_OBJECT_ID)
            }
            Some(_) => {
                self.fsck.error(FsckError::UnexpectedObjectInGraveyard(object_id))?;
            }
            None => {
                self.fsck.warning(FsckWarning::GraveyardRecordForAbsentObject(
                    self.store_id,
                    object_id,
                ))?;
            }
        }
        Ok(())
    }

    // Returns an iterator over objects in object-id order.
    // Note that this doesn't imply any ordering in terms of their position in the object graph.
    fn objects(&self) -> impl Iterator<Item = &ScannedObject> {
        self.objects.values()
    }

    // Returns an iterator over objects in BFS order (which means that orphaned objects won't be
    // scanned).
    fn iter_bfs(&self) -> ScannedStoreIterator<'_, 'a, F> {
        ScannedStoreIterator(self, self.root_objects.clone())
    }
}

// Implements a BFS iterator for a store.  Orphaned objects and graveyard objects won't be
// processed.
struct ScannedStoreIterator<'iter, 'a, F: Fn(&FsckIssue)>(&'iter ScannedStore<'a, F>, Vec<u64>);

impl<'iter, 'a, F: Fn(&FsckIssue)> std::iter::Iterator for ScannedStoreIterator<'iter, 'a, F> {
    type Item = &'iter ScannedObject;

    fn next(&mut self) -> Option<Self::Item> {
        if let Some(id) = self.1.pop() {
            let object = self.0.objects.get(&id).unwrap();
            match object {
                ScannedObject::File(..) | ScannedObject::Etc(..) | ScannedObject::Tombstone => {}
                ScannedObject::Directory(ScannedDir { children, .. }) => {
                    self.1.extend(children.iter().map(|(oid, _)| oid));
                }
            }
            Some(object)
        } else {
            None
        }
    }
}

// Scans all extents in the store, emitting synthesized allocations into |fsck.allocations| and
// updating the sizes for files in |scanned|.
// TODO(fxbug.dev/95475): Roll this back into main function.
async fn scan_extents<'a, F: Fn(&FsckIssue)>(
    store: &ObjectStore,
    scanned: &mut ScannedStore<'a, F>,
) -> Result<(), Error> {
    let store_id = store.store_object_id();
    let bs = store.block_size();
    let layer_set = store.tree().layer_set();
    let mut merger = layer_set.merger();
    let mut iter = merger.seek(Bound::Unbounded).await?;
    let mut allocated_bytes = 0;
    while let Some(itemref) = iter.get() {
        if let ItemRef {
            key:
                ObjectKey {
                    object_id,
                    data:
                        ObjectKeyData::Attribute(
                            attribute_id,
                            AttributeKey::Extent(ExtentKey { range }),
                        ),
                },
            value: ObjectValue::Extent(value),
            ..
        } = itemref
        {
            if let ExtentValue::Some { device_offset, .. } = value {
                if range.start % bs > 0 || range.end % bs > 0 {
                    scanned.fsck.error(FsckError::MisalignedExtent(
                        store_id,
                        *object_id,
                        range.clone(),
                        0,
                    ))?;
                } else if range.start >= range.end {
                    scanned.fsck.error(FsckError::MalformedExtent(
                        store_id,
                        *object_id,
                        range.clone(),
                        0,
                    ))?;
                }
                allocated_bytes += range.length().unwrap();
                if device_offset % bs > 0 {
                    scanned.fsck.error(FsckError::MisalignedExtent(
                        store_id,
                        *object_id,
                        range.clone(),
                        *device_offset,
                    ))?;
                }
                match scanned.objects.get_mut(object_id) {
                    Some(ScannedObject::File(ScannedFile {
                        attributes, allocated_size, ..
                    })) => {
                        match attributes.iter().find(|(attr_id, _)| attr_id == attribute_id) {
                            Some((_, size)) => {
                                if range.end > round_up(*size, bs).unwrap() {
                                    scanned.fsck.error(FsckError::ExtentExceedsLength(
                                        store_id,
                                        *object_id,
                                        *attribute_id,
                                        *size,
                                        range.into(),
                                    ))?;
                                }
                            }
                            None => {
                                scanned.fsck.warning(FsckWarning::ExtentForMissingAttribute(
                                    store.store_object_id(),
                                    *object_id,
                                    *attribute_id,
                                ))?;
                            }
                        }
                        *allocated_size += range.end - range.start;
                    }
                    Some(ScannedObject::Directory(..)) => {
                        scanned.fsck.warning(FsckWarning::ExtentForDirectory(
                            store.store_object_id(),
                            *object_id,
                        ))?;
                    }
                    Some(_) => { /* NOP */ }
                    None => {
                        scanned.fsck.warning(FsckWarning::ExtentForNonexistentObject(
                            store.store_object_id(),
                            *object_id,
                        ))?;
                    }
                }
                let item = Item::new(
                    AllocatorKey {
                        device_range: *device_offset..*device_offset + range.end - range.start,
                    },
                    AllocatorValue::Abs { count: 1, owner_object_id: store_id },
                );
                let lower_bound = item.key.lower_bound_for_merge_into();
                scanned
                    .fsck
                    .allocations
                    .merge_into(item, &lower_bound, allocator::merge::merge)
                    .await;
            }
        }
        iter.advance().await?;
    }
    scanned.fsck.verbose(format!(
        "Store {} has {} bytes allocated",
        store.store_object_id(),
        allocated_bytes
    ));
    Ok(())
}

/// Scans an object store, accumulating all of its allocations into |fsck.allocations| and
/// validating various object properties.
pub(super) async fn scan_store<F: Fn(&FsckIssue)>(
    fsck: &Fsck<'_, F>,
    store: &ObjectStore,
    root_objects: impl AsRef<[u64]>,
) -> Result<(), Error> {
    let store_id = store.store_object_id();

    let mut scanned = ScannedStore::new(fsck, root_objects, store_id, store.is_root());

    // Scan the store for objects, attributes, and parent/child relationships.
    let layer_set = store.tree().layer_set();
    let mut merger = layer_set.merger();
    let mut iter =
        fsck.assert(merger.seek(Bound::Unbounded).await, FsckFatal::MalformedStore(store_id))?;
    let mut last_item: Option<Item<ObjectKey, ObjectValue>> = None;
    while let Some(item) = iter.get() {
        if let Some(last_item) = last_item {
            if last_item.key >= *item.key {
                fsck.fatal(FsckFatal::MisOrderedObjectStore(store_id))?;
            }
        }
        if item.key.object_id == INVALID_OBJECT_ID {
            fsck.warning(FsckWarning::InvalidObjectIdInStore(
                store_id,
                item.key.into(),
                item.value.into(),
            ))?;
        }
        scanned.process(item.key, item.value)?;
        last_item = Some(item.cloned());
        fsck.assert(iter.advance().await, FsckFatal::MalformedStore(store_id))?;
    }

    // Add a reference for files in the graveyard (which acts as the file's parent until it is
    // purged, leaving only the Object record in the original store and no links to the file).
    // This must be done after scanning the object store.
    let layer_set = store.tree().layer_set();
    let mut merger = layer_set.merger();
    let mut iter = fsck.assert(
        Graveyard::iter(store.graveyard_directory_object_id(), &mut merger).await,
        FsckFatal::MalformedGraveyard,
    )?;
    while let Some((object_id, _)) = iter.get() {
        scanned.insert_graveyard_file(object_id)?;
        fsck.assert(iter.advance().await, FsckFatal::MalformedGraveyard)?;
    }

    // Iterate over extents, adding them to the relevant attributes for the file.
    scan_extents(store, &mut scanned).await?;

    // At this point, we've provided all of the inputs to |scanned|.

    // First, iterate in object-id order, so that we check every object (and thus find orphans).
    // It's not very efficient to scan twice, but we don't want to miss orphaned objects, so we'll
    // have to do this without some refactoring to keep orphaned objects off to the side until
    // they're parented (allowing us to easily also scan orphaned objects).
    let mut num_objects = 0;
    let mut files = 0;
    let mut directories = 0;
    let mut tombstones = 0;
    let mut other = 0;
    for object in scanned.objects() {
        num_objects += 1;
        match object {
            ScannedObject::File(ScannedFile {
                object_id,
                kind,
                attributes,
                parents,
                allocated_size: actual_allocated_size,
                ..
            }) => {
                files += 1;
                match kind {
                    Some(ObjectKind::File { refs, allocated_size, .. }) => {
                        let expected_refs = parents.len().try_into().unwrap();
                        // expected_refs == 0 is handled separately to distinguish orphaned objects
                        if expected_refs != *refs && expected_refs > 0 {
                            fsck.error(FsckError::RefCountMismatch(
                                *object_id,
                                expected_refs,
                                *refs,
                            ))?;
                        }
                        if allocated_size != actual_allocated_size {
                            fsck.error(FsckError::AllocatedSizeMismatch(
                                store_id,
                                *object_id,
                                *allocated_size,
                                *actual_allocated_size,
                            ))?;
                        }
                        if attributes
                            .iter()
                            .find(|(attr_id, _)| *attr_id == DEFAULT_DATA_ATTRIBUTE_ID)
                            .is_none()
                        {
                            fsck.error(FsckError::MissingDataAttribute(store_id, *object_id))?;
                        }
                    }
                    Some(_) => unreachable!(), // Checked during tree construction
                    None => {
                        fsck.error(FsckError::MissingObjectInfo(store_id, *object_id))?;
                    }
                }
                if parents.is_empty() {
                    fsck.warning(FsckWarning::OrphanedObject(store_id, *object_id))?;
                }
            }
            ScannedObject::Directory(ScannedDir { object_id, kind, children, parent }) => {
                directories += 1;
                let oid = unsafe { *object_id.get() };
                match kind {
                    Some(ObjectKind::Directory { sub_dirs }) => {
                        let num_dirs = children
                            .iter()
                            .filter(|(_, is_dir)| *is_dir)
                            .count()
                            .try_into()
                            .unwrap();
                        if num_dirs != *sub_dirs {
                            fsck.error(FsckError::SubDirCountMismatch(
                                store_id, oid, *sub_dirs, num_dirs,
                            ))?;
                        }
                    }
                    Some(_) => unreachable!(), // Checked during tree construction
                    None => {
                        fsck.error(FsckError::MissingObjectInfo(store_id, oid))?;
                    }
                }
                if parent.is_none() {
                    fsck.warning(FsckWarning::OrphanedObject(store_id, oid))?;
                }
            }
            ScannedObject::Etc(_) => {
                other += 1;
            }
            ScannedObject::Tombstone => {
                tombstones += 1;
                num_objects -= 1;
            }
        }
    }
    if num_objects != store.object_count() {
        fsck.error(FsckError::ObjectCountMismatch(store_id, store.object_count(), num_objects))?;
    }
    fsck.verbose(format!(
        "Store {} has {} files, {} dirs, {} tombstones, {} other objects",
        store_id, files, directories, tombstones, other
    ));

    // Now iterate again in BFS order, looking for cycles.
    for object in scanned.iter_bfs() {
        // Mark directories as visited by setting its kind to None, which we've already checked
        // above and don't need to check any more.
        // We don't bother looking for cycles involving non-directories since they can't have
        // children.
        match object {
            ScannedObject::File(..) | ScannedObject::Etc(..) | ScannedObject::Tombstone => {}
            ScannedObject::Directory(ScannedDir { object_id, .. }) => {
                let oid = unsafe { *object_id.get() };
                if oid == INVALID_OBJECT_ID {
                    fsck.error(FsckError::LinkCycle(store_id, oid))?;
                    // Once we've found a cycle, break out immediately since otherwise we'll spin
                    // forever.
                    break;
                }
                unsafe { *object_id.get() = INVALID_OBJECT_ID };
            }
        }
    }

    Ok(())
}
