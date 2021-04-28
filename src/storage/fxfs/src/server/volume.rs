// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        errors::FxfsError,
        object_store::{
            directory::{self, Directory, ObjectDescriptor},
            transaction::LockKey,
            HandleOptions, ObjectStore,
        },
        server::{
            directory::FxDirectory,
            errors::map_to_status,
            file::FxFile,
            node::{FxNode, GetResult, NodeCache},
        },
        volume::Volume,
    },
    anyhow::{bail, Error},
    async_trait::async_trait,
    fuchsia_zircon::Status,
    std::{any::Any, sync::Arc},
    vfs::{
        filesystem::{Filesystem, FilesystemRename},
        path::Path,
    },
};

/// FxVolume represents an opened volume. It is also a (weak) cache for all opened Nodes within the
/// volume.
pub struct FxVolume {
    cache: NodeCache,
    store: Arc<ObjectStore>,
    graveyard: Directory,
}

impl FxVolume {
    pub fn new(store: Arc<ObjectStore>, graveyard: Directory) -> Self {
        Self { cache: NodeCache::new(), store, graveyard }
    }

    pub fn store(&self) -> &Arc<ObjectStore> {
        &self.store
    }

    pub fn graveyard(&self) -> &Directory {
        &self.graveyard
    }

    pub fn cache(&self) -> &NodeCache {
        &self.cache
    }

    /// Attempts to get a node from the node cache. If the node wasn't present in the cache, loads
    /// the object from the object store, installing the returned node into the cache and returns the
    /// newly created FxNode backed by the loaded object.
    /// |parent| is only set on the node if the node was not present in the cache.  Otherwise, it is
    /// ignored.
    pub async fn get_or_load_node(
        self: &Arc<Self>,
        object_id: u64,
        object_descriptor: ObjectDescriptor,
        parent: Option<Arc<FxDirectory>>,
    ) -> Result<Arc<dyn FxNode>, Error> {
        match self.cache.get_or_reserve(object_id).await {
            GetResult::Node(node) => Ok(node),
            GetResult::Placeholder(placeholder) => {
                let node = match object_descriptor {
                    ObjectDescriptor::File => {
                        let file =
                            self.store.open_object(object_id, HandleOptions::default()).await?;
                        Arc::new(FxFile::new(file, self.clone())) as Arc<dyn FxNode>
                    }
                    ObjectDescriptor::Directory => {
                        let directory = self.store.open_directory(object_id).await?;
                        Arc::new(FxDirectory::new(self.clone(), parent, directory))
                            as Arc<dyn FxNode>
                    }
                    _ => bail!(FxfsError::Inconsistent),
                };
                placeholder.commit(&node);
                Ok(node)
            }
        }
    }
}

#[async_trait]
impl FilesystemRename for FxVolume {
    async fn rename(
        &self,
        src_dir: Arc<dyn Any + Sync + Send + 'static>,
        src_name: Path,
        dst_dir: Arc<dyn Any + Sync + Send + 'static>,
        dst_name: Path,
    ) -> Result<(), Status> {
        if !src_name.is_single_component() || !dst_name.is_single_component() {
            return Err(Status::INVALID_ARGS);
        }
        let (src, dst) = (src_name.peek().unwrap(), dst_name.peek().unwrap());
        let src_dir = src_dir.downcast::<FxDirectory>().map_err(|_| Err(Status::NOT_DIR))?;
        let dst_dir = dst_dir.downcast::<FxDirectory>().map_err(|_| Err(Status::NOT_DIR))?;

        // Acquire a transaction that locks |src_dir|, |dst_dir|, and |dst_name| if it exists.
        let fs = self.store.filesystem();
        let (mut transaction, dst_id_and_descriptor) = match dst_dir
            .acquire_transaction_for_unlink(
                &[LockKey::object(self.store.store_object_id(), src_dir.object_id())],
                dst,
            )
            .await
        {
            Ok((transaction, id, descriptor)) => (transaction, Some((id, descriptor))),
            Err(e) if FxfsError::NotFound.matches(&e) => {
                let transaction = fs
                    .new_transaction(&[
                        LockKey::object(self.store.store_object_id(), src_dir.object_id()),
                        LockKey::object(self.store.store_object_id(), dst_dir.object_id()),
                    ])
                    .await
                    .map_err(map_to_status)?;
                (transaction, None)
            }
            Err(e) => return Err(map_to_status(e)),
        };

        let (moved_id, moved_descriptor) =
            src_dir.directory().lookup(src).await.map_err(map_to_status)?;
        // Make sure the dst path is compatible with the moved node.
        if let ObjectDescriptor::File = moved_descriptor {
            if src_name.is_dir() || dst_name.is_dir() {
                return Err(Status::NOT_DIR);
            }
        }

        // Now that we've ensured that the dst path is compatible with the moved node, we can check
        // for the trivial case.
        if src_dir.object_id() == dst_dir.object_id() && src == dst {
            return Ok(());
        }

        if let Some((_, dst_descriptor)) = dst_id_and_descriptor.as_ref() {
            // dst is being overwritten; make sure it's a file iff src is.
            if (dst_descriptor != &moved_descriptor) {
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
            // Lastly, ensure that dst_dir isn't a (transitive) child of the moved node.
            let mut node_opt = Some(dst_dir.clone());
            while let Some(node) = node_opt {
                if node.object_id() == moved_node.object_id() {
                    return Err(Status::INVALID_ARGS);
                }
                node_opt = node.parent();
            }
        }

        // _old_node MUST be held until after the transaction is committed. See
        // FxDirectory::replace_child.
        let _old_node = if let Some((dst_id, dst_descriptor)) = dst_id_and_descriptor {
            dst_dir
                .replace_child(&mut transaction, dst, dst_id, dst_descriptor, Some((&src_dir, src)))
                .await
                .map_err(map_to_status)?
        } else {
            directory::replace_child(
                &mut transaction,
                Some((src_dir.directory(), src)),
                (dst_dir.directory(), dst),
                None,
            )
            .await
            .map_err(map_to_status)?;
            None
        };

        moved_node.set_parent(dst_dir.clone());

        transaction.commit().await;
        Ok(())
    }
}

impl Filesystem for FxVolume {}

pub struct FxVolumeAndRoot {
    volume: Arc<FxVolume>,
    root: Arc<dyn FxNode>,
}

impl FxVolumeAndRoot {
    pub async fn new(volume: Volume) -> Self {
        let (store, root_directory, graveyard) = volume.into();
        let root_object_id = root_directory.object_id();
        let volume = Arc::new(FxVolume::new(store, graveyard));
        let root: Arc<dyn FxNode> =
            Arc::new(FxDirectory::new(volume.clone(), None, root_directory));
        match volume.cache.get_or_reserve(root_object_id).await {
            GetResult::Node(_) => unreachable!(),
            GetResult::Placeholder(placeholder) => placeholder.commit(&root),
        }
        Self { volume, root }
    }

    pub fn volume(&self) -> &Arc<FxVolume> {
        &self.volume
    }

    pub fn root(&self) -> Arc<FxDirectory> {
        self.root.clone().into_any().downcast::<FxDirectory>().expect("Invalid type for root")
    }

    #[cfg(test)]
    pub(super) fn into_volume(self) -> Arc<FxVolume> {
        self.volume
    }
}

#[cfg(test)]
mod tests {
    use {
        crate::{
            object_store::FxFilesystem,
            server::{
                testing::{open_dir_validating, open_file_validating},
                volume::FxVolumeAndRoot,
            },
            testing::fake_device::FakeDevice,
            volume::root_volume,
        },
        fidl::endpoints::ServerEnd,
        fidl_fuchsia_io::{
            DirectoryMarker, MODE_TYPE_DIRECTORY, MODE_TYPE_FILE, OPEN_FLAG_CREATE,
            OPEN_FLAG_DIRECTORY, OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE,
        },
        fuchsia_async as fasync,
        fuchsia_zircon::Status,
        io_util::{read_file_bytes, write_file_bytes},
        std::sync::Arc,
        vfs::{
            directory::entry::DirectoryEntry, execution_scope::ExecutionScope, path::Path,
            registry::token_registry,
        },
    };

    #[fasync::run_singlethreaded(test)]
    async fn test_rename_different_dirs() {
        let registry = token_registry::Simple::new();
        let scope = ExecutionScope::build().token_registry(registry).new();
        let device = Arc::new(FakeDevice::new(2048, 512));
        let filesystem = FxFilesystem::new_empty(device.clone()).await.unwrap();
        let root_volume = root_volume(&filesystem).await.unwrap();
        let vol = FxVolumeAndRoot::new(root_volume.new_volume("vol").await.unwrap()).await;
        let (dir_proxy, dir_server_end) =
            fidl::endpoints::create_proxy::<DirectoryMarker>().expect("Create proxy to succeed");
        vol.root().clone().open(
            scope.clone(),
            OPEN_FLAG_DIRECTORY | OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            MODE_TYPE_DIRECTORY,
            Path::empty(),
            ServerEnd::new(dir_server_end.into_channel()),
        );

        let src = open_dir_validating(
            &dir_proxy,
            OPEN_FLAG_CREATE | OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            MODE_TYPE_DIRECTORY,
            "foo",
        )
        .await
        .expect("Create dir failed");

        let dst = open_dir_validating(
            &dir_proxy,
            OPEN_FLAG_CREATE | OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE | OPEN_FLAG_DIRECTORY,
            MODE_TYPE_DIRECTORY,
            "bar",
        )
        .await
        .expect("Create dir failed");

        open_file_validating(&dir_proxy, OPEN_FLAG_CREATE, MODE_TYPE_FILE, "foo/a")
            .await
            .expect("Create file failed");

        let (status, dst_token) = dst.get_token().await.expect("FIDL call failed");
        Status::ok(status).expect("get_token failed");
        Status::ok(src.rename("a", dst_token.unwrap(), "b").await.expect("FIDL call failed"))
            .expect("rename failed");

        assert_eq!(
            open_file_validating(&dir_proxy, 0, MODE_TYPE_FILE, "foo/a")
                .await
                .expect_err("Open succeeded")
                .root_cause()
                .downcast_ref::<Status>()
                .expect("No status"),
            &Status::NOT_FOUND,
        );
        open_file_validating(&dir_proxy, 0, MODE_TYPE_FILE, "bar/b")
            .await
            .expect("Open file failed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_rename_same_dir() {
        let registry = token_registry::Simple::new();
        let scope = ExecutionScope::build().token_registry(registry).new();
        let device = Arc::new(FakeDevice::new(2048, 512));
        let filesystem = FxFilesystem::new_empty(device.clone()).await.unwrap();
        let root_volume = root_volume(&filesystem).await.unwrap();
        let vol = FxVolumeAndRoot::new(root_volume.new_volume("vol").await.unwrap()).await;
        let (dir_proxy, dir_server_end) =
            fidl::endpoints::create_proxy::<DirectoryMarker>().expect("Create proxy to succeed");
        vol.root().clone().open(
            scope.clone(),
            OPEN_FLAG_DIRECTORY | OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            MODE_TYPE_DIRECTORY,
            Path::empty(),
            ServerEnd::new(dir_server_end.into_channel()),
        );

        let src = open_dir_validating(
            &dir_proxy,
            OPEN_FLAG_CREATE | OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            MODE_TYPE_DIRECTORY,
            "foo",
        )
        .await
        .expect("Create dir failed");

        open_file_validating(&dir_proxy, OPEN_FLAG_CREATE, MODE_TYPE_FILE, "foo/a")
            .await
            .expect("Create file failed");

        let (status, src_token) = src.get_token().await.expect("FIDL call failed");
        Status::ok(status).expect("get_token failed");
        Status::ok(src.rename("a", src_token.unwrap(), "b").await.expect("FIDL call failed"))
            .expect("rename failed");

        assert_eq!(
            open_file_validating(&dir_proxy, 0, MODE_TYPE_FILE, "foo/a")
                .await
                .expect_err("Open succeeded")
                .root_cause()
                .downcast_ref::<Status>()
                .expect("No status"),
            &Status::NOT_FOUND,
        );
        open_file_validating(&dir_proxy, 0, MODE_TYPE_FILE, "foo/b")
            .await
            .expect("Open file failed");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_rename_overwrites_file() {
        let registry = token_registry::Simple::new();
        let scope = ExecutionScope::build().token_registry(registry).new();
        let device = Arc::new(FakeDevice::new(2048, 512));
        let filesystem = FxFilesystem::new_empty(device.clone()).await.unwrap();
        let root_volume = root_volume(&filesystem).await.unwrap();
        let vol = FxVolumeAndRoot::new(root_volume.new_volume("vol").await.unwrap()).await;
        let (dir_proxy, dir_server_end) =
            fidl::endpoints::create_proxy::<DirectoryMarker>().expect("Create proxy to succeed");
        vol.root().clone().open(
            scope.clone(),
            OPEN_FLAG_DIRECTORY | OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            MODE_TYPE_DIRECTORY,
            Path::empty(),
            ServerEnd::new(dir_server_end.into_channel()),
        );

        let src = open_dir_validating(
            &dir_proxy,
            OPEN_FLAG_CREATE | OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            MODE_TYPE_DIRECTORY,
            "foo",
        )
        .await
        .expect("Create dir failed");

        let dst = open_dir_validating(
            &dir_proxy,
            OPEN_FLAG_CREATE | OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE | OPEN_FLAG_DIRECTORY,
            MODE_TYPE_DIRECTORY,
            "bar",
        )
        .await
        .expect("Create dir failed");

        // The src file is non-empty.
        let src_file = open_file_validating(
            &dir_proxy,
            OPEN_FLAG_CREATE | OPEN_RIGHT_WRITABLE,
            MODE_TYPE_FILE,
            "foo/a",
        )
        .await
        .expect("Create file failed");
        let buf = vec![0xaa as u8; 8192];
        write_file_bytes(&src_file, buf.as_slice()).await.expect("Failed to write to file");
        Status::ok(src_file.close().await.expect("FIDL call failed")).expect("close failed");

        // The dst file is empty (so we can distinguish it).
        open_file_validating(&dir_proxy, OPEN_FLAG_CREATE, MODE_TYPE_FILE, "bar/b")
            .await
            .expect("Create file failed");

        let (status, dst_token) = dst.get_token().await.expect("FIDL call failed");
        Status::ok(status).expect("get_token failed");
        Status::ok(src.rename("a", dst_token.unwrap(), "b").await.expect("FIDL call failed"))
            .expect("rename failed");

        assert_eq!(
            open_file_validating(&dir_proxy, 0, MODE_TYPE_FILE, "foo/a")
                .await
                .expect_err("Open succeeded")
                .root_cause()
                .downcast_ref::<Status>()
                .expect("No status"),
            &Status::NOT_FOUND,
        );
        let file = open_file_validating(&dir_proxy, OPEN_RIGHT_READABLE, MODE_TYPE_FILE, "bar/b")
            .await
            .expect("Open file failed");
        let buf = read_file_bytes(&file).await.expect("read file failed");
        assert_eq!(buf, vec![0xaa as u8; 8192]);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_rename_overwrites_dir() {
        let registry = token_registry::Simple::new();
        let scope = ExecutionScope::build().token_registry(registry).new();
        let device = Arc::new(FakeDevice::new(2048, 512));
        let filesystem = FxFilesystem::new_empty(device.clone()).await.unwrap();
        let root_volume = root_volume(&filesystem).await.unwrap();
        let vol = FxVolumeAndRoot::new(root_volume.new_volume("vol").await.unwrap()).await;
        let (dir_proxy, dir_server_end) =
            fidl::endpoints::create_proxy::<DirectoryMarker>().expect("Create proxy to succeed");
        vol.root().clone().open(
            scope.clone(),
            OPEN_FLAG_DIRECTORY | OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            MODE_TYPE_DIRECTORY,
            Path::empty(),
            ServerEnd::new(dir_server_end.into_channel()),
        );

        let src = open_dir_validating(
            &dir_proxy,
            OPEN_FLAG_CREATE | OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            MODE_TYPE_DIRECTORY,
            "foo",
        )
        .await
        .expect("Create dir failed");

        let dst = open_dir_validating(
            &dir_proxy,
            OPEN_FLAG_CREATE | OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE | OPEN_FLAG_DIRECTORY,
            MODE_TYPE_DIRECTORY,
            "bar",
        )
        .await
        .expect("Create dir failed");

        // The src dir is non-empty.
        open_dir_validating(
            &dir_proxy,
            OPEN_FLAG_CREATE | OPEN_RIGHT_WRITABLE,
            MODE_TYPE_DIRECTORY,
            "foo/a",
        )
        .await
        .expect("Create dir failed");
        open_file_validating(&dir_proxy, OPEN_FLAG_CREATE, MODE_TYPE_FILE, "foo/a/file")
            .await
            .expect("Create file failed");

        open_dir_validating(&dir_proxy, OPEN_FLAG_CREATE, MODE_TYPE_DIRECTORY, "bar/b")
            .await
            .expect("Create file failed");

        let (status, dst_token) = dst.get_token().await.expect("FIDL call failed");
        Status::ok(status).expect("get_token failed");
        Status::ok(src.rename("a", dst_token.unwrap(), "b").await.expect("FIDL call failed"))
            .expect("rename failed");

        assert_eq!(
            open_dir_validating(&dir_proxy, 0, MODE_TYPE_DIRECTORY, "foo/a")
                .await
                .expect_err("Open succeeded")
                .root_cause()
                .downcast_ref::<Status>()
                .expect("No status"),
            &Status::NOT_FOUND,
        );
        open_file_validating(&dir_proxy, 0, MODE_TYPE_FILE, "bar/b/file")
            .await
            .expect("Open file failed");
    }
}
