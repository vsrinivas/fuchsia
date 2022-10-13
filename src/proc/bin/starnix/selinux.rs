// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::fs::*;
use crate::lock::{Mutex, RwLock};
use crate::logging::not_implemented;
use crate::task::*;
use crate::types::as_any::AsAny;
use crate::types::*;
use std::collections::BTreeMap;
use std::sync::Arc;
use zerocopy::AsBytes;

/// The version of selinux_status_t this kernel implements.
const SELINUX_STATUS_VERSION: u32 = 1;

const SELINUX_PERMS: &[&[u8]] = &[b"add", b"find", b"set"];

struct SeLinuxFs;
impl FileSystemOps for SeLinuxFs {
    fn statfs(&self, _fs: &FileSystem) -> Result<statfs, Errno> {
        Ok(statfs::default(SELINUX_MAGIC))
    }
}

impl SeLinuxFs {
    fn new_fs(kernel: &Kernel) -> Result<FileSystemHandle, Errno> {
        let fs = FileSystem::new_with_permanent_entries(kernel, SeLinuxFs);
        StaticDirectoryBuilder::new(&fs)
            .entry(b"load", SeLinuxNode::new(|| Ok(SeLoad)), mode!(IFREG, 0o600))
            .entry(b"enforce", SeLinuxNode::new(|| Ok(SeEnforce)), mode!(IFREG, 0o644))
            .entry(b"checkreqprot", SeLinuxNode::new(|| Ok(SeCheckReqProt)), mode!(IFREG, 0o644))
            .entry(b"access", AccessFileNode::new(), mode!(IFREG, 0o666))
            .entry(
                b"deny_unknown",
                // Allow all unknown object classes/permissions.
                ByteVecFile::new_node(b"0:0\n".to_vec()),
                mode!(IFREG, 0o444),
            )
            .entry(
                b"status",
                // The status file needs to be mmap-able, so use a VMO-backed file.
                // When the selinux state changes in the future, the way to update this data (and
                // communicate updates with userspace) is to use the
                // ["seqlock"](https://en.wikipedia.org/wiki/Seqlock) technique.
                VmoFileNode::from_bytes(
                    selinux_status_t { version: SELINUX_STATUS_VERSION, ..Default::default() }
                        .as_bytes(),
                )?,
                mode!(IFREG, 0o444),
            )
            .entry(b"class", SeLinuxClassDirectory::new(), mode!(IFDIR, 0o777))
            .entry(b"context", SeLinuxNode::new(|| Ok(SeContext)), mode!(IFREG, 0o644))
            .entry_dev(b"null", DeviceFileNode {}, mode!(IFCHR, 0o666), DeviceType::NULL)
            .build_root();

        Ok(fs)
    }
}

pub struct SeLinuxNode<F, O>
where
    F: Fn() -> Result<O, Errno>,
    O: FileOps,
{
    create_file_ops: F,
}
impl<F, O> SeLinuxNode<F, O>
where
    F: Fn() -> Result<O, Errno> + Send + Sync,
    O: FileOps,
{
    pub fn new(create_file_ops: F) -> SeLinuxNode<F, O> {
        Self { create_file_ops }
    }
}
impl<F, O> FsNodeOps for SeLinuxNode<F, O>
where
    F: Fn() -> Result<O, Errno> + Send + Sync + 'static,
    O: FileOps,
{
    fn create_file_ops(
        &self,
        _node: &FsNode,
        _flags: OpenFlags,
    ) -> Result<Box<dyn FileOps>, Errno> {
        Ok(Box::new((self.create_file_ops)()?))
    }

    fn truncate(&self, _node: &FsNode, _length: u64) -> Result<(), Errno> {
        // TODO(tbodt): Is this right? This is the minimum to handle O_TRUNC
        Ok(())
    }
}

trait SeLinuxFile {
    fn write(&self, current_task: &CurrentTask, data: Vec<u8>) -> Result<(), Errno>;
    fn read(&self, _current_task: &CurrentTask) -> Result<Vec<u8>, Errno> {
        error!(ENOSYS)
    }
}

impl<T: SeLinuxFile + Send + Sync + AsAny + 'static> FileOps for T {
    fileops_impl_seekable!();
    fileops_impl_nonblocking!();

    fn write_at(
        &self,
        _file: &FileObject,
        current_task: &CurrentTask,
        offset: usize,
        data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        if offset != 0 {
            return error!(EINVAL);
        }
        let size = UserBuffer::get_total_length(data)?;
        let mut buf = vec![0u8; size];
        current_task.mm.read_all(data, &mut buf)?;
        self.write(current_task, buf)?;
        Ok(size)
    }

    fn read_at(
        &self,
        _file: &FileObject,
        current_task: &CurrentTask,
        _offset: usize,
        buffer: &[UserBuffer],
    ) -> Result<usize, Errno> {
        let data = self.read(current_task)?;
        current_task.mm.write_all(buffer, &data)
    }
}

/// The C-style struct exposed to userspace by the /sys/fs/selinux/status file.
/// Defined here (instead of imported through bindgen) as selinux headers are not exposed through
/// kernel uapi headers.
#[derive(Debug, Copy, Clone, AsBytes, Default)]
#[repr(C, packed)]
struct selinux_status_t {
    /// Version number of this structure (1).
    version: u32,
    /// Sequence number. See [seqlock](https://en.wikipedia.org/wiki/Seqlock).
    sequence: u32,
    /// `0` means permissive mode, `1` means enforcing mode.
    enforcing: u32,
    /// The number of times the selinux policy has been reloaded.
    policyload: u32,
    /// `0` means allow and `1` means deny unknown object classes/permissions.
    deny_unknown: u32,
}

struct SeLoad;
impl SeLinuxFile for SeLoad {
    fn write(&self, current_task: &CurrentTask, data: Vec<u8>) -> Result<(), Errno> {
        not_implemented!(current_task, "got selinux policy, length {}, ignoring", data.len());
        Ok(())
    }
}

struct SeEnforce;
impl SeLinuxFile for SeEnforce {
    fn write(&self, current_task: &CurrentTask, data: Vec<u8>) -> Result<(), Errno> {
        let enforce = parse_int(&data)?;
        not_implemented!(current_task, "selinux setenforce: {}", enforce);
        Ok(())
    }

    fn read(&self, _current_task: &CurrentTask) -> Result<Vec<u8>, Errno> {
        Ok(b"0\n".to_vec())
    }
}

struct SeCheckReqProt;
impl SeLinuxFile for SeCheckReqProt {
    fn write(&self, current_task: &CurrentTask, data: Vec<u8>) -> Result<(), Errno> {
        let checkreqprot = parse_int(&data)?;
        not_implemented!(current_task, "selinux checkreqprot: {}", checkreqprot);
        Ok(())
    }
}

struct SeContext;
impl SeLinuxFile for SeContext {
    fn write(&self, current_task: &CurrentTask, data: Vec<u8>) -> Result<(), Errno> {
        not_implemented!(
            current_task,
            "selinux validate context: {}",
            String::from_utf8_lossy(&data)
        );
        Ok(())
    }
}

struct AccessFile {
    seqno: u64,
}

impl FileOps for AccessFile {
    fileops_impl_nonseekable!();
    fileops_impl_nonblocking!();

    fn read(
        &self,
        _file: &FileObject,
        current_task: &CurrentTask,
        data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        // Format is allowed decided autitallow auditdeny seqno flags
        // Everything but seqno must be in hexadecimal format and represents a bits field.
        let content = format!("ffffffff ffffffff 0 ffffffff {} 0\n", self.seqno);
        let bytes = content.as_bytes();
        current_task.mm.write_all(data, bytes)
    }

    fn write(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        UserBuffer::get_total_length(data)
    }
}

struct DeviceFileNode;

impl FsNodeOps for DeviceFileNode {
    fn create_file_ops(
        &self,
        _node: &FsNode,
        _flags: OpenFlags,
    ) -> Result<Box<dyn FileOps>, Errno> {
        unreachable!("Special nodes cannot be opened.");
    }
}

struct AccessFileNode {
    seqno: Mutex<u64>,
}

impl AccessFileNode {
    fn new() -> Self {
        Self { seqno: Mutex::new(0) }
    }
}

impl FsNodeOps for AccessFileNode {
    fn create_file_ops(
        &self,
        _node: &FsNode,
        _flags: OpenFlags,
    ) -> Result<Box<dyn FileOps>, Errno> {
        let seqno = {
            let mut writer = self.seqno.lock();
            *writer += 1;
            *writer
        };
        Ok(Box::new(AccessFile { seqno }))
    }
}

struct SeLinuxClassDirectory {
    entries: RwLock<BTreeMap<FsString, FsNodeHandle>>,
}

impl SeLinuxClassDirectory {
    fn new() -> Arc<Self> {
        Arc::new(Self { entries: RwLock::new(BTreeMap::new()) })
    }
}

impl FsNodeOps for Arc<SeLinuxClassDirectory> {
    fn create_file_ops(
        &self,
        _node: &FsNode,
        _flags: OpenFlags,
    ) -> Result<Box<dyn FileOps>, Errno> {
        Ok(VecDirectory::new_file(
            self.entries
                .read()
                .iter()
                .map(|(name, node)| VecDirectoryEntry {
                    entry_type: DirectoryEntryType::DIR,
                    name: name.clone(),
                    inode: Some(node.inode_num),
                })
                .collect(),
        ))
    }

    fn lookup(
        &self,
        node: &FsNode,
        _current_task: &CurrentTask,
        name: &FsStr,
    ) -> Result<FsNodeHandle, Errno> {
        let mut entries = self.entries.write();
        let next_index = entries.len() + 1;
        Ok(entries
            .entry(name.to_vec())
            .or_insert_with(|| {
                let index = format!("{}\n", next_index).into_bytes();
                StaticDirectoryBuilder::new(&node.fs())
                    .entry(b"index", ByteVecFile::new_node(index), mode!(IFREG, 0o444))
                    .subdir(b"perms", 0o555, |mut perms| {
                        for (i, perm) in SELINUX_PERMS.iter().enumerate() {
                            let node =
                                ByteVecFile::new_node(format!("{}\n", i + 1).as_bytes().to_vec());
                            perms = perms.entry(perm, node, mode!(IFREG, 0o444));
                        }
                        perms
                    })
                    .set_mode(mode!(IFDIR, 0o555))
                    .build()
            })
            .clone())
    }
}

fn parse_int(buf: &[u8]) -> Result<u32, Errno> {
    let i = buf.iter().position(|c| !char::from(*c).is_ascii_digit()).unwrap_or(buf.len());
    std::str::from_utf8(&buf[..i]).unwrap().parse::<u32>().map_err(|_| errno!(EINVAL))
}

pub fn selinux_fs(kern: &Kernel) -> &FileSystemHandle {
    kern.selinux_fs.get_or_init(|| SeLinuxFs::new_fs(kern).expect("failed to construct selinuxfs"))
}
