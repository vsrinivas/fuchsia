// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::{
        file::FatFile,
        filesystem::{FatFilesystem, FatFilesystemInner},
        node::{FatNode, WeakFatNode},
        refs::{FatfsDirRef, FatfsFileRef},
        types::{Dir, DirEntry},
        util::{dos_to_unix_time, fatfs_error_to_status, unix_to_dos_time},
    },
    async_trait::async_trait,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::{
        self as fio, NodeAttributes, NodeMarker, DIRENT_TYPE_DIRECTORY, DIRENT_TYPE_FILE,
        INO_UNKNOWN, MODE_TYPE_DIRECTORY, MODE_TYPE_MASK, OPEN_FLAG_CREATE,
        OPEN_FLAG_CREATE_IF_ABSENT, OPEN_FLAG_DIRECTORY, WATCH_MASK_EXISTING,
    },
    fuchsia_async as fasync,
    fuchsia_zircon::Status,
    std::{
        any::Any,
        borrow::Borrow,
        cell::UnsafeCell,
        cmp::PartialEq,
        collections::HashMap,
        fmt::Debug,
        hash::{Hash, Hasher},
        sync::{Arc, RwLock},
    },
    vfs::{
        common::send_on_open_with_error,
        directory::{
            connection::io1::DerivedConnection,
            dirents_sink::{self, AppendResult, Sink},
            entry::{DirectoryEntry, EntryInfo},
            entry_container::{AsyncGetEntry, Directory, MutableDirectory},
            traversal_position::TraversalPosition,
            watchers::{
                event_producers::{SingleNameEventProducer, StaticVecEventProducer},
                Watchers,
            },
        },
        execution_scope::ExecutionScope,
        filesystem::Filesystem,
        path::Path,
    },
};

fn check_open_flags_for_existing_entry(flags: u32) -> Result<(), Status> {
    if flags & OPEN_FLAG_CREATE_IF_ABSENT != 0 {
        return Err(Status::ALREADY_EXISTS);
    }
    // Other flags are verified by VFS's new_connection_validate_flags method.
    Ok(())
}

struct FatDirectoryData {
    /// The parent directory of this entry. Might be None if this is the root directory,
    /// or if this directory has been deleted.
    parent: Option<Arc<FatDirectory>>,
    /// We keep a cache of `FatDirectory`/`FatFile`s to ensure
    /// there is only ever one canonical version of each. This means
    /// we can use the reference count in the Arc<> to make sure rename, etc. operations are safe.
    children: HashMap<InsensitiveString, WeakFatNode>,
    /// True if this directory has been deleted.
    deleted: bool,
    watchers: Watchers,
}

// Whilst it's tempting to use the unicase crate, at time of writing, it had its own case tables,
// which might not match Rust's built-in tables (which is what fatfs uses).  It's important what we
// do here is consistent with the fatfs crate.  It would be nice if that were consistent with other
// implementations, but it probably isn't the end of the world if it isn't since we shouldn't have
// clients using obscure ranges of Unicode.
struct InsensitiveString(String);

impl Hash for InsensitiveString {
    fn hash<H: Hasher>(&self, hasher: &mut H) {
        for c in self.0.chars().flat_map(|c| c.to_uppercase()) {
            hasher.write_u32(c as u32);
        }
    }
}

impl PartialEq for InsensitiveString {
    fn eq(&self, other: &Self) -> bool {
        self.0
            .chars()
            .flat_map(|c| c.to_uppercase())
            .eq(other.0.chars().flat_map(|c| c.to_uppercase()))
    }
}

impl Eq for InsensitiveString {}

// A trait that allows us to find entries in our hash table using &str.
pub(crate) trait InsensitiveStringRef {
    fn as_str(&self) -> &str;
}

impl<'a> Borrow<dyn InsensitiveStringRef + 'a> for InsensitiveString {
    fn borrow(&self) -> &(dyn InsensitiveStringRef + 'a) {
        self
    }
}

impl<'a> Eq for (dyn InsensitiveStringRef + 'a) {}

impl<'a> PartialEq for (dyn InsensitiveStringRef + 'a) {
    fn eq(&self, other: &dyn InsensitiveStringRef) -> bool {
        self.as_str()
            .chars()
            .flat_map(|c| c.to_uppercase())
            .eq(other.as_str().chars().flat_map(|c| c.to_uppercase()))
    }
}

impl<'a> Hash for (dyn InsensitiveStringRef + 'a) {
    fn hash<H: Hasher>(&self, hasher: &mut H) {
        for c in self.as_str().chars().flat_map(|c| c.to_uppercase()) {
            hasher.write_u32(c as u32);
        }
    }
}

impl InsensitiveStringRef for &str {
    fn as_str(&self) -> &str {
        self
    }
}

impl InsensitiveStringRef for InsensitiveString {
    fn as_str(&self) -> &str {
        &self.0
    }
}

/// This wraps a directory on the FAT volume.
pub struct FatDirectory {
    /// The underlying directory.
    dir: UnsafeCell<FatfsDirRef>,
    /// We synchronise all accesses to directory on filesystem's lock().
    /// We always acquire the filesystem lock before the data lock, if the data lock is also going
    /// to be acquired.
    filesystem: Arc<FatFilesystem>,
    /// Other information about this FatDirectory that shares a lock.
    /// This should always be acquired after the filesystem lock if the filesystem lock is also
    /// going to be acquired.
    data: RwLock<FatDirectoryData>,
}

// The only member that isn't `Sync + Send` is the `dir` member.
// `dir` is protected by the lock on `filesystem`, so we can safely
// implement Sync + Send for FatDirectory.
unsafe impl Sync for FatDirectory {}
unsafe impl Send for FatDirectory {}

impl FatDirectory {
    /// Create a new FatDirectory.
    pub(crate) fn new(
        dir: FatfsDirRef,
        parent: Option<Arc<FatDirectory>>,
        filesystem: Arc<FatFilesystem>,
    ) -> Arc<Self> {
        Arc::new(FatDirectory {
            dir: UnsafeCell::new(dir),
            filesystem,
            data: RwLock::new(FatDirectoryData {
                parent,
                children: HashMap::new(),
                deleted: false,
                watchers: Watchers::new(),
            }),
        })
    }

    /// Borrow the underlying fatfs `Dir` that corresponds to this directory.
    pub(crate) fn borrow_dir<'a>(
        &'a self,
        fs: &'a FatFilesystemInner,
    ) -> Result<&'a Dir<'a>, Status> {
        unsafe { self.dir.get().as_ref() }.unwrap().borrow(fs).ok_or(Status::UNAVAILABLE)
    }

    /// Borrow the underlying fatfs `Dir` that corresponds to this directory.
    pub(crate) fn borrow_dir_mut<'a>(
        &'a self,
        fs: &'a FatFilesystemInner,
    ) -> Result<&'a mut Dir<'a>, Status> {
        unsafe { self.dir.get().as_mut() }.unwrap().borrow_mut(fs).ok_or(Status::UNAVAILABLE)
    }

    /// Gets a child directory entry from the underlying fatfs implementation.
    pub(crate) fn find_child<'a>(
        &'a self,
        fs: &'a FatFilesystemInner,
        name: &str,
    ) -> Result<Option<DirEntry<'a>>, Status> {
        let dir = self.borrow_dir(fs)?;
        for entry in dir.iter().into_iter() {
            let entry = entry?;
            if entry.eq_name(name) {
                return Ok(Some(entry));
            }
        }
        Ok(None)
    }

    /// Remove and detach a child node from this FatDirectory, returning it if it exists in the
    /// cache.  The caller must ensure that the corresponding filesystem entry is removed to prevent
    /// the item being added back to the cache, and must later attach() the returned node somewhere.
    pub fn remove_child(&self, fs: &FatFilesystemInner, name: &str) -> Option<FatNode> {
        let node = self.cache_remove(fs, name);
        if let Some(node) = node {
            node.detach(fs);
            Some(node)
        } else {
            None
        }
    }

    /// Add and attach a child node to this FatDirectory. The caller needs to make sure that the
    /// entry corresponds to a node on the filesystem, and that there is no existing entry with
    /// that name in the cache.
    pub fn add_child(
        self: &Arc<Self>,
        fs: &FatFilesystemInner,
        name: String,
        child: FatNode,
    ) -> Result<(), Status> {
        child.attach(self.clone(), &name, fs)?;
        // We only add back to the cache if the above succeeds, otherwise we have no
        // interest in serving more connections to a file that doesn't exist.
        let mut data = self.data.write().unwrap();
        assert!(
            data.children.insert(InsensitiveString(name), child.downgrade()).is_none(),
            "conflicting cache entries with the same name"
        );
        Ok(())
    }

    /// Remove a child entry from the cache, if it exists. The caller must hold the fs lock, as
    /// otherwise another thread could immediately add the entry back to the cache.
    fn cache_remove(&self, _fs: &FatFilesystemInner, name: &str) -> Option<FatNode> {
        let mut data = self.data.write().unwrap();
        data.children.remove(&name as &dyn InsensitiveStringRef).and_then(|entry| entry.upgrade())
    }

    /// Lookup a child entry in the cache.
    pub fn cache_get(&self, name: &str) -> Option<FatNode> {
        // Note that we don't remove an entry even if its Arc<> has
        // gone away, to allow us to use the read-only lock here and avoid races.
        let data = self.data.read().unwrap();
        data.children.get(&name as &dyn InsensitiveStringRef).and_then(|entry| entry.upgrade())
    }

    /// Flush to disk and invalidate the reference that's contained within this FatDir.
    /// Any operations on the directory will return Status::UNAVAILABLE until it is re-attached.
    pub fn detach(&self, fs: &FatFilesystemInner) {
        // Safe because we hold the fs lock.
        let dir = unsafe { self.dir.get().as_mut() }.unwrap();
        // This causes a flush to disk when the underlying fatfs Dir is dropped.
        dir.take(fs);
    }

    /// Re-open the underlying `FatfsDirRef` this directory represents, and attach to the given
    /// parent.
    pub fn attach(
        &self,
        new_parent: Arc<FatDirectory>,
        name: &str,
        fs: &FatFilesystemInner,
    ) -> Result<(), Status> {
        let mut data = self.data.write().unwrap();
        assert!(data.parent.replace(new_parent).is_some());

        let dir_ref = if let Some(parent) = data.parent.as_ref() {
            let entry = parent.find_child(fs, name)?.ok_or(Status::NOT_FOUND)?;
            // Safe because we have a reference to the FatFilesystem.
            unsafe { FatfsDirRef::from(entry.to_dir()) }
        } else {
            // Safe because we have a reference to the FatFilesystem.
            unsafe { FatfsDirRef::from(fs.root_dir()) }
        };

        // Safe because we hold the fs lock.
        let dir = unsafe { self.dir.get().as_mut().unwrap() };
        *dir = dir_ref;
        Ok(())
    }

    pub fn remove_from(&self, fs: &FatFilesystemInner, parent: &Dir<'_>) -> Result<(), Status> {
        let dir = self.borrow_dir_mut(fs)?;
        parent.unlink_dir(dir).map_err(fatfs_error_to_status)?;

        let mut data = self.data.write().unwrap();
        data.parent.take();
        data.deleted = true;
        Ok(())
    }

    /// Open a child entry with the given name.
    /// Flags can be any of the following, matching their fuchsia.io definitions:
    /// * OPEN_FLAG_CREATE
    /// * OPEN_FLAG_CREATE_IF_ABSENT
    /// * OPEN_FLAG_DIRECTORY
    /// * OPEN_FLAG_NOT_DIRECTORY
    pub(crate) fn open_child(
        self: Arc<Self>,
        name: &str,
        flags: u32,
        mode: u32,
    ) -> Result<FatNode, Status> {
        // First, check the cache.
        if let Some(entry) = self.cache_get(name) {
            check_open_flags_for_existing_entry(flags)?;
            return Ok(entry);
        };

        let fs_lock = self.filesystem.lock().unwrap();
        // Check the cache again in case we lost the race for the fs_lock.
        if let Some(entry) = self.cache_get(name) {
            check_open_flags_for_existing_entry(flags)?;
            return Ok(entry);
        };

        let mut created = false;
        let node = {
            // Cache failed - try the real filesystem.
            let entry = self.find_child(&fs_lock, name)?;
            if let Some(entry) = entry {
                check_open_flags_for_existing_entry(flags)?;

                if entry.is_dir() {
                    // Safe because we give the FatDirectory a FatFilesystem which ensures that the
                    // FatfsDirRef will not outlive its FatFilesystem.
                    let dir_ref = unsafe { FatfsDirRef::from(entry.to_dir()) };
                    FatNode::Dir(FatDirectory::new(
                        dir_ref,
                        Some(self.clone()),
                        self.filesystem.clone(),
                    ))
                } else {
                    // Safe because we give the FatFile a FatFilesystem which ensures that the
                    // FatfsFileRef will not outlive its FatFilesystem.
                    let file_ref = unsafe { FatfsFileRef::from(entry.to_file()) };
                    FatNode::File(FatFile::new(file_ref, self.clone(), self.filesystem.clone()))
                }
            } else if flags & OPEN_FLAG_CREATE != 0 {
                // Child entry does not exist, but we've been asked to create it.
                created = true;
                let dir = self.borrow_dir(&fs_lock)?;
                if flags & OPEN_FLAG_DIRECTORY != 0
                    || (mode & MODE_TYPE_MASK == MODE_TYPE_DIRECTORY)
                {
                    let dir = dir.create_dir(name).map_err(fatfs_error_to_status)?;
                    // Safe because we give the FatDirectory a FatFilesystem which ensures that the
                    // FatfsDirRef will not outlive its FatFilesystem.
                    let dir_ref = unsafe { FatfsDirRef::from(dir) };
                    FatNode::Dir(FatDirectory::new(
                        dir_ref,
                        Some(self.clone()),
                        self.filesystem.clone(),
                    ))
                } else {
                    let file = dir.create_file(name).map_err(fatfs_error_to_status)?;
                    // Safe because we give the FatFile a FatFilesystem which ensures that the
                    // FatfsFileRef will not outlive its FatFilesystem.
                    let file_ref = unsafe { FatfsFileRef::from(file) };
                    FatNode::File(FatFile::new(file_ref, self.clone(), self.filesystem.clone()))
                }
            } else {
                // Not creating, and no existing entry => not found.
                return Err(Status::NOT_FOUND);
            }
        };

        let mut data = self.data.write().unwrap();
        data.children.insert(InsensitiveString(name.to_owned()), node.downgrade());
        if created {
            data.watchers.send_event(&mut SingleNameEventProducer::added(name));
        }

        Ok(node)
    }

    /// True if this directory has been deleted.
    pub(crate) fn is_deleted(&self) -> bool {
        self.data.read().unwrap().deleted
    }

    /// Called to indicate a file or directory was removed from this directory.
    pub(crate) fn did_remove(&self, name: &str) {
        self.data.write().unwrap().watchers.send_event(&mut SingleNameEventProducer::removed(name));
    }

    /// Called to indicate a file or directory was added to this directory.
    pub(crate) fn did_add(&self, name: &str) {
        self.data.write().unwrap().watchers.send_event(&mut SingleNameEventProducer::added(name));
    }
}

impl Debug for FatDirectory {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("FatDirectory").field("parent", &self.data.read().unwrap().parent).finish()
    }
}

impl Drop for FatDirectory {
    fn drop(&mut self) {
        // We need to drop the underlying Fatfs `Dir` while holding the filesystem lock,
        // to make sure that it's able to flush, etc. before getting dropped.
        let fs_lock = self.filesystem.lock().unwrap();
        // Safe because we hold fs_lock.
        unsafe { self.dir.get().as_mut() }.unwrap().take(&fs_lock);
    }
}

impl MutableDirectory for FatDirectory {
    fn link(&self, _name: String, _entry: Arc<dyn DirectoryEntry>) -> Result<(), Status> {
        Err(Status::NOT_SUPPORTED)
    }

    fn unlink(&self, path: Path) -> Result<(), Status> {
        let fs_lock = self.filesystem.lock().unwrap();
        let parent = self.borrow_dir(&fs_lock)?;
        let name = path.peek().unwrap();
        if let Some(FatNode::File(_)) = self.cache_get(&name) {
            if path.is_dir() {
                return Err(Status::NOT_DIR);
            }
        }
        match self.cache_remove(&fs_lock, &name) {
            Some(entry) => {
                // The file or directory is already open elsewhere, so we remove the direntry but
                // don't delete the cluster chain that it actually occupies.
                match entry {
                    FatNode::File(file) => file.remove_from(&fs_lock, parent),
                    FatNode::Dir(dir) => dir.remove_from(&fs_lock, parent),
                }
            }
            // The file is not currently open by anyone, so it's safe to remove directly.
            None => {
                let dir = self.borrow_dir(&fs_lock)?;
                if path.is_dir() {
                    // Make sure the on-disk thing is a dir too.
                    let entry = self.find_child(&fs_lock, &name)?;
                    if !entry.ok_or(Status::NOT_FOUND)?.is_dir() {
                        return Err(Status::NOT_DIR);
                    }
                }
                dir.remove(&name).map_err(fatfs_error_to_status)
            }
        }?;
        self.data.write().unwrap().watchers.send_event(&mut SingleNameEventProducer::removed(name));
        Ok(())
    }

    fn set_attrs(&self, flags: u32, attrs: NodeAttributes) -> Result<(), Status> {
        let fs_lock = self.filesystem.lock().unwrap();
        let dir = self.borrow_dir_mut(&fs_lock)?;

        if flags & fio::NODE_ATTRIBUTE_FLAG_CREATION_TIME != 0 {
            dir.set_created(unix_to_dos_time(attrs.creation_time));
        }
        if flags & fio::NODE_ATTRIBUTE_FLAG_MODIFICATION_TIME != 0 {
            dir.set_modified(unix_to_dos_time(attrs.modification_time));
        }

        Ok(())
    }

    fn get_filesystem(&self) -> Arc<dyn Filesystem> {
        self.filesystem.clone()
    }

    fn into_any(self: Arc<Self>) -> Arc<dyn Any + Sync + Send> {
        self as Arc<dyn Any + Sync + Send>
    }

    fn sync(&self) -> Result<(), Status> {
        // TODO(fxb/55291): Support sync on root of fatfs volume.
        Ok(())
    }
}

impl DirectoryEntry for FatDirectory {
    fn open(
        self: Arc<Self>,
        scope: ExecutionScope,
        flags: u32,
        mode: u32,
        mut path: Path,
        server_end: ServerEnd<NodeMarker>,
    ) {
        if path.is_empty() {
            vfs::directory::mutable::connection::io1::MutableConnection::create_connection(
                scope, self, flags, mode, server_end,
            );
        } else {
            let mut cur_entry = FatNode::Dir(self);
            while !path.is_empty() {
                let (child_flags, child_mode) = if path.is_single_component() {
                    (flags, mode)
                } else {
                    (OPEN_FLAG_DIRECTORY, MODE_TYPE_DIRECTORY)
                };

                match cur_entry {
                    FatNode::Dir(entry) => {
                        let result = entry.clone().open_child(
                            &path.next().unwrap().to_owned(),
                            child_flags,
                            child_mode,
                        );
                        match result {
                            Ok(val) => cur_entry = val,
                            Err(e) => {
                                send_on_open_with_error(flags, server_end, e);
                                return;
                            }
                        }
                    }
                    FatNode::File(_) => {
                        send_on_open_with_error(flags, server_end, Status::NOT_DIR);
                        return;
                    }
                };
            }

            match cur_entry {
                FatNode::Dir(entry) => entry.clone().open(scope, flags, mode, path, server_end),
                FatNode::File(entry) => entry.clone().open(scope, flags, mode, path, server_end),
            };
        }
    }

    fn entry_info(&self) -> EntryInfo {
        EntryInfo::new(fio::INO_UNKNOWN, fio::DIRENT_TYPE_DIRECTORY)
    }

    fn can_hardlink(&self) -> bool {
        false
    }
}

#[async_trait]
impl Directory for FatDirectory {
    fn get_entry(self: Arc<Self>, name: String) -> AsyncGetEntry {
        match self.clone().open_child(&name, 0, 0) {
            Ok(FatNode::Dir(child)) => {
                AsyncGetEntry::Immediate(Ok(child as Arc<dyn DirectoryEntry>))
            }
            Ok(FatNode::File(child)) => {
                AsyncGetEntry::Immediate(Ok(child as Arc<dyn DirectoryEntry>))
            }
            Err(e) => AsyncGetEntry::Immediate(Err(e)),
        }
    }

    async fn read_dirents<'a>(
        &'a self,
        pos: &'a TraversalPosition,
        sink: Box<dyn Sink>,
    ) -> Result<(TraversalPosition, Box<dyn dirents_sink::Sealed>), Status> {
        if self.is_deleted() {
            return Ok((TraversalPosition::End, sink.seal()));
        }

        let fs_lock = self.filesystem.lock().unwrap();
        let dir = self.borrow_dir(&fs_lock)?;

        if let TraversalPosition::End = pos {
            return Ok((TraversalPosition::End, sink.seal()));
        }

        let filter = |name: &str| match pos {
            TraversalPosition::Start => true,
            TraversalPosition::Name(next_name) => name >= next_name.as_str(),
            _ => false,
        };

        // Get all the entries in this directory.
        let mut entries: Vec<_> = dir
            .iter()
            .filter_map(|maybe_entry| {
                maybe_entry
                    .map(|entry| {
                        let name = entry.file_name();
                        if &name == ".." || !filter(&name) {
                            None
                        } else {
                            let entry_type = if entry.is_dir() {
                                DIRENT_TYPE_DIRECTORY
                            } else {
                                DIRENT_TYPE_FILE
                            };
                            Some((name, EntryInfo::new(INO_UNKNOWN, entry_type)))
                        }
                    })
                    .transpose()
            })
            .collect::<std::io::Result<Vec<_>>>()?;

        // If it's the root directory, we need to synthesize a "." entry if appropriate.
        if self.data.read().unwrap().parent.is_none() && filter(".") {
            entries.push((".".to_owned(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)));
        }

        // Sort them by alphabetical order.
        entries.sort_by(|a, b| a.0.partial_cmp(&b.0).unwrap());

        // Iterate through the entries, adding them one by one to the sink.
        let mut cur_sink = sink;
        for (name, info) in entries.into_iter() {
            let result = cur_sink.append(&info, &name.clone());

            match result {
                AppendResult::Ok(new_sink) => cur_sink = new_sink,
                AppendResult::Sealed(sealed) => {
                    return Ok((TraversalPosition::Name(name.clone()), sealed));
                }
            }
        }

        return Ok((TraversalPosition::End, cur_sink.seal()));
    }

    fn register_watcher(
        self: Arc<Self>,
        scope: ExecutionScope,
        mask: u32,
        channel: fasync::Channel,
    ) -> Result<(), Status> {
        let fs_lock = self.filesystem.lock().unwrap();
        let mut data = self.data.write().unwrap();
        let is_deleted = data.deleted;
        let is_root = data.parent.is_none();
        if let Some(controller) = data.watchers.add(scope, self.clone(), mask, channel) {
            if mask & WATCH_MASK_EXISTING != 0 && !is_deleted {
                let entries = {
                    let dir = self.borrow_dir(&fs_lock)?;
                    let synthesized_dot = if is_root {
                        // We need to synthesize a "." entry.
                        Some(Ok(".".to_owned()))
                    } else {
                        None
                    };
                    synthesized_dot
                        .into_iter()
                        .chain(dir.iter().filter_map(|maybe_entry| {
                            maybe_entry
                                .map(|entry| {
                                    let name = entry.file_name();
                                    if &name == ".." {
                                        None
                                    } else {
                                        Some(name)
                                    }
                                })
                                .transpose()
                        }))
                        .collect::<std::io::Result<Vec<String>>>()
                        .map_err(fatfs_error_to_status)?
                };
                controller.send_event(&mut StaticVecEventProducer::existing(entries));
            }
            controller.send_event(&mut SingleNameEventProducer::idle());
        }
        Ok(())
    }

    fn unregister_watcher(self: Arc<Self>, key: usize) {
        self.data.write().unwrap().watchers.remove(key);
    }

    fn get_attrs(&self) -> Result<NodeAttributes, Status> {
        let fs_lock = self.filesystem.lock().unwrap();
        let dir = self.borrow_dir(&fs_lock)?;

        let creation_time = dos_to_unix_time(dir.created());
        let modification_time = dos_to_unix_time(dir.modified());
        Ok(NodeAttributes {
            // Mode is filled in by the caller.
            mode: 0,
            id: INO_UNKNOWN,
            content_size: 0,
            storage_size: 0,
            link_count: 1,
            creation_time,
            modification_time,
        })
    }
}

#[cfg(test)]
mod tests {
    // We only test things here that aren't covered by fs_tests.
    use {
        super::*,
        crate::tests::{TestDiskContents, TestFatDisk},
        vfs::directory::dirents_sink::{AppendResult, Sealed},
    };

    const TEST_DISK_SIZE: u64 = 2048 << 10; // 2048K

    #[test]
    fn test_link_fails() {
        let disk = TestFatDisk::empty_disk(TEST_DISK_SIZE);
        let structure = TestDiskContents::dir().add_child("test_file", "test file contents".into());
        structure.create(&disk.root_dir());

        let fs = disk.into_fatfs();
        let dir = fs.get_fatfs_root();
        if let AsyncGetEntry::Immediate { 0: entry } = dir.clone().get_entry("test_file".to_owned())
        {
            let entry = entry.expect("Getting test file");

            assert_eq!(dir.link("test2".to_owned(), entry).unwrap_err(), Status::NOT_SUPPORTED);
        } else {
            panic!("Unsupported AsyncGetEntry type");
        }
    }

    #[derive(Clone)]
    struct DummySink {
        max_size: usize,
        entries: Vec<(String, EntryInfo)>,
        sealed: bool,
    }

    impl DummySink {
        pub fn new(max_size: usize) -> Self {
            DummySink { max_size, entries: Vec::with_capacity(max_size), sealed: false }
        }

        fn from_sealed(sealed: Box<dyn dirents_sink::Sealed>) -> Box<DummySink> {
            sealed.into()
        }
    }

    impl From<Box<dyn dirents_sink::Sealed>> for Box<DummySink> {
        fn from(sealed: Box<dyn dirents_sink::Sealed>) -> Self {
            sealed.open().downcast::<DummySink>().unwrap()
        }
    }

    impl Sink for DummySink {
        fn append(mut self: Box<Self>, entry: &EntryInfo, name: &str) -> AppendResult {
            assert!(!self.sealed);
            if self.entries.len() == self.max_size {
                AppendResult::Sealed(self.seal())
            } else {
                self.entries.push((name.to_owned(), entry.clone()));
                AppendResult::Ok(self)
            }
        }

        fn seal(mut self: Box<Self>) -> Box<dyn Sealed> {
            self.sealed = true;
            self
        }
    }

    impl Sealed for DummySink {
        fn open(self: Box<Self>) -> Box<dyn Any> {
            self
        }
    }

    #[test]
    /// Test with a sink that can't handle the entire directory in one go.
    fn test_read_dirents_small_sink() {
        let disk = TestFatDisk::empty_disk(TEST_DISK_SIZE);
        let structure = TestDiskContents::dir()
            .add_child("test_file", "test file contents".into())
            .add_child("aaa", "this file is first".into())
            .add_child("qwerty", "hello".into())
            .add_child("directory", TestDiskContents::dir().add_child("a", "test".into()));
        structure.create(&disk.root_dir());

        let fs = disk.into_fatfs();
        let dir = fs.get_fatfs_root();

        let (pos, sealed) = futures::executor::block_on(
            dir.clone().read_dirents(&TraversalPosition::Start, Box::new(DummySink::new(4))),
        )
        .expect("read_dirents failed");
        assert_eq!(
            DummySink::from_sealed(sealed).entries,
            vec![
                (".".to_owned(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)),
                ("aaa".to_owned(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_FILE)),
                ("directory".to_owned(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)),
                ("qwerty".to_owned(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_FILE)),
            ]
        );

        // Read the next two entries.
        let (_, sealed) =
            futures::executor::block_on(dir.read_dirents(&pos, Box::new(DummySink::new(4))))
                .expect("read_dirents failed");
        assert_eq!(
            DummySink::from_sealed(sealed).entries,
            vec![("test_file".to_owned(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_FILE)),]
        );
    }

    #[test]
    /// Test with a sink that can hold everything.
    fn test_read_dirents_big_sink() {
        let disk = TestFatDisk::empty_disk(TEST_DISK_SIZE);
        let structure = TestDiskContents::dir()
            .add_child("test_file", "test file contents".into())
            .add_child("aaa", "this file is first".into())
            .add_child("qwerty", "hello".into())
            .add_child("directory", TestDiskContents::dir().add_child("a", "test".into()));
        structure.create(&disk.root_dir());

        let fs = disk.into_fatfs();
        let dir = fs.get_fatfs_root();

        let (_, sealed) = futures::executor::block_on(
            dir.read_dirents(&TraversalPosition::Start, Box::new(DummySink::new(30))),
        )
        .expect("read_dirents failed");
        assert_eq!(
            DummySink::from_sealed(sealed).entries,
            vec![
                (".".to_owned(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)),
                ("aaa".to_owned(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_FILE)),
                ("directory".to_owned(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)),
                ("qwerty".to_owned(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_FILE)),
                ("test_file".to_owned(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_FILE)),
            ]
        );
    }

    #[test]
    fn test_read_dirents_with_entry_that_sorts_before_dot() {
        let disk = TestFatDisk::empty_disk(TEST_DISK_SIZE);
        let structure = TestDiskContents::dir().add_child("!", "!".into());
        structure.create(&disk.root_dir());

        let fs = disk.into_fatfs();
        let dir = fs.get_fatfs_root();
        let (pos, sealed) = futures::executor::block_on(
            dir.clone().read_dirents(&TraversalPosition::Start, Box::new(DummySink::new(1))),
        )
        .expect("read_dirents failed");
        assert_eq!(
            DummySink::from_sealed(sealed).entries,
            vec![("!".to_owned(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_FILE))]
        );

        let (_, sealed) =
            futures::executor::block_on(dir.read_dirents(&pos, Box::new(DummySink::new(1))))
                .expect("read_dirents failed");
        assert_eq!(
            DummySink::from_sealed(sealed).entries,
            vec![(".".to_owned(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)),]
        );
    }
}
