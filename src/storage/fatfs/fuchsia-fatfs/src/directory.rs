// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::{
        file::FatFile,
        filesystem::{FatFilesystem, FatFilesystemInner},
        refs::{FatfsDirRef, FatfsFileRef},
        types::{Dir, DirEntry},
        util::{dos_to_unix_time, fatfs_error_to_status},
    },
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::{
        self as fio, NodeAttributes, NodeMarker, DIRENT_TYPE_DIRECTORY, DIRENT_TYPE_FILE,
        INO_UNKNOWN, MODE_TYPE_DIRECTORY, MODE_TYPE_MASK, OPEN_FLAG_CREATE,
        OPEN_FLAG_CREATE_IF_ABSENT, OPEN_FLAG_DIRECTORY, OPEN_FLAG_NOT_DIRECTORY,
    },
    fuchsia_async as fasync,
    fuchsia_syslog::fx_log_err,
    fuchsia_zircon::Status,
    std::{
        any::Any,
        collections::HashMap,
        fmt::Debug,
        sync::{Arc, RwLock, Weak},
    },
    vfs::{
        directory::{
            connection::io1::DerivedConnection,
            dirents_sink::{AppendResult, Sink},
            entry::{DirectoryEntry, EntryInfo},
            entry_container::{AsyncGetEntry, AsyncReadDirents, Directory, MutableDirectory},
            traversal_position::AlphabeticalTraversal,
        },
        execution_scope::ExecutionScope,
        filesystem::Filesystem,
        path::Path,
    },
};

#[derive(Clone, Debug)]
/// This enum is used to represent values which could be either a FatDirectory
/// or a FatFile. This holds a strong reference to the contained file/directory.
pub enum FatNode {
    Dir(Arc<FatDirectory>),
    File(Arc<FatFile>),
}

impl FatNode {
    /// Downgrade this FatNode into a WeakFatNode.
    fn downgrade(&self) -> WeakFatNode {
        match self {
            FatNode::Dir(a) => WeakFatNode::Dir(Arc::downgrade(a)),
            FatNode::File(b) => WeakFatNode::File(Arc::downgrade(b)),
        }
    }
}

/// The same as FatNode, but using a weak reference.
#[derive(Debug)]
enum WeakFatNode {
    Dir(Weak<FatDirectory>),
    File(Weak<FatFile>),
}

impl WeakFatNode {
    /// Try and upgrade this WeakFatNode to a FatNode. Returns None
    /// if the referenced object has been destroyed.
    pub fn upgrade(&self) -> Option<FatNode> {
        match self {
            WeakFatNode::Dir(a) => a.upgrade().map(|val| FatNode::Dir(val)),
            WeakFatNode::File(b) => b.upgrade().map(|val| FatNode::File(val)),
        }
    }
}

/// This wraps a directory on the FAT volume.
pub struct FatDirectory {
    /// The parent directory of this entry. Might be None if this is the root directory.
    parent: Option<Arc<FatDirectory>>,
    /// The underlying directory.
    dir: FatfsDirRef,
    /// We synchronise all accesses to directory on filesystem's lock().
    /// We always take the children lock first, and then the filesystem lock.
    filesystem: Arc<FatFilesystem>,
    /// We keep a cache of `FatDirectory`/`FatFile`s to ensure
    /// there is only ever one canonical version of each. This means
    /// we can use the reference count in the Arc<> to make sure rename, etc. operations are safe.
    // TODO(fxb/55292): handle case insensitivity.
    children: RwLock<HashMap<String, WeakFatNode>>,
}

impl FatDirectory {
    /// Create a new FatDirectory.
    pub(crate) fn new(
        dir: FatfsDirRef,
        parent: Option<Arc<FatDirectory>>,
        filesystem: Arc<FatFilesystem>,
    ) -> Arc<Self> {
        Arc::new(FatDirectory { parent, dir, filesystem, children: RwLock::new(HashMap::new()) })
    }

    /// Borrow the underlying fatfs `Dir` that corresponds to this directory.
    pub(crate) fn borrow_dir<'a>(&'a self, fs: &'a FatFilesystemInner) -> &'a Dir<'a> {
        self.dir.borrow(fs)
    }

    /// Borrow the underlying fatfs `DirEntry` that corresponds to this directory.
    fn borrow_dirent<'a>(&'a self, fs: &'a FatFilesystemInner) -> Option<&'a DirEntry<'a>> {
        self.dir.borrow_entry(fs)
    }

    /// Gets a child directory entry from the underlying fatfs implementation.
    pub(crate) fn find_child<'a>(
        &'a self,
        fs: &'a FatFilesystemInner,
        name: &str,
    ) -> Result<Option<DirEntry<'a>>, Status> {
        let dir = self.borrow_dir(fs);
        for entry in dir.iter().into_iter() {
            let entry = entry?;
            if &entry.file_name() == name {
                return Ok(Some(entry));
            }
        }
        Ok(None)
    }

    /// Lookup a child entry in the cache.
    pub fn cache_get(&self, name: &str) -> Option<FatNode> {
        // Note that we don't remove an entry even if its Arc<> has
        // gone away, to allow us to use the read-only lock here and avoid races.
        let children = self.children.read().unwrap();
        children.get(name).map(|entry| entry.upgrade()).flatten()
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
            if flags & OPEN_FLAG_CREATE_IF_ABSENT != 0 {
                return Err(Status::ALREADY_EXISTS);
            }
            return Ok(entry);
        };

        let node = {
            // Cache failed - try the real filesystem.
            let fs_lock = self.filesystem.lock().unwrap();
            let entry = self.find_child(&fs_lock, name)?;
            if let Some(entry) = entry {
                // Child entry exists! Make sure the result matches any requested flags.
                if flags & OPEN_FLAG_CREATE_IF_ABSENT != 0 {
                    return Err(Status::ALREADY_EXISTS);
                }
                // Make sure that entry type matches requested type, if a type was requested.
                if entry.is_file() && flags & OPEN_FLAG_DIRECTORY != 0 {
                    return Err(Status::NOT_DIR);
                } else if entry.is_dir() && flags & OPEN_FLAG_NOT_DIRECTORY != 0 {
                    return Err(Status::NOT_FILE);
                }

                if entry.is_dir() {
                    // Safe because we give the FatDirectory a FatFilesystem which ensures that the
                    // FatfsDirRef will not outlive its FatFilesystem.
                    let dir_ref = unsafe { FatfsDirRef::from(entry) };
                    FatNode::Dir(FatDirectory::new(
                        dir_ref,
                        Some(self.clone()),
                        self.filesystem.clone(),
                    ))
                } else {
                    // Safe because we give the FatFile a FatFilesystem which ensures that the
                    // FatfsFileRef will not outlive its FatFilesystem.
                    let file_ref = unsafe { FatfsFileRef::from(entry) };
                    FatNode::File(FatFile::new(file_ref, self.clone(), self.filesystem.clone()))
                }
            } else if flags & OPEN_FLAG_CREATE != 0 {
                // Child entry does not exist, but we've been asked to create it.
                let dir = self.borrow_dir(&fs_lock);
                if flags & OPEN_FLAG_DIRECTORY != 0
                    || (mode & MODE_TYPE_MASK == MODE_TYPE_DIRECTORY)
                {
                    dir.create_dir(name).map_err(fatfs_error_to_status)?;
                    // This should never fail.
                    let entry = self.find_child(&fs_lock, name)?.ok_or(Status::INTERNAL)?;
                    // Safe because we give the FatDirectory a FatFilesystem which ensures that the
                    // FatfsDirRef will not outlive its FatFilesystem.
                    let dir_ref = unsafe { FatfsDirRef::from(entry) };
                    FatNode::Dir(FatDirectory::new(
                        dir_ref,
                        Some(self.clone()),
                        self.filesystem.clone(),
                    ))
                } else {
                    dir.create_file(name).map_err(fatfs_error_to_status)?;
                    // This should never fail.
                    let entry = self.find_child(&fs_lock, name)?.ok_or(Status::INTERNAL)?;
                    // Safe because we give the FatFile a FatFilesystem which ensures that the
                    // FatfsFileRef will not outlive its FatFilesystem.
                    let file_ref = unsafe { FatfsFileRef::from(entry) };
                    FatNode::File(FatFile::new(file_ref, self.clone(), self.filesystem.clone()))
                }
            } else {
                // Not creating, and no existing entry => not found.
                return Err(Status::NOT_FOUND);
            }
        };

        // It's possible that two threads could check the cache at the same time, see that
        // there is no entry in the cache, and then add an entry in the cache.
        // To avoid accidentally making two Arcs referencing the same directory,
        // we check `children` for a valid entry once again. If one exists, we throw away the one
        // we made and return the entry that was added.
        let mut children = self.children.write().unwrap();
        match children.get(name).map(|v| v.upgrade()).flatten() {
            Some(existing_node) => Ok(existing_node),
            None => {
                children.insert(name.to_owned(), node.downgrade());
                Ok(node)
            }
        }
    }
}

impl Debug for FatDirectory {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("FatDirectory").field("parent", &self.parent).finish()
    }
}

impl Drop for FatDirectory {
    fn drop(&mut self) {
        // We need to drop the underlying Fatfs `Dir` while holding the filesystem lock,
        // to make sure that it's able to flush, etc. before getting dropped.
        let fs_lock = self.filesystem.lock().unwrap();
        self.dir.take(&fs_lock);
    }
}

impl MutableDirectory for FatDirectory {
    fn link(&self, _name: String, _entry: Arc<dyn DirectoryEntry>) -> Result<(), Status> {
        Err(Status::NOT_SUPPORTED)
    }

    fn unlink(&self, name: String) -> Result<(), Status> {
        // TODO(fxb/55465): To properly implement this, we need a way to keep the file while
        // it's still connected. For now, refuse to remove a file if anyone else is looking at it.
        // We could mark files as "hidden", and refuse to serve any new connections to them?
        match self.cache_get(&name) {
            Some(_) => {
                // This reference is still alive, so we can't safely delete it.
                return Err(Status::UNAVAILABLE);
            }
            // The file is not currently open by anyone, so it's safe to continue.
            None => {}
        }

        let fs_lock = self.filesystem.lock().unwrap();
        self.borrow_dir(&fs_lock).remove(&name).map_err(fatfs_error_to_status)
    }

    fn set_attrs(&self, _flags: u32, _attrs: NodeAttributes) -> Result<(), Status> {
        Err(Status::NOT_SUPPORTED)
    }

    fn get_filesystem(&self) -> Arc<dyn Filesystem> {
        self.filesystem.clone()
    }

    fn into_any(self: Arc<Self>) -> Arc<dyn Any + Sync + Send> {
        self as Arc<dyn Any + Sync + Send>
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
                                server_end
                                    .close_with_epitaph(e)
                                    .unwrap_or_else(|e| fx_log_err!("Failed to fail: {:?}", e));
                                return;
                            }
                        }
                    }
                    FatNode::File(_) => {
                        server_end
                            .close_with_epitaph(Status::NOT_DIR)
                            .unwrap_or_else(|e| fx_log_err!("Failed to fail: {:?}", e));
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

    fn read_dirents(
        self: Arc<Self>,
        pos: AlphabeticalTraversal,
        sink: Box<dyn Sink>,
    ) -> AsyncReadDirents {
        let fs_lock = self.filesystem.lock().unwrap();
        let dir = self.borrow_dir(&fs_lock);
        // Figure out where the last call to read_dirents() got up to...
        let last_name = match pos {
            AlphabeticalTraversal::Dot => ".".to_owned(),
            AlphabeticalTraversal::Name(name) => name,
            AlphabeticalTraversal::End => {
                return AsyncReadDirents::Immediate(Ok(sink.seal(AlphabeticalTraversal::End)))
            }
        };

        // Get all the entries in this directory.
        let mut entries: Vec<_> = dir
            .iter()
            .filter_map(|entry| {
                // TODO handle errors.
                let entry = entry.unwrap();
                let name = entry.file_name();
                if &name == ".." {
                    None
                } else if &last_name == "." || &last_name < &name {
                    let entry_type =
                        if entry.is_dir() { DIRENT_TYPE_DIRECTORY } else { DIRENT_TYPE_FILE };
                    Some((name, EntryInfo::new(INO_UNKNOWN, entry_type)))
                } else {
                    None
                }
            })
            .collect();
        // Sort them by alphabetical order.
        entries.sort_by(|a, b| a.0.partial_cmp(&b.0).unwrap());

        // Iterate through the entries, adding them one by one to the sink.
        let mut cur_sink = sink;
        for (name, info) in entries.into_iter() {
            let result = cur_sink
                .append(&info, &name.clone(), &|| AlphabeticalTraversal::Name(name.clone()));

            match result {
                AppendResult::Ok(new_sink) => cur_sink = new_sink,
                AppendResult::Sealed(sealed) => return AsyncReadDirents::Immediate(Ok(sealed)),
            }
        }

        return AsyncReadDirents::Immediate(Ok(cur_sink.seal(AlphabeticalTraversal::End)));
    }

    fn register_watcher(
        self: Arc<Self>,
        _scope: ExecutionScope,
        _mask: u32,
        _channel: fasync::Channel,
    ) -> Status {
        // TODO(simonshields): add watcher support.
        panic!("Not implemented");
    }

    fn unregister_watcher(self: Arc<Self>, _key: usize) {}

    fn get_attrs(&self) -> Result<NodeAttributes, Status> {
        let fs_lock = self.filesystem.lock().unwrap();
        let dirent = self.borrow_dirent(&fs_lock).unwrap();

        let creation_time = dos_to_unix_time(dirent.created());
        let modification_time = dos_to_unix_time(dirent.modified());
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
        std::sync::Mutex,
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

    struct SinkInner {
        max_size: usize,
        entries: Vec<(String, EntryInfo)>,
        sealed_pos: AlphabeticalTraversal,
    }

    #[derive(Clone)]
    struct DummySink {
        inner: Arc<Mutex<SinkInner>>,
    }

    impl DummySink {
        pub fn new(max_size: usize) -> Self {
            DummySink {
                inner: Arc::new(Mutex::new(SinkInner {
                    max_size,
                    entries: Vec::with_capacity(max_size),
                    sealed_pos: AlphabeticalTraversal::Dot,
                })),
            }
        }

        pub fn reset(&self) {
            self.inner.lock().unwrap().entries.clear();
        }
    }

    impl Sink for DummySink {
        fn append(
            self: Box<Self>,
            entry: &EntryInfo,
            name: &str,
            pos: &dyn Fn() -> AlphabeticalTraversal,
        ) -> AppendResult {
            let inner_arc = self.inner.clone();
            let mut inner = inner_arc.lock().unwrap();
            inner.entries.push((name.to_owned(), entry.clone()));

            if inner.entries.len() == inner.max_size {
                // seal the sink
                inner.sealed_pos = pos();
                AppendResult::Sealed(self)
            } else {
                AppendResult::Ok(self)
            }
        }

        fn seal(self: Box<Self>, pos: AlphabeticalTraversal) -> Box<dyn Sealed> {
            self.inner.lock().unwrap().sealed_pos = pos;
            self
        }
    }

    impl Sealed for DummySink {
        fn open(self: Box<Self>) -> Box<dyn Any> {
            todo!();
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

        let sink = Box::new(DummySink::new(2));
        let mut pos = AlphabeticalTraversal::Dot;
        match dir.clone().read_dirents(pos, sink.clone()) {
            AsyncReadDirents::Immediate(Ok(_)) => {
                let inner = sink.inner.lock().unwrap();
                assert_eq!(
                    inner.entries,
                    vec![
                        ("aaa".to_owned(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_FILE)),
                        (
                            "directory".to_owned(),
                            EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)
                        ),
                    ]
                );
                pos = inner.sealed_pos.clone();
                assert_eq!(pos, AlphabeticalTraversal::Name("directory".to_owned()));
            }
            _ => panic!("Unexpected result"),
        }

        // Read the next two entries.
        sink.reset();
        match dir.clone().read_dirents(pos, sink.clone()) {
            AsyncReadDirents::Immediate(Ok(_)) => {
                let inner = sink.inner.lock().unwrap();
                assert_eq!(
                    inner.entries,
                    vec![
                        ("qwerty".to_owned(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_FILE)),
                        ("test_file".to_owned(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_FILE)),
                    ]
                );
                pos = inner.sealed_pos.clone();
                assert_eq!(pos, AlphabeticalTraversal::Name("test_file".to_owned()));
            }
            _ => panic!("Unexpected result"),
        }

        // Read the "end" entry.
        sink.reset();
        match dir.clone().read_dirents(pos, sink.clone()) {
            AsyncReadDirents::Immediate(Ok(_)) => {
                let inner = sink.inner.lock().unwrap();
                assert_eq!(inner.entries, vec![]);
                pos = inner.sealed_pos.clone();
                assert_eq!(pos, AlphabeticalTraversal::End);
            }
            _ => panic!("Unexpected result"),
        }
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

        let sink = Box::new(DummySink::new(30));
        let mut pos = AlphabeticalTraversal::Dot;
        match dir.clone().read_dirents(pos, sink.clone()) {
            AsyncReadDirents::Immediate(Ok(_)) => {
                let inner = sink.inner.lock().unwrap();
                assert_eq!(
                    inner.entries,
                    vec![
                        ("aaa".to_owned(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_FILE)),
                        (
                            "directory".to_owned(),
                            EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)
                        ),
                        ("qwerty".to_owned(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_FILE)),
                        ("test_file".to_owned(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_FILE)),
                    ]
                );
                pos = inner.sealed_pos.clone();
                assert_eq!(pos, AlphabeticalTraversal::End);
            }
            _ => panic!("Unexpected result"),
        }
    }
}
