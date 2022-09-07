// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context, Error},
    fidl::AsHandleRef as _,
    fidl_fuchsia_io as fio,
    fuchsia_bootfs::BootfsParser,
    fuchsia_runtime::{take_startup_handle, HandleInfo, HandleType},
    fuchsia_zircon::{self as zx, HandleBased, Resource},
    std::convert::TryFrom,
    std::mem::replace,
    std::sync::Arc,
    vfs::{
        directory::entry::DirectoryEntry, execution_scope::ExecutionScope,
        file::vmo::asynchronous as vmo, tree_builder::TreeBuilder,
    },
};

// Used to create executable VMOs.
const BOOTFS_VMEX_NAME: &str = "bootfs_vmex";

// If passed as a kernel handle, this gets relocated into a '/boot/log' directory.
const KERNEL_CRASHLOG_NAME: &str = "crashlog";
const LAST_PANIC_FILEPATH: &str = "log/last-panic.txt";

// Kernel startup VMOs are published beneath '/boot/kernel'. The VFS is relative
// to '/boot', so we only need to prepend paths under that.
const KERNEL_VMO_SUBDIRECTORY: &str = "kernel/";

// Bootfs will sequentially number files and directories starting with this value.
// This is a self contained immutable filesystem, so we only need to ensure that
// there are no internal collisions.
const FIRST_INODE_VALUE: u64 = 1;

// Packages in bootfs can contain both executable and read-only files. For example,
// 'pkg/my_package/bin' should be executable but 'pkg/my_package/foo' should not.
const BOOTFS_PACKAGE_PREFIX: &str = "pkg";
const BOOTFS_EXECUTABLE_PACKAGE_DIRECTORIES: &[&str] = &["bin", "lib"];

// Top level directories in bootfs that are allowed to contain executable files.
// Every file in these directories will have ZX_RIGHT_EXECUTE.
const BOOTFS_EXECUTABLE_DIRECTORIES: &[&str] = &["bin", "driver", "lib", "test", "blob"];

pub struct BootfsSvc {
    next_inode: u64,
    parser: BootfsParser,
    bootfs: zx::Vmo,
    tree_builder: TreeBuilder,
}

impl BootfsSvc {
    pub fn new() -> Result<Self, Error> {
        let bootfs_handle = take_startup_handle(HandleType::BootfsVmo.into());
        let bootfs = Into::<zx::Vmo>::into(bootfs_handle.ok_or_else(|| {
            anyhow!("Ingesting a bootfs image requires a valid bootfs vmo handle.")
        })?);

        Self::new_internal(bootfs)
    }

    // BootfsSvc can be used in hermetic integration tests by providing
    // an arbitrary vmo containing a valid bootfs image.
    pub fn new_for_test(bootfs: zx::Vmo) -> Result<Self, Error> {
        Self::new_internal(bootfs)
    }

    fn new_internal(bootfs: zx::Vmo) -> Result<Self, Error> {
        let bootfs_dup = bootfs.duplicate_handle(zx::Rights::SAME_RIGHTS)?.into();
        let parser = BootfsParser::create_from_vmo(bootfs_dup)?;

        Ok(Self {
            next_inode: FIRST_INODE_VALUE,
            parser,
            bootfs,
            tree_builder: TreeBuilder::empty_dir(),
        })
    }

    fn get_next_inode(inode: &mut u64) -> u64 {
        let next_inode = *inode;
        *inode += 1;
        next_inode
    }

    fn file_in_executable_directory(path: &Vec<&str>) -> bool {
        // If the first token is 'pkg', the second token can be anything, with the third
        // token needing to be within the list of allowed executable package directories.
        if path.len() > 2 && path[0] == BOOTFS_PACKAGE_PREFIX {
            for dir in BOOTFS_EXECUTABLE_PACKAGE_DIRECTORIES.iter() {
                if path[2] == *dir {
                    return true;
                }
            }
        }

        // If the first token is an allowed executable directory, everything beneath it
        // can be marked executable.
        if path.len() > 1 {
            for dir in BOOTFS_EXECUTABLE_DIRECTORIES.iter() {
                if path[0] == *dir {
                    return true;
                }
            }
        }

        false
    }

    fn create_dir_entry_with_child(
        parent: &zx::Vmo,
        offset: u64,
        size: u64,
        is_exec: bool,
        inode: u64,
    ) -> Result<Arc<dyn DirectoryEntry>, Error> {
        // If this is a VMO with execution rights, passing zx::VmoChildOptions::NO_WRITE will
        // allow the child to also inherit execution rights. Without that flag execution
        // rights are stripped, even if the VMO already lacked write permission.
        let child = parent
            .create_child(
                zx::VmoChildOptions::SNAPSHOT_AT_LEAST_ON_WRITE | zx::VmoChildOptions::NO_WRITE,
                offset,
                size,
            )
            .with_context(|| {
                format!(
                    "Failed to create child VMO region of size {} with offset {}.",
                    size, offset
                )
            })?;

        BootfsSvc::create_dir_entry(child, is_exec, inode)
    }

    fn create_dir_entry(
        vmo: zx::Vmo,
        is_exec: bool,
        inode: u64,
    ) -> Result<Arc<dyn DirectoryEntry>, Error> {
        let init_vmo = move || {
            // This lambda is not FnOnce, so the handle must be duplicated before use so that this
            // can be invoked multiple times.
            let vmo_dup =
                vmo.duplicate_handle(zx::Rights::SAME_RIGHTS).expect("Failed to duplicate VMO.");
            async move { Ok(vmo_dup) }
        };
        Ok(vmo::VmoFile::new_with_inode(init_vmo, true, false, is_exec, inode))
    }

    /// Read configs from the parsed bootfs image before the filesystem has been fully initialized.
    /// This is required for configs needed to run the VFS, such as the component manager config
    /// which specifies the number of threads for the executor which the VFS needs to run within.
    /// Path should be relative to '/boot' without a leading forward slash.
    pub fn read_config_from_uninitialized_vfs(&self, config_path: &str) -> Result<Vec<u8>, Error> {
        for entry in self.parser.zero_copy_iter() {
            match entry {
                Ok(entry) => {
                    assert!(entry.payload.is_none()); // Using the zero copy iterator.
                    if entry.name == config_path {
                        let mut buffer = vec![0; usize::try_from(entry.size)?];
                        if let Err(error) = self.bootfs.read(&mut buffer, entry.offset) {
                            return Err(anyhow!(
                                "[BootfsSvc] Found file but failed to load it into a buffer: {}.",
                                error
                            ));
                        } else {
                            return Ok(buffer);
                        }
                    }
                }
                Err(error) => {
                    println!("[BootfsSvc] Bootfs parsing error: {}", error);
                }
            }
        }

        Err(anyhow!("Unable to find config: {}.", config_path))
    }

    pub fn ingest_bootfs_vmo(self, system: &Option<Resource>) -> Result<Self, Error> {
        let system = system.as_ref().ok_or(anyhow!(
            "Bootfs requires a valid system resource handle so that it can make \
            VMO regions executable."
        ))?;

        let vmex = system.create_child(
            zx::ResourceKind::SYSTEM,
            None,
            zx::sys::ZX_RSRC_SYSTEM_VMEX_BASE,
            1,
            BOOTFS_VMEX_NAME.as_bytes(),
        )?;

        // The bootfs VFS is comprised of multiple child VMOs which are just offsets into a
        // single backing parent VMO.
        //
        // The parent VMO is duplicated here and marked as executable to reduce the total
        // number of syscalls required. Files in directories that are read-only will just
        // be children of the original read-only VMO, and files in directories that are
        // read-execution will be children of the duplicated read-execution VMO.
        let bootfs_exec: zx::Vmo = self.bootfs.duplicate_handle(zx::Rights::SAME_RIGHTS)?.into();
        let bootfs_exec = bootfs_exec.replace_as_executable(&vmex)?;

        self.ingest_bootfs_vmo_internal(bootfs_exec)
    }

    // Ingesting the bootfs vmo with this API will produce a /boot VFS that supports all
    // functionality except execution of contents; the permission to convert arbitrary vmos
    // to executable requires the root System resource, which is not available to fuchsia
    // test components.
    pub fn ingest_bootfs_vmo_for_test(self) -> Result<Self, Error> {
        let fake_exec: zx::Vmo = self.bootfs.duplicate_handle(zx::Rights::SAME_RIGHTS)?.into();
        self.ingest_bootfs_vmo_internal(fake_exec)
    }

    pub fn ingest_bootfs_vmo_internal(mut self, bootfs_exec: zx::Vmo) -> Result<Self, Error> {
        for entry in self.parser.zero_copy_iter() {
            match entry {
                Ok(entry) => {
                    assert!(entry.payload.is_none()); // Using the zero copy iterator.

                    let name = entry.name;
                    let path_parts: Vec<&str> =
                        name.split("/").filter(|&x| !x.is_empty()).collect();

                    let is_exec = BootfsSvc::file_in_executable_directory(&path_parts);
                    let vmo = if is_exec { &bootfs_exec } else { &self.bootfs };
                    match BootfsSvc::create_dir_entry_with_child(
                        vmo,
                        entry.offset,
                        entry.size,
                        is_exec,
                        BootfsSvc::get_next_inode(&mut self.next_inode),
                    ) {
                        Ok(dir_entry) => {
                            self.tree_builder.add_entry(&path_parts, dir_entry).unwrap_or_else(
                                |error| {
                                    println!(
                                        "[BootfsSvc] Failed to add bootfs entry {} \
                                        to directory: {}.",
                                        name, error
                                    );
                                },
                            );
                        }
                        Err(error) => {
                            return Err(anyhow!(
                                "Unable to create VMO for binary {}: {}",
                                name,
                                error
                            ));
                        }
                    }
                }
                Err(error) => {
                    println!("[BootfsSvc] Bootfs parsing error: {}", error);
                }
            }
        }

        Ok(self)
    }

    // Publish a VMO beneath '/boot/kernel'. Used to publish VDSOs and kernel files.
    pub fn publish_kernel_vmo(mut self, vmo: zx::Vmo) -> Result<Self, Error> {
        let name = vmo.get_name()?.into_string()?;
        if name.is_empty() {
            // Skip VMOs without names.
            return Ok(self);
        }

        let path = format!("{}{}", KERNEL_VMO_SUBDIRECTORY, name);
        let mut path_parts: Vec<&str> = path.split("/").filter(|&x| !x.is_empty()).collect();

        // There is special handling for the crashlog.
        if path_parts.len() > 1 && path_parts[path_parts.len() - 1] == KERNEL_CRASHLOG_NAME {
            path_parts = LAST_PANIC_FILEPATH.split("/").filter(|&x| !x.is_empty()).collect();
        }

        let vmo_size = vmo.get_size()?;
        if vmo_size == 0 {
            // Skip empty VMOs.
            return Ok(self);
        }

        // If content size is zero, set it to the size of the VMO.
        if vmo.get_content_size()? == 0 {
            vmo.set_content_size(&vmo_size)?;
        }

        let info = vmo.basic_info()?;
        let is_exec = info.rights.contains(zx::Rights::EXECUTE);

        match BootfsSvc::create_dir_entry(
            vmo,
            is_exec,
            BootfsSvc::get_next_inode(&mut self.next_inode),
        ) {
            Ok(dir_entry) => {
                self.tree_builder.add_entry(&path_parts, dir_entry).unwrap_or_else(|error| {
                    println!(
                        "[BootfsSvc] Failed to publish kernel VMO {} to directory: {}.",
                        path, error
                    );
                });
            }
            Err(error) => {
                return Err(anyhow!("Unable to create VMO for binary {}: {}", path, error));
            }
        }

        Ok(self)
    }

    /// Publish all VMOs of a given type provided to this process through its processargs
    /// bootstrap message. An initial index can be provided to skip handles that were already
    /// taken.
    pub fn publish_kernel_vmos(
        mut self,
        handle_type: HandleType,
        first_index: u16,
    ) -> Result<Self, Error> {
        println!(
            "[BootfsSvc] Adding kernel VMOs of type {:?} starting at index {}.",
            handle_type, first_index
        );
        // The first handle may not be at index 0 if we have already taken it previously.
        let mut index = first_index;
        loop {
            let vmo = take_startup_handle(HandleInfo::new(handle_type, index)).map(zx::Vmo::from);
            match vmo {
                Some(vmo) => {
                    index += 1;
                    self = self.publish_kernel_vmo(vmo)?;
                }
                None => break,
            }
        }

        Ok(self)
    }

    pub fn create_and_bind_vfs(&mut self) -> Result<(), Error> {
        println!("[BootfsSvc] Finalizing rust bootfs service.");

        let tree_builder = replace(&mut self.tree_builder, TreeBuilder::empty_dir());

        let mut get_inode = |_| -> u64 { BootfsSvc::get_next_inode(&mut self.next_inode) };

        let vfs = tree_builder.build_with_inode_generator(&mut get_inode);
        let (directory, directory_server_end) = fidl::endpoints::create_endpoints()?;
        vfs.open(
            ExecutionScope::new(),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_EXECUTABLE,
            fio::MODE_TYPE_DIRECTORY,
            vfs::path::Path::dot(),
            fidl::endpoints::ServerEnd::<fio::NodeMarker>::new(directory_server_end.into_channel()),
        );

        let ns = fdio::Namespace::installed()?;
        assert!(
            ns.unbind("/boot").is_err(),
            "No filesystem should already be bound to /boot when BootfsSvc is starting."
        );

        ns.bind("/boot", directory)?;

        println!("[BootfsSvc] Bootfs is ready and is now serving /boot.");

        Ok(())
    }
}
