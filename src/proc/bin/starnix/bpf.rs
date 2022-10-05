// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implementation of (e)BPF.
//!
//! BPF stands for Berkeley Packet Filter and is an API introduced in BSD that allows filtering
//! network packets by running little programs in the kernel. eBPF stands for extended BFP and
//! is a Linux extension of BPF that allows hooking BPF programs into many different
//! non-networking-related contexts.

// TODO(https://github.com/rust-lang/rust/issues/39371): remove
#![allow(non_upper_case_globals)]

use std::collections::BTreeMap;
use std::ops::Bound;
use std::sync::Arc;
use zerocopy::{AsBytes, FromBytes};

use crate::auth::*;
use crate::fs::*;
use crate::lock::RwLock;
use crate::syscalls::*;
use crate::task::Kernel;
use crate::types::as_any::AsAny;
use crate::types::*;

trait BpfObject: Send + Sync + AsAny + 'static {}

/// A reference to a BPF object that can be stored in either an FD or an entry in the /sys/fs/bpf
/// filesystem.
#[derive(Clone)]
struct BpfHandle(Arc<dyn BpfObject>);

impl FileOps for BpfHandle {
    fileops_impl_nonseekable!();
    fileops_impl_nonblocking!();
    fn read(
        &self,
        _file: &FileObject,
        _current_task: &crate::task::CurrentTask,
        _data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        error!(EINVAL) // TODO
    }
    fn write(
        &self,
        _file: &FileObject,
        _current_task: &crate::task::CurrentTask,
        _data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        error!(EINVAL) // TODO
    }
}

impl BpfHandle {
    fn new(obj: impl BpfObject) -> Self {
        Self(Arc::new(obj))
    }

    fn downcast<T: BpfObject>(&self) -> Option<&T> {
        (*self.0).as_any().downcast_ref::<T>()
    }
}

/// A BPF Type Format object, often referred to as BTF. See
/// https://www.kernel.org/doc/html/latest/bpf/btf.html
#[allow(dead_code)]
struct BpfTypeFormat {
    data: Vec<u8>,
}
impl BpfObject for BpfTypeFormat {}

/// A BPF map. This is a hashtable that can be accessed both by BPF programs and userspace.
struct Map {
    map_type: bpf_map_type,
    key_size: u32,
    value_size: u32,
    max_entries: u32,
    flags: u32,

    // TODO(tbodt): Linux actually has 30 different implementations of a BPF map, from hashmap to
    // array to bloom filter. BTreeMap is probably the correct semantics for none of them. This
    // will ultimately need to be a trait object.
    entries: RwLock<BTreeMap<Vec<u8>, Vec<u8>>>,
}
impl BpfObject for Map {}

/// A BPF program. Currently empty because none of the state of a program actually matters to us
/// yet.
struct Program;
impl BpfObject for Program {}

/// Read the arguments for a BPF command. The ABI works like this: If the arguments struct
/// passed is larger than the kernel knows about, the excess must be zeros. Similarly, if the
/// arguments struct is smaller than the kernel knows about, the kernel fills the excess with
/// zero.
fn read_attr<Attr: FromBytes>(
    current_task: &CurrentTask,
    attr_addr: UserAddress,
    attr_size: u32,
) -> Result<Attr, Errno> {
    let attr_size = attr_size as usize;
    let sizeof_attr = std::mem::size_of::<Attr>();

    // Verify that the extra is all zeros.
    if attr_size > sizeof_attr {
        let mut tail = vec![0u8; attr_size - sizeof_attr];
        current_task.mm.read_memory(attr_addr + sizeof_attr, &mut tail)?;
        for byte in tail {
            if byte != 0 {
                return error!(E2BIG);
            }
        }
    }

    // If the struct passed is smaller than our definition of the struct, let whatever is not
    // passed be zero.
    let mut attr = Attr::new_zeroed();
    // SAFETY: attr is FromBytes, meaning it is safe to write any bit pattern to its storage. (The
    // unsafe slice construction is necessary because it's not necessarily safe to read from its
    // storage directly.)
    current_task.mm.read_memory(attr_addr, unsafe {
        std::slice::from_raw_parts_mut(&mut attr as *mut Attr as *mut u8, sizeof_attr)
    })?;

    Ok(attr)
}

fn install_bpf_fd(current_task: &CurrentTask, obj: impl BpfObject) -> Result<SyscallResult, Errno> {
    install_bpf_handle_fd(current_task, BpfHandle::new(obj))
}

fn install_bpf_handle_fd(
    current_task: &CurrentTask,
    handle: BpfHandle,
) -> Result<SyscallResult, Errno> {
    // All BPF FDs have the CLOEXEC flag turned on by default.
    let file = Anon::new_file(current_task, Box::new(handle), OpenFlags::CLOEXEC);
    Ok(current_task.files.add_with_flags(file, FdFlags::CLOEXEC)?.into())
}

fn get_bpf_fd(current_task: &CurrentTask, fd: u32) -> Result<BpfHandle, Errno> {
    Ok(current_task
        .files
        .get(FdNumber::from_raw(fd as i32))?
        .downcast_file::<BpfHandle>()
        .ok_or_else(|| errno!(EBADF))?
        .clone())
}

pub fn sys_bpf(
    current_task: &CurrentTask,
    cmd: bpf_cmd,
    attr_addr: UserAddress,
    attr_size: u32,
) -> Result<SyscallResult, Errno> {
    // TODO(security): Implement the actual security semantics of BPF. This is commented out
    // because Android calls bpf from unprivileged processes.
    // if !current_task.creds().has_capability(CAP_SYS_ADMIN) {
    //     return error!(EPERM);
    // }

    // The best available documentation on the various BPF commands is at
    // https://www.kernel.org/doc/html/latest/userspace-api/ebpf/syscall.html.
    // Comments on commands are copied from there.

    match cmd {
        // Create a map and return a file descriptor that refers to the map.
        bpf_cmd_BPF_MAP_CREATE => {
            let map_attr: bpf_attr__bindgen_ty_1 = read_attr(current_task, attr_addr, attr_size)?;
            strace!(current_task, "BPF_MAP_CREATE {:?}", map_attr);
            let mut map = Map {
                map_type: map_attr.map_type,
                key_size: map_attr.key_size,
                value_size: map_attr.value_size,
                max_entries: map_attr.max_entries,
                flags: map_attr.map_flags,
                entries: Default::default(),
            };

            // To quote
            // https://cs.android.com/android/platform/superproject/+/master:system/bpf/libbpf_android/Loader.cpp;l=670;drc=28e295395471b33e662b7116378d15f1e88f0864
            // "DEVMAPs are readonly from the bpf program side's point of view, as such the kernel
            // in kernel/bpf/devmap.c dev_map_init_map() will set the flag"
            if map.map_type == bpf_map_type_BPF_MAP_TYPE_DEVMAP
                || map.map_type == bpf_map_type_BPF_MAP_TYPE_DEVMAP_HASH
            {
                map.flags |= BPF_F_RDONLY_PROG;
            }

            install_bpf_fd(current_task, map)
        }

        // Create or update an element (key/value pair) in a specified map.
        bpf_cmd_BPF_MAP_UPDATE_ELEM => {
            let elem_attr: bpf_attr__bindgen_ty_2 = read_attr(current_task, attr_addr, attr_size)?;
            strace!(current_task, "BPF_MAP_UPDATE_ELEM");
            let map = get_bpf_fd(current_task, elem_attr.map_fd)?;
            let map = map.downcast::<Map>().ok_or_else(|| errno!(EINVAL))?;

            let mut key = vec![0u8; map.key_size as usize];
            current_task.mm.read_memory(UserAddress::from(elem_attr.key), &mut key)?;
            let mut value = vec![0u8; map.value_size as usize];
            // SAFETY: this union object was created with FromBytes so it's safe to access any
            // variant (right?)
            let value_addr = unsafe { elem_attr.__bindgen_anon_1.value };
            current_task.mm.read_memory(UserAddress::from(value_addr), &mut value)?;

            map.entries.write().insert(key, value);
            Ok(SUCCESS)
        }

        // Look up an element by key in a specified map and return the key of the next element. Can
        // be used to iterate over all elements in the map.
        bpf_cmd_BPF_MAP_GET_NEXT_KEY => {
            let elem_attr: bpf_attr__bindgen_ty_2 = read_attr(current_task, attr_addr, attr_size)?;
            strace!(current_task, "BPF_MAP_GET_NEXT_KEY");
            let map = get_bpf_fd(current_task, elem_attr.map_fd)?;
            let map = map.downcast::<Map>().ok_or_else(|| errno!(EINVAL))?;
            let key = if elem_attr.key != 0 {
                let mut key = vec![0u8; map.key_size as usize];
                current_task.mm.read_memory(UserAddress::from(elem_attr.key), &mut key)?;
                Some(key)
            } else {
                None
            };

            let entries = map.entries.read();
            let next_entry = match key {
                Some(key) if entries.contains_key(&key) => {
                    entries.range((Bound::Excluded(key), Bound::Unbounded)).next()
                }
                _ => entries.iter().next(),
            };
            let (next_key, _next_value) = next_entry.ok_or_else(|| errno!(ENOENT))?;
            // SAFETY: this union object was created with FromBytes so it's safe to access any
            // variant (right?)
            let next_key_addr = unsafe { elem_attr.__bindgen_anon_1.next_key };
            current_task.mm.write_memory(UserAddress::from(next_key_addr), next_key)?;
            Ok(SUCCESS)
        }

        // Verify and load an eBPF program, returning a new file descriptor associated with the
        // program.
        bpf_cmd_BPF_PROG_LOAD => {
            let _prog_attr: bpf_attr__bindgen_ty_4 = read_attr(current_task, attr_addr, attr_size)?;
            strace!(current_task, "BPF_PROG_LOAD");
            // Just pretend to load the program. We certainly can't execute it.
            install_bpf_fd(current_task, Program)
        }

        // Pin an eBPF program or map referred by the specified bpf_fd to the provided pathname on
        // the filesystem.
        bpf_cmd_BPF_OBJ_PIN => {
            let pin_attr: bpf_attr__bindgen_ty_5 = read_attr(current_task, attr_addr, attr_size)?;
            strace!(current_task, "BPF_OBJ_PIN {:?}", pin_attr);
            let object = get_bpf_fd(current_task, pin_attr.bpf_fd)?;
            let mut pathname = vec![0u8; PATH_MAX as usize];
            let path_addr = UserCString::new(UserAddress::from(pin_attr.pathname));
            let pathname = current_task.mm.read_c_string(path_addr, &mut pathname)?.to_owned();
            let (parent, basename) =
                current_task.lookup_parent_at(FdNumber::AT_FDCWD, &pathname)?;
            parent.entry.node.downcast_ops::<BpfFsDir>().ok_or_else(|| errno!(EINVAL))?;
            parent.entry.add_node_ops(basename, mode!(IFREG, 0o600), BpfFsObject(object))?;
            Ok(SUCCESS)
        }

        // Open a file descriptor for the eBPF object pinned to the specified pathname.
        bpf_cmd_BPF_OBJ_GET => {
            let path_attr: bpf_attr__bindgen_ty_5 = read_attr(current_task, attr_addr, attr_size)?;
            strace!(current_task, "BPF_OBJ_GET {:?}", path_attr);
            let mut pathname = vec![0u8; PATH_MAX as usize];
            let path_addr = UserCString::new(UserAddress::from(path_attr.pathname));
            let pathname = current_task.mm.read_c_string(path_addr, &mut pathname)?.to_owned();
            let node = current_task.lookup_path_from_root(&pathname)?;
            // TODO(tbodt): This might be the wrong error code, write a test program to find out
            let node =
                node.entry.node.downcast_ops::<BpfFsObject>().ok_or_else(|| errno!(EINVAL))?;
            install_bpf_handle_fd(current_task, node.0.clone())
        }

        // Obtain information about the eBPF object corresponding to bpf_fd.
        bpf_cmd_BPF_OBJ_GET_INFO_BY_FD => {
            let mut get_info_attr: bpf_attr__bindgen_ty_9 =
                read_attr(current_task, attr_addr, attr_size)?;
            strace!(current_task, "BPF_OBJ_GET_INFO_BY_FD {:?}", get_info_attr);
            let fd = get_bpf_fd(current_task, get_info_attr.bpf_fd)?;

            let mut info = if let Some(map) = fd.downcast::<Map>() {
                bpf_map_info {
                    type_: map.map_type,
                    id: 0, // not used by android as far as I can tell
                    key_size: map.key_size,
                    value_size: map.value_size,
                    max_entries: map.max_entries,
                    map_flags: map.flags,
                    ..Default::default()
                }
                .as_bytes()
                .to_owned()
            } else if let Some(_prog) = fd.downcast::<Program>() {
                bpf_prog_info {
                    // Doesn't matter yet
                    ..Default::default()
                }
                .as_bytes()
                .to_owned()
            } else {
                return error!(EINVAL);
            };

            // If info_len is larger than info, write out the full length of info and write the
            // smaller size into info_len. If info_len is smaller, truncate info.
            // TODO(tbodt): This is just a guess for the behavior. Works with BpfSyscallWrappers.h,
            // but could be wrong.
            info.truncate(get_info_attr.info_len as usize);
            get_info_attr.info_len = info.len() as u32;
            current_task.mm.write_memory(UserAddress::from(get_info_attr.info), &info)?;
            current_task.mm.write_memory(attr_addr, get_info_attr.as_bytes())?;
            Ok(SUCCESS)
        }

        // Verify and load BPF Type Format (BTF) metadata into the kernel, returning a new file
        // descriptor associated with the metadata. BTF is described in more detail at
        // https://www.kernel.org/doc/html/latest/bpf/btf.html.
        bpf_cmd_BPF_BTF_LOAD => {
            let btf_attr: bpf_attr__bindgen_ty_12 = read_attr(current_task, attr_addr, attr_size)?;
            strace!(current_task, "BPF_BTF_LOAD {:?}", btf_attr);
            let mut data = vec![0u8; btf_attr.btf_size as usize];
            current_task.mm.read_memory(UserAddress::from(btf_attr.btf), &mut data)?;
            install_bpf_fd(current_task, BpfTypeFormat { data })
        }

        _ => {
            not_implemented!(current_task, "bpf command {}", cmd);
            error!(EINVAL)
        }
    }
}

pub struct BpfFs;
impl BpfFs {
    pub fn new_fs(kernel: &Kernel) -> Result<FileSystemHandle, Errno> {
        let fs = FileSystem::new_with_permanent_entries(kernel, BpfFs);
        fs.set_root(BpfFsDir);
        Ok(fs)
    }
}

impl FileSystemOps for BpfFs {
    fn statfs(&self, _fs: &FileSystem) -> Result<statfs, Errno> {
        Ok(statfs::default(BPF_FS_MAGIC))
    }

    fn rename(
        &self,
        _fs: &FileSystem,
        _old_parent: &FsNodeHandle,
        _old_name: &FsStr,
        _new_parent: &FsNodeHandle,
        _new_name: &FsStr,
        _renamed: &FsNodeHandle,
        _replaced: Option<&FsNodeHandle>,
    ) -> Result<(), Errno> {
        Ok(())
    }
}

struct BpfFsDir;
impl FsNodeOps for BpfFsDir {
    fn create_file_ops(
        &self,
        _node: &FsNode,
        _flags: OpenFlags,
    ) -> Result<Box<dyn FileOps>, Errno> {
        Ok(Box::new(MemoryDirectoryFile::new()))
    }

    fn lookup(
        &self,
        _node: &FsNode,
        _current_task: &CurrentTask,
        _name: &FsStr,
    ) -> Result<FsNodeHandle, Errno> {
        error!(ENOENT)
    }

    fn mkdir(
        &self,
        node: &FsNode,
        _name: &FsStr,
        mode: FileMode,
        owner: FsCred,
    ) -> Result<FsNodeHandle, Errno> {
        Ok(node.fs().create_node(Box::new(BpfFsDir), mode, owner))
    }

    fn mknod(
        &self,
        _node: &FsNode,
        _name: &FsStr,
        _mode: FileMode,
        _dev: DeviceType,
        _owner: FsCred,
    ) -> Result<FsNodeHandle, Errno> {
        error!(EPERM)
    }

    fn create_symlink(
        &self,
        _node: &FsNode,
        _name: &FsStr,
        _target: &FsStr,
        _owner: FsCred,
    ) -> Result<FsNodeHandle, Errno> {
        error!(EPERM)
    }

    fn link(&self, _node: &FsNode, _name: &FsStr, _child: &FsNodeHandle) -> Result<(), Errno> {
        Ok(())
    }

    fn unlink(&self, _node: &FsNode, _name: &FsStr, _child: &FsNodeHandle) -> Result<(), Errno> {
        Ok(())
    }
}

struct BpfFsObject(BpfHandle);
impl FsNodeOps for BpfFsObject {
    fn create_file_ops(
        &self,
        _node: &FsNode,
        _flags: OpenFlags,
    ) -> Result<Box<dyn FileOps>, Errno> {
        error!(EIO)
    }
}
