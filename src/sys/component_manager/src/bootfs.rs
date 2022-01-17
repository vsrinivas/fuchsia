// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context, Error},
    fidl::{
        endpoints::{create_proxy, Proxy, ServerEnd},
        AsHandleRef,
    },
    fidl_fuchsia_io::{
        DirectoryMarker, NodeMarker, MODE_TYPE_DIRECTORY, OPEN_RIGHT_EXECUTABLE,
        OPEN_RIGHT_READABLE,
    },
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

// Bootfs shouldn't take ownership of the true default VDSO which is used downstream
// during process creation. Temporarily bootsvc is duplicating the default VDSO and
// shifting other entries down by one, and once bootsvc is gone userboot will
// do the same.
// TODO(fxb/91230): Move duplicating the vdso handle from bootsvc to userboot.
const DEFAULT_VDSO_INDEX: u16 = 0;

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
const BOOTFS_EXECUTABLE_DIRECTORIES: &[&str] = &["bin", "driver", "lib", "test"];

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

        BootfsSvc::create_dir_entry(child, size, is_exec, inode)
    }

    fn create_dir_entry(
        vmo: zx::Vmo,
        size: u64,
        is_exec: bool,
        inode: u64,
    ) -> Result<Arc<dyn DirectoryEntry>, Error> {
        let init_vmo = move || {
            // This lambda is not FnOnce, so the handle must be duplicated before use so that this
            // can be invoked multiple times.
            let vmo_dup =
                vmo.duplicate_handle(zx::Rights::SAME_RIGHTS).expect("Failed to duplicate VMO.");
            async move { Ok(vmo::NewVmo { vmo: vmo_dup, size, capacity: size }) }
        };

        Ok(vmo::create_immutable_vmo_file(init_vmo, true, is_exec, inode))
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

    pub fn ingest_bootfs_vmo(mut self, system: &Option<Resource>) -> Result<Self, Error> {
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

    pub fn publish_kernel_vmo(mut self, handle_type: HandleType) -> Result<Self, Error> {
        println!("[BootfsSvc] Adding kernel VMOs of type {:?}.", handle_type);
        let mut index = if handle_type == HandleType::VdsoVmo { DEFAULT_VDSO_INDEX + 1 } else { 0 };
        loop {
            let vmo: zx::Vmo = match take_startup_handle(HandleInfo::new(handle_type, index)) {
                Some(vmo) => {
                    index += 1;
                    vmo.into()
                }
                None => break,
            };

            let name = vmo.get_name()?.into_string()?;
            if name.is_empty() {
                // Skip VMOs without names.
                continue;
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
                continue;
            }

            // If content size is set (non-zero), it's the exact size of the file backed
            // by the VMO, and is the file size the VFS should report.
            let content_size = vmo.get_content_size()?;
            let size = if content_size != 0 { content_size } else { vmo_size };

            let info = vmo.basic_info()?;
            let is_exec = info.rights.contains(zx::Rights::EXECUTE);

            match BootfsSvc::create_dir_entry(
                vmo,
                size,
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
        }

        Ok(self)
    }

    pub fn create_and_bind_vfs(&mut self) -> Result<(), Error> {
        println!("[BootfsSvc] Finalizing rust bootfs service.");

        let tree_builder = replace(&mut self.tree_builder, TreeBuilder::empty_dir());

        let mut get_inode = |_| -> u64 { BootfsSvc::get_next_inode(&mut self.next_inode) };

        let vfs = tree_builder.build_with_inode_generator(&mut get_inode);
        let (directory_proxy, directory_server_end) = create_proxy::<DirectoryMarker>()?;
        vfs.open(
            ExecutionScope::new(),
            OPEN_RIGHT_READABLE | OPEN_RIGHT_EXECUTABLE,
            MODE_TYPE_DIRECTORY,
            vfs::path::Path::dot(),
            ServerEnd::<NodeMarker>::new(directory_server_end.into_channel()),
        );

        let ns = fdio::Namespace::installed()?;
        assert!(
            ns.unbind("/boot").is_err(),
            "No filesystem should already be bound to /boot when BootfsSvc is starting."
        );

        if let Ok(channel) = directory_proxy.into_channel() {
            ns.bind("/boot", channel.into_zx_channel())?;
        } else {
            return Err(anyhow!("Can't convert bootfs proxy into channel."));
        }

        println!("[BootfsSvc] Bootfs is ready and is now serving /boot.");

        Ok(())
    }
}
