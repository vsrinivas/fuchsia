// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        errors::FxfsError,
        filesystem::SyncOptions,
        log::*,
        object_handle::INVALID_OBJECT_ID,
        object_store::{
            self,
            directory::{self, ObjectDescriptor, ReplacedChild},
            transaction::{LockKey, Options, Transaction},
            ObjectStore, Timestamp,
        },
        platform::fuchsia::{
            device::BlockServer,
            errors::map_to_status,
            file::FxFile,
            node::{FxNode, GetResult, OpenedNode},
            volume::{info_to_filesystem_info, FxVolume},
        },
    },
    anyhow::{bail, Error},
    async_trait::async_trait,
    either::{Left, Right},
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io as fio,
    fuchsia_zircon::Status,
    std::{
        any::Any,
        sync::{Arc, Mutex},
    },
    vfs::{
        common::{rights_to_posix_mode_bits, send_on_open_with_error},
        directory::{
            dirents_sink::{self, AppendResult, Sink},
            entry::{DirectoryEntry, EntryInfo},
            entry_container::{Directory, DirectoryWatcher, MutableDirectory},
            mutable::connection::io1::MutableConnection,
            traversal_position::TraversalPosition,
            watchers::{event_producers::SingleNameEventProducer, Watchers},
        },
        execution_scope::ExecutionScope,
        path::Path,
    },
};

pub struct FxDirectory {
    // The root directory is the only directory which has no parent, and its parent can never
    // change, hence the Option can go on the outside.
    parent: Option<Mutex<Arc<FxDirectory>>>,
    directory: object_store::Directory<FxVolume>,
    watchers: Mutex<Watchers>,
}

impl FxDirectory {
    pub(super) fn new(
        parent: Option<Arc<FxDirectory>>,
        directory: object_store::Directory<FxVolume>,
    ) -> Self {
        Self {
            parent: parent.map(|p| Mutex::new(p)),
            directory,
            watchers: Mutex::new(Watchers::new()),
        }
    }

    pub(super) fn directory(&self) -> &object_store::Directory<FxVolume> {
        &self.directory
    }

    pub fn volume(&self) -> &Arc<FxVolume> {
        self.directory.owner()
    }

    fn store(&self) -> &ObjectStore {
        self.directory.store()
    }

    pub fn is_deleted(&self) -> bool {
        self.directory.is_deleted()
    }

    pub fn set_deleted(&self) {
        self.directory.set_deleted();
        self.watchers.lock().unwrap().send_event(&mut SingleNameEventProducer::deleted());
    }

    /// Acquires a transaction with the appropriate locks to unlink |name|. Returns the transaction,
    /// as well as the ID and type of the child.
    ///
    /// We always need to lock |self|, but we only need to lock the child if it's a directory,
    /// to prevent entries being added to the directory.
    pub(super) async fn acquire_transaction_for_unlink<'a>(
        self: &Arc<Self>,
        extra_keys: &[LockKey],
        name: &str,
        borrow_metadata_space: bool,
    ) -> Result<(Transaction<'a>, u64, ObjectDescriptor), Error> {
        // Since we don't know the child object ID until we've looked up the child, we need to loop
        // until we have acquired a lock on a child whose ID is the same as it was in the last
        // iteration (or the child is a file, at which point it doesn't matter).
        //
        // Note that the returned transaction may lock more objects than is necessary (for example,
        // if the child "foo" was first a directory, then was renamed to "bar" and a file "foo" was
        // created, we might acquire a lock on both the parent and "bar").
        let store = self.store();
        let mut child_object_id = INVALID_OBJECT_ID;
        loop {
            let mut lock_keys = if child_object_id == INVALID_OBJECT_ID {
                vec![LockKey::object(store.store_object_id(), self.object_id())]
            } else {
                vec![
                    LockKey::object(store.store_object_id(), self.object_id()),
                    LockKey::object(store.store_object_id(), child_object_id),
                ]
            };
            lock_keys.extend_from_slice(extra_keys);
            let fs = store.filesystem().clone();
            let transaction = fs
                .new_transaction(
                    &lock_keys,
                    Options { borrow_metadata_space, ..Default::default() },
                )
                .await?;

            let (object_id, object_descriptor) =
                self.directory.lookup(name).await?.ok_or(FxfsError::NotFound)?;
            match object_descriptor {
                ObjectDescriptor::File => {
                    return Ok((transaction, object_id, object_descriptor));
                }
                ObjectDescriptor::Directory => {
                    if object_id == child_object_id {
                        return Ok((transaction, object_id, object_descriptor));
                    }
                    child_object_id = object_id;
                }
                ObjectDescriptor::Volume => bail!(FxfsError::Inconsistent),
            }
        }
    }

    async fn lookup(
        self: &Arc<Self>,
        flags: fio::OpenFlags,
        mode: u32,
        mut path: Path,
    ) -> Result<OpenedNode<dyn FxNode>, Error> {
        if path.is_empty() {
            return Ok(OpenedNode::new(self.clone()));
        }
        let store = self.store();
        let fs = store.filesystem();
        let mut current_node = self.clone() as Arc<dyn FxNode>;
        loop {
            let last_segment = path.is_single_component();
            let current_dir =
                current_node.into_any().downcast::<FxDirectory>().map_err(|_| FxfsError::NotDir)?;
            let name = path.next().unwrap();

            // Create the transaction here if we might need to create the object so that we have a
            // lock in place.
            let keys =
                [LockKey::object(store.store_object_id(), current_dir.directory.object_id())];
            let transaction_or_guard = if last_segment && flags.intersects(fio::OpenFlags::CREATE) {
                Left(fs.clone().new_transaction(&keys, Options::default()).await?)
            } else {
                // When child objects are created, the object is created along with the directory
                // entry in the same transaction, and so we need to hold a read lock over the lookup
                // and open calls.
                Right(fs.read_lock(&keys).await)
            };

            match current_dir.directory.lookup(name).await? {
                Some((object_id, object_descriptor)) => {
                    if transaction_or_guard.is_left()
                        && flags.intersects(fio::OpenFlags::CREATE_IF_ABSENT)
                    {
                        bail!(FxfsError::AlreadyExists);
                    }
                    if last_segment {
                        match object_descriptor {
                            ObjectDescriptor::File => {
                                if flags.intersects(fio::OpenFlags::DIRECTORY) {
                                    bail!(FxfsError::NotDir)
                                }
                            }
                            ObjectDescriptor::Directory => {
                                if flags.intersects(fio::OpenFlags::NOT_DIRECTORY) {
                                    bail!(FxfsError::NotFile)
                                }
                            }
                            ObjectDescriptor::Volume => bail!(FxfsError::Inconsistent),
                        }
                    }
                    current_node = self
                        .volume()
                        .get_or_load_node(object_id, object_descriptor, Some(self.clone()))
                        .await?;
                    if last_segment {
                        // We must make sure to take an open-count whilst we are holding a read
                        // lock.
                        return Ok(OpenedNode::new(current_node));
                    }
                }
                None => {
                    if let Left(mut transaction) = transaction_or_guard {
                        let node = OpenedNode::new(
                            current_dir.create_child(&mut transaction, name, mode).await?,
                        );
                        if let GetResult::Placeholder(p) =
                            self.volume().cache().get_or_reserve(node.object_id()).await
                        {
                            transaction
                                .commit_with_callback(|_| {
                                    p.commit(&node);
                                    current_dir.did_add(name);
                                })
                                .await?;
                            return Ok(node);
                        } else {
                            // We created a node, but the object ID was already used in the cache,
                            // which suggests a object ID was reused (which would either be a bug or
                            // corruption).
                            bail!(FxfsError::Inconsistent);
                        }
                    } else {
                        bail!(FxfsError::NotFound);
                    }
                }
            };
        }
    }

    async fn create_child(
        self: &Arc<Self>,
        transaction: &mut Transaction<'_>,
        name: &str,
        mode: u32,
    ) -> Result<Arc<dyn FxNode>, Error> {
        // NOTE: The usage of | below is for a match case OR, not a bitwise OR
        match mode & fio::MODE_TYPE_MASK {
            fio::MODE_TYPE_DIRECTORY => Ok(Arc::new(FxDirectory::new(
                Some(self.clone()),
                self.directory.create_child_dir(transaction, name).await?,
            )) as Arc<dyn FxNode>),
            0 | fio::MODE_TYPE_FILE | fio::MODE_TYPE_BLOCK_DEVICE => {
                Ok(FxFile::new(self.directory.create_child_file(transaction, name).await?)
                    as Arc<dyn FxNode>)
            }
            fio::MODE_TYPE_SERVICE => {
                bail!(FxfsError::NotSupported)
            }
            _ => bail!(FxfsError::InvalidArgs),
        }
    }

    /// Called to indicate a file or directory was removed from this directory.
    pub(crate) fn did_remove(&self, name: &str) {
        self.watchers.lock().unwrap().send_event(&mut SingleNameEventProducer::removed(name));
    }

    /// Called to indicate a file or directory was added to this directory.
    pub(crate) fn did_add(&self, name: &str) {
        self.watchers.lock().unwrap().send_event(&mut SingleNameEventProducer::added(name));
    }
}

impl Drop for FxDirectory {
    fn drop(&mut self) {
        self.volume().cache().remove(self);
    }
}

impl FxNode for FxDirectory {
    fn object_id(&self) -> u64 {
        self.directory.object_id()
    }

    fn parent(&self) -> Option<Arc<FxDirectory>> {
        self.parent.as_ref().map(|p| p.lock().unwrap().clone())
    }

    fn set_parent(&self, parent: Arc<FxDirectory>) {
        match &self.parent {
            Some(p) => *p.lock().unwrap() = parent,
            None => panic!("Called set_parent on root node"),
        }
    }

    fn open_count_add_one(&self) {}

    fn open_count_sub_one(&self) {}
}

#[async_trait]
impl MutableDirectory for FxDirectory {
    async fn link(
        self: Arc<Self>,
        name: String,
        source_dir: Arc<dyn Any + Send + Sync>,
        source_name: &str,
    ) -> Result<(), Status> {
        let source_dir = source_dir.downcast::<Self>().unwrap();
        let store = self.store();
        let fs = store.filesystem().clone();
        // new_transaction will dedupe the locks if necessary.  In theory we only need a read lock
        // for the source directory, but we can leave that optimisation for now.
        let mut transaction = fs
            .new_transaction(
                &[
                    LockKey::object(store.store_object_id(), self.object_id()),
                    LockKey::object(store.store_object_id(), source_dir.object_id()),
                ],
                Options::default(),
            )
            .await
            .map_err(map_to_status)?;
        if self.is_deleted() {
            return Err(Status::ACCESS_DENIED);
        }
        let source_id =
            match source_dir.directory.lookup(source_name).await.map_err(map_to_status)? {
                Some((object_id, ObjectDescriptor::File)) => object_id,
                None => return Err(Status::NOT_FOUND),
                _ => return Err(Status::NOT_SUPPORTED),
            };
        if self.directory.lookup(&name).await.map_err(map_to_status)?.is_some() {
            return Err(Status::ALREADY_EXISTS);
        }
        self.directory
            .insert_child(&mut transaction, &name, source_id, ObjectDescriptor::File)
            .await
            .map_err(map_to_status)?;
        store.adjust_refs(&mut transaction, source_id, 1).await.map_err(map_to_status)?;
        transaction.commit_with_callback(|_| self.did_add(&name)).await.map_err(map_to_status)?;
        Ok(())
    }

    async fn unlink(self: Arc<Self>, name: &str, must_be_directory: bool) -> Result<(), Status> {
        let (mut transaction, _object_id, object_descriptor) =
            self.acquire_transaction_for_unlink(&[], name, true).await.map_err(map_to_status)?;
        if let ObjectDescriptor::Directory = object_descriptor {
        } else if must_be_directory {
            return Err(Status::NOT_DIR);
        }
        match directory::replace_child(&mut transaction, None, (self.directory(), name))
            .await
            .map_err(map_to_status)?
        {
            ReplacedChild::None => return Err(Status::NOT_FOUND),
            ReplacedChild::FileWithRemainingLinks(..) => {
                transaction
                    .commit_with_callback(|_| self.did_remove(name))
                    .await
                    .map_err(map_to_status)?;
            }
            ReplacedChild::File(id) => {
                transaction
                    .commit_with_callback(|_| self.did_remove(name))
                    .await
                    .map_err(map_to_status)?;
                // If purging fails , we should still return success, since the file will appear
                // unlinked at this point anyways.  The file should be cleaned up on a later mount.
                if let Err(e) = self.volume().maybe_purge_file(id).await {
                    warn!(error = e.as_value(), "Failed to purge file");
                }
            }
            ReplacedChild::Directory(id) => {
                transaction
                    .commit_with_callback(|_| {
                        self.did_remove(name);
                        self.volume().mark_directory_deleted(id);
                    })
                    .await
                    .map_err(map_to_status)?;
            }
        };
        Ok(())
    }

    async fn set_attrs(
        &self,
        flags: fio::NodeAttributeFlags,
        attrs: fio::NodeAttributes,
    ) -> Result<(), Status> {
        let crtime = flags
            .contains(fio::NodeAttributeFlags::CREATION_TIME)
            .then(|| Timestamp::from_nanos(attrs.creation_time));
        let mtime = flags
            .contains(fio::NodeAttributeFlags::MODIFICATION_TIME)
            .then(|| Timestamp::from_nanos(attrs.modification_time));
        if let (None, None) = (crtime.as_ref(), mtime.as_ref()) {
            return Ok(());
        }

        let fs = self.store().filesystem();
        let mut transaction = fs
            .clone()
            .new_transaction(
                &[LockKey::object(self.store().store_object_id(), self.directory.object_id())],
                Options { borrow_metadata_space: true, ..Default::default() },
            )
            .await
            .map_err(map_to_status)?;
        self.directory
            .update_attributes(&mut transaction, crtime, mtime, |_| {})
            .await
            .map_err(map_to_status)?;
        transaction.commit().await.map_err(map_to_status)?;
        Ok(())
    }

    async fn sync(&self) -> Result<(), Status> {
        // FDIO implements `syncfs` by calling sync on a directory, so replicate that behaviour.
        self.volume()
            .store()
            .filesystem()
            .sync(SyncOptions { flush_device: true, ..Default::default() })
            .await
            .map_err(map_to_status)
    }

    async fn rename(
        self: Arc<Self>,
        src_dir: Arc<dyn MutableDirectory>,
        src_name: Path,
        dst_name: Path,
    ) -> Result<(), Status> {
        if !src_name.is_single_component() || !dst_name.is_single_component() {
            return Err(Status::INVALID_ARGS);
        }
        let (src, dst) = (src_name.peek().unwrap(), dst_name.peek().unwrap());
        let src_dir =
            src_dir.into_any().downcast::<FxDirectory>().map_err(|_| Err(Status::NOT_DIR))?;

        // Acquire a transaction that locks |src_dir|, |self|, and |dst_name| if it exists.
        let store = self.store();
        let fs = store.filesystem();
        let (mut transaction, dst_id_and_descriptor) = match self
            .acquire_transaction_for_unlink(
                &[LockKey::object(store.store_object_id(), src_dir.object_id())],
                dst,
                false,
            )
            .await
        {
            Ok((transaction, id, descriptor)) => (transaction, Some((id, descriptor))),
            Err(e) if FxfsError::NotFound.matches(&e) => {
                let transaction = fs
                    .new_transaction(
                        &[
                            LockKey::object(store.store_object_id(), src_dir.object_id()),
                            LockKey::object(store.store_object_id(), self.object_id()),
                        ],
                        // It's ok to borrow metadata space here since after compaction, it should
                        // be a wash.
                        Options { borrow_metadata_space: true, ..Default::default() },
                    )
                    .await
                    .map_err(map_to_status)?;
                (transaction, None)
            }
            Err(e) => return Err(map_to_status(e)),
        };

        if self.is_deleted() {
            return Err(Status::NOT_FOUND);
        }

        let (moved_id, moved_descriptor) = src_dir
            .directory()
            .lookup(src)
            .await
            .map_err(map_to_status)?
            .ok_or(Status::NOT_FOUND)?;
        // Make sure the dst path is compatible with the moved node.
        if let ObjectDescriptor::File = moved_descriptor {
            if src_name.is_dir() || dst_name.is_dir() {
                return Err(Status::NOT_DIR);
            }
        }

        // Now that we've ensured that the dst path is compatible with the moved node, we can check
        // for the trivial case.
        if src_dir.object_id() == self.object_id() && src == dst {
            return Ok(());
        }

        if let Some((_, dst_descriptor)) = dst_id_and_descriptor.as_ref() {
            // dst is being overwritten; make sure it's a file iff src is.
            if dst_descriptor != &moved_descriptor {
                match dst_descriptor {
                    ObjectDescriptor::File => return Err(Status::NOT_DIR),
                    ObjectDescriptor::Directory => return Err(Status::NOT_FILE),
                    _ => return Err(Status::IO_DATA_INTEGRITY),
                };
            }
        }

        let moved_node = src_dir
            .volume()
            .get_or_load_node(moved_id, moved_descriptor.clone(), Some(src_dir.clone()))
            .await
            .map_err(map_to_status)?;

        if let ObjectDescriptor::Directory = moved_descriptor {
            // Lastly, ensure that self isn't a (transitive) child of the moved node.
            let mut node_opt = Some(self.clone());
            while let Some(node) = node_opt {
                if node.object_id() == moved_node.object_id() {
                    return Err(Status::INVALID_ARGS);
                }
                node_opt = node.parent();
            }
        }

        let replace_result = directory::replace_child(
            &mut transaction,
            Some((src_dir.directory(), src)),
            (self.directory(), dst),
        )
        .await
        .map_err(map_to_status)?;

        transaction
            .commit_with_callback(|_| {
                moved_node.set_parent(self.clone());
                src_dir.did_remove(src);

                match replace_result {
                    ReplacedChild::None => self.did_add(dst),
                    ReplacedChild::FileWithRemainingLinks(..) | ReplacedChild::File(_) => {
                        self.did_remove(dst);
                        self.did_add(dst);
                    }
                    ReplacedChild::Directory(id) => {
                        self.did_remove(dst);
                        self.did_add(dst);
                        self.volume().mark_directory_deleted(id);
                    }
                }
            })
            .await
            .map_err(map_to_status)?;

        if let ReplacedChild::File(id) = replace_result {
            self.volume().maybe_purge_file(id).await.map_err(map_to_status)?;
        }
        Ok(())
    }
}

impl DirectoryEntry for FxDirectory {
    fn open(
        self: Arc<Self>,
        _scope: ExecutionScope,
        flags: fio::OpenFlags,
        mode: u32,
        path: Path,
        server_end: ServerEnd<fio::NodeMarker>,
    ) {
        // Ignore the provided scope which might be for the parent pseudo filesystem and use the
        // volume's scope instead.
        let scope = self.volume().scope().clone();
        scope.clone().spawn_with_shutdown(move |shutdown| async move {
            match self.lookup(flags, mode, path).await {
                Err(e) => send_on_open_with_error(flags, server_end, map_to_status(e)),
                Ok(node) => {
                    if node.is::<FxDirectory>() {
                        MutableConnection::create_connection_async(
                            scope,
                            node.downcast::<FxDirectory>()
                                .unwrap_or_else(|_| unreachable!())
                                .take(),
                            flags,
                            server_end,
                            shutdown,
                        )
                        .await;
                    } else if node.is::<FxFile>() {
                        let node = node.downcast::<FxFile>().unwrap_or_else(|_| unreachable!());
                        if mode & fio::MODE_TYPE_MASK == fio::MODE_TYPE_BLOCK_DEVICE {
                            let mut server =
                                BlockServer::new(node, scope, server_end.into_channel());
                            let _ = server.run().await;
                        } else {
                            FxFile::create_connection(node, scope, flags, server_end, shutdown)
                                .await;
                        }
                    } else {
                        unreachable!();
                    }
                }
            };
        });
    }

    fn entry_info(&self) -> EntryInfo {
        EntryInfo::new(self.object_id(), fio::DirentType::Directory)
    }
}

#[async_trait]
impl Directory for FxDirectory {
    async fn read_dirents<'a>(
        &'a self,
        pos: &'a TraversalPosition,
        mut sink: Box<dyn Sink>,
    ) -> Result<(TraversalPosition, Box<dyn dirents_sink::Sealed>), Status> {
        if let TraversalPosition::End = pos {
            return Ok((TraversalPosition::End, sink.seal()));
        } else if let TraversalPosition::Index(_) = pos {
            // The VFS should never send this to us, since we never return it here.
            return Err(Status::BAD_STATE);
        }

        let store = self.store();
        let fs = store.filesystem();
        let _read_guard =
            fs.read_lock(&[LockKey::object(store.store_object_id(), self.object_id())]).await;
        if self.is_deleted() {
            return Ok((TraversalPosition::End, sink.seal()));
        }

        let starting_name = match pos {
            TraversalPosition::Start => {
                // Synthesize a "." entry if we're at the start of the stream.
                match sink
                    .append(&EntryInfo::new(fio::INO_UNKNOWN, fio::DirentType::Directory), ".")
                {
                    AppendResult::Ok(new_sink) => sink = new_sink,
                    AppendResult::Sealed(sealed) => {
                        // Note that the VFS should have yielded an error since the first entry
                        // didn't fit. This is defensive in case the VFS' behaviour changes, so that
                        // we return a reasonable value.
                        return Ok((TraversalPosition::Start, sealed));
                    }
                }
                ""
            }
            TraversalPosition::Name(name) => name,
            _ => unreachable!(),
        };

        let layer_set = self.store().tree().layer_set();
        let mut merger = layer_set.merger();
        let mut iter =
            self.directory.iter_from(&mut merger, starting_name).await.map_err(map_to_status)?;
        while let Some((name, object_id, object_descriptor)) = iter.get() {
            let entry_type = match object_descriptor {
                ObjectDescriptor::File => fio::DirentType::File,
                ObjectDescriptor::Directory => fio::DirentType::Directory,
                ObjectDescriptor::Volume => return Err(Status::IO_DATA_INTEGRITY),
            };
            let info = EntryInfo::new(object_id, entry_type);
            match sink.append(&info, name) {
                AppendResult::Ok(new_sink) => sink = new_sink,
                AppendResult::Sealed(sealed) => {
                    // We did *not* add the current entry to the sink (e.g. because the sink was
                    // full), so mark |name| as the next position so that it's the first entry we
                    // process on a subsequent call of read_dirents.
                    // Note that entries inserted between the previous entry and this entry before
                    // the next call to read_dirents would not be included in the results (but
                    // there's no requirement to include them anyways).
                    return Ok((TraversalPosition::Name(name.to_owned()), sealed));
                }
            }
            iter.advance().await.map_err(map_to_status)?;
        }
        Ok((TraversalPosition::End, sink.seal()))
    }

    fn register_watcher(
        self: Arc<Self>,
        scope: ExecutionScope,
        mask: fio::WatchMask,
        watcher: DirectoryWatcher,
    ) -> Result<(), Status> {
        let controller =
            self.watchers.lock().unwrap().add(scope.clone(), self.clone(), mask, watcher);
        if mask.contains(fio::WatchMask::EXISTING) && !self.is_deleted() {
            scope.spawn(async move {
                let layer_set = self.store().tree().layer_set();
                let mut merger = layer_set.merger();
                let mut iter = match self.directory.iter_from(&mut merger, "").await {
                    Ok(iter) => iter,
                    Err(e) => {
                        error!(error = e.as_value(), "Failed to iterate directory for watch",);
                        // TODO(fxbug.dev/96086): This really should close the watcher connection
                        // with an epitaph so that the watcher knows.
                        return;
                    }
                };
                // TODO(fxbug.dev/96087): It is possible that we'll duplicate entries that are added
                // as we iterate over directories.  I suspect fixing this might be non-trivial.
                controller.send_event(&mut SingleNameEventProducer::existing("."));
                while let Some((name, _, _)) = iter.get() {
                    controller.send_event(&mut SingleNameEventProducer::existing(name));
                    if let Err(e) = iter.advance().await {
                        error!(error = e.as_value(), "Failed to iterate directory for watch",);
                        return;
                    }
                }
                controller.send_event(&mut SingleNameEventProducer::idle());
            });
        }
        Ok(())
    }

    fn unregister_watcher(self: Arc<Self>, key: usize) {
        self.watchers.lock().unwrap().remove(key);
    }

    async fn get_attrs(&self) -> Result<fio::NodeAttributes, Status> {
        let props = self.directory.get_properties().await.map_err(map_to_status)?;
        Ok(fio::NodeAttributes {
            mode: fio::MODE_TYPE_DIRECTORY
                | rights_to_posix_mode_bits(/*r*/ true, /*w*/ true, /*x*/ false),
            id: self.directory.object_id(),
            content_size: props.data_attribute_size,
            storage_size: props.allocated_size,
            // +1 for the '.' reference, and 1 for each sub-directory.
            link_count: props.refs + 1 + props.sub_dirs,
            creation_time: props.creation_time.as_nanos(),
            modification_time: props.modification_time.as_nanos(),
        })
    }

    fn close(&self) -> Result<(), Status> {
        Ok(())
    }

    fn query_filesystem(&self) -> Result<fio::FilesystemInfo, Status> {
        let store = self.directory.store();
        Ok(info_to_filesystem_info(
            store.filesystem().get_info(),
            store.filesystem().block_size(),
            store.object_count(),
            self.volume().id(),
        ))
    }
}

#[cfg(test)]
mod tests {
    use {
        crate::platform::fuchsia::testing::{
            close_dir_checked, close_file_checked, open_dir, open_dir_checked, open_file,
            open_file_checked, TestFixture,
        },
        fidl_fuchsia_io as fio, fuchsia_async as fasync,
        fuchsia_fs::directory::{DirEntry, DirentKind},
        fuchsia_fs::file,
        fuchsia_zircon::Status,
        rand::Rng,
        std::{sync::Arc, time::Duration},
        storage_device::{fake_device::FakeDevice, DeviceHolder},
    };

    #[fasync::run_singlethreaded(test)]
    async fn test_open_root_dir() {
        let fixture = TestFixture::new().await;
        let root = fixture.root();
        root.describe_deprecated().await.expect("Describe failed");
        fixture.close().await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_create_dir_persists() {
        let mut device = DeviceHolder::new(FakeDevice::new(8192, 512));
        for i in 0..2 {
            let fixture = TestFixture::open(device, /*format=*/ i == 0, true).await;
            let root = fixture.root();

            let flags = if i == 0 {
                fio::OpenFlags::CREATE | fio::OpenFlags::RIGHT_READABLE
            } else {
                fio::OpenFlags::RIGHT_READABLE
            };
            let dir = open_dir_checked(&root, flags, fio::MODE_TYPE_DIRECTORY, "foo").await;
            close_dir_checked(dir).await;

            device = fixture.close().await;
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_open_nonexistent_file() {
        let fixture = TestFixture::new().await;
        let root = fixture.root();

        assert_eq!(
            open_file(&root, fio::OpenFlags::RIGHT_READABLE, fio::MODE_TYPE_FILE, "foo")
                .await
                .expect_err("Open succeeded")
                .root_cause()
                .downcast_ref::<Status>()
                .expect("No status"),
            &Status::NOT_FOUND,
        );

        fixture.close().await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_create_file() {
        let fixture = TestFixture::new().await;
        let root = fixture.root();

        let f = open_file_checked(
            &root,
            fio::OpenFlags::CREATE | fio::OpenFlags::RIGHT_READABLE,
            fio::MODE_TYPE_FILE,
            "foo",
        )
        .await;
        close_file_checked(f).await;

        let f =
            open_file_checked(&root, fio::OpenFlags::RIGHT_READABLE, fio::MODE_TYPE_FILE, "foo")
                .await;
        close_file_checked(f).await;

        fixture.close().await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_create_dir_nested() {
        let fixture = TestFixture::new().await;
        let root = fixture.root();

        let d = open_dir_checked(
            &root,
            fio::OpenFlags::CREATE
                | fio::OpenFlags::RIGHT_READABLE
                | fio::OpenFlags::RIGHT_WRITABLE,
            fio::MODE_TYPE_DIRECTORY,
            "foo",
        )
        .await;
        close_dir_checked(d).await;

        let d = open_dir_checked(
            &root,
            fio::OpenFlags::CREATE | fio::OpenFlags::RIGHT_READABLE,
            fio::MODE_TYPE_DIRECTORY,
            "foo/bar",
        )
        .await;
        close_dir_checked(d).await;

        let d = open_dir_checked(
            &root,
            fio::OpenFlags::RIGHT_READABLE,
            fio::MODE_TYPE_DIRECTORY,
            "foo/bar",
        )
        .await;
        close_dir_checked(d).await;

        fixture.close().await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_strict_create_file_fails_if_present() {
        let fixture = TestFixture::new().await;
        let root = fixture.root();

        let f = open_file_checked(
            &root,
            fio::OpenFlags::CREATE
                | fio::OpenFlags::CREATE_IF_ABSENT
                | fio::OpenFlags::RIGHT_READABLE,
            fio::MODE_TYPE_FILE,
            "foo",
        )
        .await;
        close_file_checked(f).await;

        assert_eq!(
            open_file(
                &root,
                fio::OpenFlags::CREATE
                    | fio::OpenFlags::CREATE_IF_ABSENT
                    | fio::OpenFlags::RIGHT_READABLE,
                fio::MODE_TYPE_FILE,
                "foo",
            )
            .await
            .expect_err("Open succeeded")
            .root_cause()
            .downcast_ref::<Status>()
            .expect("No status"),
            &Status::ALREADY_EXISTS,
        );

        fixture.close().await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_unlink_file_with_no_refs_immediately_freed() {
        let fixture = TestFixture::new().await;
        let root = fixture.root();

        let file = open_file_checked(
            &root,
            fio::OpenFlags::CREATE
                | fio::OpenFlags::RIGHT_READABLE
                | fio::OpenFlags::RIGHT_WRITABLE,
            fio::MODE_TYPE_FILE,
            "foo",
        )
        .await;

        // Fill up the file with a lot of data, so we can verify that the extents are freed.
        let buf = vec![0xaa as u8; 512];
        loop {
            match file::write(&file, buf.as_slice()).await {
                Ok(_) => {}
                Err(e) => {
                    if let fuchsia_fs::file::WriteError::WriteError(status) = e {
                        if status == Status::NO_SPACE {
                            break;
                        }
                    }
                    panic!("Unexpected write error {:?}", e);
                }
            }
        }

        close_file_checked(file).await;

        root.unlink("foo", fio::UnlinkOptions::EMPTY)
            .await
            .expect("FIDL call failed")
            .expect("unlink failed");

        assert_eq!(
            open_file(&root, fio::OpenFlags::RIGHT_READABLE, fio::MODE_TYPE_FILE, "foo")
                .await
                .expect_err("Open succeeded")
                .root_cause()
                .downcast_ref::<Status>()
                .expect("No status"),
            &Status::NOT_FOUND,
        );

        // Create another file so we can verify that the extents were actually freed.
        let file = open_file_checked(
            &root,
            fio::OpenFlags::CREATE
                | fio::OpenFlags::RIGHT_READABLE
                | fio::OpenFlags::RIGHT_WRITABLE,
            fio::MODE_TYPE_FILE,
            "bar",
        )
        .await;
        let buf = vec![0xaa as u8; 8192];
        file::write(&file, buf.as_slice()).await.expect("Failed to write new file");
        close_file_checked(file).await;

        fixture.close().await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_unlink_file() {
        let fixture = TestFixture::new().await;
        let root = fixture.root();

        let file = open_file_checked(
            &root,
            fio::OpenFlags::CREATE
                | fio::OpenFlags::RIGHT_READABLE
                | fio::OpenFlags::RIGHT_WRITABLE,
            fio::MODE_TYPE_FILE,
            "foo",
        )
        .await;
        close_file_checked(file).await;

        root.unlink("foo", fio::UnlinkOptions::EMPTY)
            .await
            .expect("FIDL call failed")
            .expect("unlink failed");

        assert_eq!(
            open_file(&root, fio::OpenFlags::RIGHT_READABLE, fio::MODE_TYPE_FILE, "foo")
                .await
                .expect_err("Open succeeded")
                .root_cause()
                .downcast_ref::<Status>()
                .expect("No status"),
            &Status::NOT_FOUND,
        );

        fixture.close().await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_unlink_file_with_active_references() {
        let fixture = TestFixture::new().await;
        let root = fixture.root();

        let file = open_file_checked(
            &root,
            fio::OpenFlags::CREATE
                | fio::OpenFlags::RIGHT_READABLE
                | fio::OpenFlags::RIGHT_WRITABLE,
            fio::MODE_TYPE_FILE,
            "foo",
        )
        .await;

        let buf = vec![0xaa as u8; 512];
        file::write(&file, buf.as_slice()).await.expect("write failed");

        root.unlink("foo", fio::UnlinkOptions::EMPTY)
            .await
            .expect("FIDL call failed")
            .expect("unlink failed");

        // The child should immediately appear unlinked...
        assert_eq!(
            open_file(&root, fio::OpenFlags::RIGHT_READABLE, fio::MODE_TYPE_FILE, "foo")
                .await
                .expect_err("Open succeeded")
                .root_cause()
                .downcast_ref::<Status>()
                .expect("No status"),
            &Status::NOT_FOUND,
        );

        // But its contents should still be readable from the other handle.
        file.seek(fio::SeekOrigin::Start, 0)
            .await
            .expect("seek failed")
            .map_err(Status::from_raw)
            .expect("seek error");
        let rbuf = file::read(&file).await.expect("read failed");
        assert_eq!(rbuf, buf);
        close_file_checked(file).await;

        fixture.close().await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_unlink_dir_with_children_fails() {
        let fixture = TestFixture::new().await;
        let root = fixture.root();

        let dir = open_dir_checked(
            &root,
            fio::OpenFlags::CREATE
                | fio::OpenFlags::RIGHT_READABLE
                | fio::OpenFlags::RIGHT_WRITABLE,
            fio::MODE_TYPE_DIRECTORY,
            "foo",
        )
        .await;
        let f = open_file_checked(
            &dir,
            fio::OpenFlags::CREATE | fio::OpenFlags::RIGHT_READABLE,
            fio::MODE_TYPE_FILE,
            "bar",
        )
        .await;
        close_file_checked(f).await;

        assert_eq!(
            Status::from_raw(
                root.unlink("foo", fio::UnlinkOptions::EMPTY)
                    .await
                    .expect("FIDL call failed")
                    .expect_err("unlink succeeded")
            ),
            Status::NOT_EMPTY
        );

        dir.unlink("bar", fio::UnlinkOptions::EMPTY)
            .await
            .expect("FIDL call failed")
            .expect("unlink failed");
        root.unlink("foo", fio::UnlinkOptions::EMPTY)
            .await
            .expect("FIDL call failed")
            .expect("unlink failed");

        close_dir_checked(dir).await;

        fixture.close().await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_unlink_dir_makes_directory_immutable() {
        let fixture = TestFixture::new().await;
        let root = fixture.root();

        let dir = open_dir_checked(
            &root,
            fio::OpenFlags::CREATE
                | fio::OpenFlags::RIGHT_READABLE
                | fio::OpenFlags::RIGHT_WRITABLE,
            fio::MODE_TYPE_DIRECTORY,
            "foo",
        )
        .await;

        root.unlink("foo", fio::UnlinkOptions::EMPTY)
            .await
            .expect("FIDL call failed")
            .expect("unlink failed");

        assert_eq!(
            open_file(
                &dir,
                fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::CREATE,
                fio::MODE_TYPE_FILE,
                "bar"
            )
            .await
            .expect_err("Create file succeeded")
            .root_cause()
            .downcast_ref::<Status>()
            .expect("No status"),
            &Status::ACCESS_DENIED,
        );

        close_dir_checked(dir).await;

        fixture.close().await;
    }

    #[fasync::run(10, test)]
    async fn test_unlink_directory_with_children_race() {
        let fixture = TestFixture::new().await;
        let root = fixture.root();

        const PARENT: &str = "foo";
        const CHILD: &str = "bar";
        const GRANDCHILD: &str = "baz";
        open_dir_checked(
            &root,
            fio::OpenFlags::CREATE
                | fio::OpenFlags::RIGHT_READABLE
                | fio::OpenFlags::RIGHT_WRITABLE,
            fio::MODE_TYPE_DIRECTORY,
            PARENT,
        )
        .await;

        let open_parent = || async {
            open_dir_checked(
                &root,
                fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
                fio::MODE_TYPE_DIRECTORY,
                PARENT,
            )
            .await
        };
        let parent = open_parent().await;

        // Each iteration proceeds as follows:
        //  - Initialize a directory foo/bar/. (This might still be around from the previous
        //    iteration, which is fine.)
        //  - In one task, try to unlink foo/bar/.
        //  - In another task, try to add a file foo/bar/baz.
        for _ in 0..100 {
            let d = open_dir_checked(
                &parent,
                fio::OpenFlags::CREATE
                    | fio::OpenFlags::RIGHT_READABLE
                    | fio::OpenFlags::RIGHT_WRITABLE,
                fio::MODE_TYPE_DIRECTORY,
                CHILD,
            )
            .await;
            close_dir_checked(d).await;

            let parent = open_parent().await;
            let deleter = fasync::Task::spawn(async move {
                let wait_time = rand::thread_rng().gen_range(0..5);
                fasync::Timer::new(Duration::from_millis(wait_time)).await;
                match parent
                    .unlink(CHILD, fio::UnlinkOptions::EMPTY)
                    .await
                    .expect("FIDL call failed")
                    .map_err(Status::from_raw)
                {
                    Ok(()) => {}
                    Err(Status::NOT_EMPTY) => {}
                    Err(e) => panic!("Unexpected status from unlink: {:?}", e),
                };
                close_dir_checked(parent).await;
            });

            let parent = open_parent().await;
            let writer = fasync::Task::spawn(async move {
                let child_or = open_dir(
                    &parent,
                    fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
                    fio::MODE_TYPE_DIRECTORY,
                    CHILD,
                )
                .await;
                if let Err(e) = &child_or {
                    // The directory was already deleted.
                    assert_eq!(
                        e.root_cause().downcast_ref::<Status>().expect("No status"),
                        &Status::NOT_FOUND
                    );
                    close_dir_checked(parent).await;
                    return;
                }
                let child = child_or.unwrap();
                child.describe_deprecated().await.expect("describe failed");
                match open_file(
                    &child,
                    fio::OpenFlags::CREATE | fio::OpenFlags::RIGHT_READABLE,
                    fio::MODE_TYPE_FILE,
                    GRANDCHILD,
                )
                .await
                {
                    Ok(grandchild) => {
                        grandchild.describe_deprecated().await.expect("describe failed");
                        close_file_checked(grandchild).await;
                        // We added the child before the directory was deleted; go ahead and
                        // clean up.
                        child
                            .unlink(GRANDCHILD, fio::UnlinkOptions::EMPTY)
                            .await
                            .expect("FIDL call failed")
                            .expect("unlink failed");
                    }
                    Err(e) => {
                        // The directory started to be deleted before we created a child.
                        // Make sure we get the right error.
                        assert_eq!(
                            e.root_cause().downcast_ref::<Status>().expect("No status"),
                            &Status::ACCESS_DENIED,
                        );
                    }
                };
                close_dir_checked(child).await;
                close_dir_checked(parent).await;
            });
            writer.await;
            deleter.await;
        }

        close_dir_checked(parent).await;
        fixture.close().await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_readdir() {
        let fixture = TestFixture::new().await;
        let root = fixture.root();

        let open_dir = || {
            open_dir_checked(
                &root,
                fio::OpenFlags::CREATE
                    | fio::OpenFlags::RIGHT_READABLE
                    | fio::OpenFlags::RIGHT_WRITABLE,
                fio::MODE_TYPE_DIRECTORY,
                "foo",
            )
        };
        let parent = Arc::new(open_dir().await);

        let files = ["eenie", "meenie", "minie", "moe"];
        for file in &files {
            let file = open_file_checked(
                parent.as_ref(),
                fio::OpenFlags::CREATE,
                fio::MODE_TYPE_FILE,
                file,
            )
            .await;
            close_file_checked(file).await;
        }
        let dirs = ["fee", "fi", "fo", "fum"];
        for dir in &dirs {
            let dir = open_dir_checked(
                parent.as_ref(),
                fio::OpenFlags::CREATE,
                fio::MODE_TYPE_DIRECTORY,
                dir,
            )
            .await;
            close_dir_checked(dir).await;
        }

        let readdir = |dir: Arc<fio::DirectoryProxy>| async move {
            let status = dir.rewind().await.expect("FIDL call failed");
            Status::ok(status).expect("rewind failed");
            let (status, buf) = dir.read_dirents(fio::MAX_BUF).await.expect("FIDL call failed");
            Status::ok(status).expect("read_dirents failed");
            let mut entries = vec![];
            for res in fuchsia_fs::directory::parse_dir_entries(&buf) {
                entries.push(res.expect("Failed to parse entry"));
            }
            entries
        };

        let mut expected_entries =
            vec![DirEntry { name: ".".to_owned(), kind: DirentKind::Directory }];
        expected_entries.extend(
            files.iter().map(|&name| DirEntry { name: name.to_owned(), kind: DirentKind::File }),
        );
        expected_entries.extend(
            dirs.iter()
                .map(|&name| DirEntry { name: name.to_owned(), kind: DirentKind::Directory }),
        );
        expected_entries.sort_unstable();
        assert_eq!(expected_entries, readdir(Arc::clone(&parent)).await);

        // Remove an entry.
        parent
            .unlink(&expected_entries.pop().unwrap().name, fio::UnlinkOptions::EMPTY)
            .await
            .expect("FIDL call failed")
            .expect("unlink failed");

        assert_eq!(expected_entries, readdir(Arc::clone(&parent)).await);

        close_dir_checked(Arc::try_unwrap(parent).unwrap()).await;
        fixture.close().await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_readdir_multiple_calls() {
        let fixture = TestFixture::new().await;
        let root = fixture.root();

        let parent = open_dir_checked(
            &root,
            fio::OpenFlags::CREATE
                | fio::OpenFlags::RIGHT_READABLE
                | fio::OpenFlags::RIGHT_WRITABLE,
            fio::MODE_TYPE_DIRECTORY,
            "foo",
        )
        .await;

        let files = ["a", "b"];
        for file in &files {
            let file =
                open_file_checked(&parent, fio::OpenFlags::CREATE, fio::MODE_TYPE_FILE, file).await;
            close_file_checked(file).await;
        }

        // TODO(fxbug.dev/95356): Magic number; can we get this from fuchsia.io?
        const DIRENT_SIZE: u64 = 10; // inode: u64, size: u8, kind: u8
        const BUFFER_SIZE: u64 = DIRENT_SIZE + 2; // Enough space for a 2-byte name.

        let parse_entries = |buf| {
            let mut entries = vec![];
            for res in fuchsia_fs::directory::parse_dir_entries(buf) {
                entries.push(res.expect("Failed to parse entry"));
            }
            entries
        };

        let expected_entries = vec![
            DirEntry { name: ".".to_owned(), kind: DirentKind::Directory },
            DirEntry { name: "a".to_owned(), kind: DirentKind::File },
        ];
        let (status, buf) = parent.read_dirents(2 * BUFFER_SIZE).await.expect("FIDL call failed");
        Status::ok(status).expect("read_dirents failed");
        assert_eq!(expected_entries, parse_entries(&buf));

        let expected_entries = vec![DirEntry { name: "b".to_owned(), kind: DirentKind::File }];
        let (status, buf) = parent.read_dirents(2 * BUFFER_SIZE).await.expect("FIDL call failed");
        Status::ok(status).expect("read_dirents failed");
        assert_eq!(expected_entries, parse_entries(&buf));

        // Subsequent calls yield nothing.
        let expected_entries: Vec<DirEntry> = vec![];
        let (status, buf) = parent.read_dirents(2 * BUFFER_SIZE).await.expect("FIDL call failed");
        Status::ok(status).expect("read_dirents failed");
        assert_eq!(expected_entries, parse_entries(&buf));

        close_dir_checked(parent).await;
        fixture.close().await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_set_attrs() {
        let fixture = TestFixture::new().await;
        let root = fixture.root();

        let dir = open_dir_checked(
            &root,
            fio::OpenFlags::CREATE
                | fio::OpenFlags::RIGHT_READABLE
                | fio::OpenFlags::RIGHT_WRITABLE,
            fio::MODE_TYPE_DIRECTORY,
            "foo",
        )
        .await;

        let (status, initial_attrs) = dir.get_attr().await.expect("FIDL call failed");
        Status::ok(status).expect("get_attr failed");

        let crtime = initial_attrs.creation_time ^ 1u64;
        let mtime = initial_attrs.modification_time ^ 1u64;

        let mut attrs = initial_attrs.clone();
        attrs.creation_time = crtime;
        attrs.modification_time = mtime;
        let status = dir
            .set_attr(fio::NodeAttributeFlags::CREATION_TIME, &mut attrs)
            .await
            .expect("FIDL call failed");
        Status::ok(status).expect("set_attr failed");

        let mut expected_attrs = initial_attrs.clone();
        expected_attrs.creation_time = crtime; // Only crtime is updated so far.
        let (status, attrs) = dir.get_attr().await.expect("FIDL call failed");
        Status::ok(status).expect("get_attr failed");
        assert_eq!(expected_attrs, attrs);

        let mut attrs = initial_attrs.clone();
        attrs.creation_time = 0u64; // This should be ignored since we don't set the flag.
        attrs.modification_time = mtime;
        let status = dir
            .set_attr(fio::NodeAttributeFlags::MODIFICATION_TIME, &mut attrs)
            .await
            .expect("FIDL call failed");
        Status::ok(status).expect("set_attr failed");

        let mut expected_attrs = initial_attrs.clone();
        expected_attrs.creation_time = crtime;
        expected_attrs.modification_time = mtime;
        let (status, attrs) = dir.get_attr().await.expect("FIDL call failed");
        Status::ok(status).expect("get_attr failed");
        assert_eq!(expected_attrs, attrs);

        close_dir_checked(dir).await;
        fixture.close().await;
    }
}
