use {
    crate::object_store,
    // crate::object_store,
    async_trait::async_trait,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::{self as fio, NodeAttributes, NodeMarker},
    fuchsia_async as fasync,
    fuchsia_zircon::Status,
    std::{any::Any, sync::Arc},
    vfs::{
        // common::send_on_open_with_error,
        directory::{
            // connection::{io1::DerivedConnection},
            dirents_sink::{self, Sink},
            entry::{DirectoryEntry, EntryInfo},
            entry_container::{AsyncGetEntry, Directory, MutableDirectory},
            traversal_position::TraversalPosition,
            /*            watchers::{
                event_producers::{SingleNameEventProducer, StaticVecEventProducer},
                Watchers,
            }, */
        },
        execution_scope::ExecutionScope,
        filesystem::{Filesystem, FilesystemRename},
        path::Path,
    },
};

struct FxVolume {}

impl FilesystemRename for FxVolume {
    fn rename(
        &self,
        _src_dir: Arc<dyn Any + Sync + Send + 'static>,
        _src_name: Path,
        _dst_dir: Arc<dyn Any + Sync + Send + 'static>,
        _dst_name: Path,
    ) -> Result<(), Status> {
        Err(Status::NOT_SUPPORTED)
    }
}

impl Filesystem for FxVolume {}

struct FxDirectory {
    filesystem: Arc<FxVolume>,
    directory: object_store::Directory,
}

fn map_to_status(std::io::Error) -> Status {
    Status::NOT_FOUND  // TODO
}

impl FxDirectory {
    fn lookup(
        self: &Arc<Self>,
        flags: u32,
        mode: u32,
        mut path: Path) -> Result<FxNode, Status> {
        let current_entry = self;
        while !path.is_empty() {
            let name = path.next().unwrap();
            let object_id = self.directory.lookup(name).map_err(map_to_status)?;
            
        }
            
            
        if !path.is_single_component() {
            send_on_open_with_error(flags, server_end, Status::NOT_FOUND)
        }
        self.directory.lookup(name)
        /*

        }
    }

}

impl MutableDirectory for FxDirectory {
    fn link(&self, _name: String, _entry: Arc<dyn DirectoryEntry>) -> Result<(), Status> {
        Err(Status::NOT_SUPPORTED)
    }

    fn unlink(&self, _path: Path) -> Result<(), Status> {
        Err(Status::NOT_SUPPORTED)
    }

    fn set_attrs(&self, _flags: u32, _attrs: NodeAttributes) -> Result<(), Status> {
        Err(Status::NOT_SUPPORTED)
    }

    fn get_filesystem(&self) -> &dyn Filesystem {
        &*self.filesystem
    }

    fn into_any(self: Arc<Self>) -> Arc<dyn Any + Sync + Send> {
        self as Arc<dyn Any + Sync + Send>
    }

    fn sync(&self) -> Result<(), Status> {
        // TODO(csuter): Support sync on root of fatfs volume.
        Ok(())
    }
}

impl DirectoryEntry for FxDirectory {
    fn open(
        self: Arc<Self>,
        _scope: ExecutionScope,
        _flags: u32,
        _mode: u32,
        _path: Path,
        _server_end: ServerEnd<NodeMarker>,
    ) {
        // TODO: empty path
        let mut closer = Closer::new(&self.filesystem);

        match self.lookup(flags, mode, path, &mut closer) {
            Err(e) => send_on_open_with_error(flags, server_end, e),
            Ok(FatNode::Dir(entry)) => {
                entry
                    .open_ref(&self.filesystem.lock().unwrap())
                    .expect("entry should already be open");
                vfs::directory::mutable::connection::io1::MutableConnection::create_connection(
                    scope,
                    OpenDirectory::new(entry),
                    flags,
                    mode,
                    server_end,
                );
            }
            Ok(FatNode::File(entry)) => {
                entry.clone().open(scope, flags, mode, Path::empty(), server_end);
            }
        };
         */
    }

    fn entry_info(&self) -> EntryInfo {
        EntryInfo::new(fio::INO_UNKNOWN, fio::DIRENT_TYPE_DIRECTORY)
    }

    fn can_hardlink(&self) -> bool {
        false
    }
}

#[async_trait]
impl Directory for FxDirectory {
    fn get_entry(self: Arc<Self>, _name: String) -> AsyncGetEntry {
        AsyncGetEntry::Immediate(Err(Status::NOT_FOUND))
        /*
        let mut closer = Closer::new(&self.filesystem);
        match self.open_child(&name, 0, 0, &mut closer) {
            Ok(FatNode::Dir(child)) => {
                AsyncGetEntry::Immediate(Ok(child as Arc<dyn DirectoryEntry>))
            }
            Ok(FatNode::File(child)) => {
                AsyncGetEntry::Immediate(Ok(child as Arc<dyn DirectoryEntry>))
            }
            Err(e) => AsyncGetEntry::Immediate(Err(e)),
        }
         */
    }

    async fn read_dirents<'a>(
        &'a self,
        _pos: &'a TraversalPosition,
        sink: Box<dyn Sink>,
    ) -> Result<(TraversalPosition, Box<dyn dirents_sink::Sealed>), Status> {
        return Ok((TraversalPosition::End, sink.seal()));

        /*
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
         */
    }

    fn register_watcher(
        self: Arc<Self>,
        _scope: ExecutionScope,
        _mask: u32,
        _channel: fasync::Channel,
    ) -> Result<(), Status> {
        /*
        let fs_lock = self.filesystem.lock().unwrap();
        let mut data = self.data.write().unwrap();
        let is_deleted = data.deleted;
        let is_root = data.parent.is_none();
        let controller = data.watchers.add(scope, self.clone(), mask, channel);
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
        Ok(())
         */
        Err(Status::NOT_SUPPORTED)
    }

    fn unregister_watcher(self: Arc<Self>, _key: usize) {
        // self.data.write().unwrap().watchers.remove(key);
    }

    fn get_attrs(&self) -> Result<NodeAttributes, Status> {
        Err(Status::NOT_SUPPORTED)
        /*
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
         */
    }

    fn close(&self) -> Result<(), Status> {
        // self.close_ref(&self.filesystem.lock().unwrap());
        Ok(())
    }
}
