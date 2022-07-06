// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::fs::*;
use crate::logging::not_implemented;
use crate::task::*;
use crate::types::as_any::AsAny;
use crate::types::*;
use std::sync::Arc;
use zerocopy::AsBytes;

/// The version of selinux_status_t this kernel implements.
const SELINUX_STATUS_VERSION: u32 = 1;

struct SeLinuxFs;
impl FileSystemOps for SeLinuxFs {
    fn statfs(&self, _fs: &FileSystem) -> Result<statfs, Errno> {
        Ok(statfs { f_type: SELINUX_MAGIC as i64, ..Default::default() })
    }
}

impl SeLinuxFs {
    fn new() -> Result<FileSystemHandle, Errno> {
        let fs = FileSystem::new_with_permanent_entries(SeLinuxFs);
        StaticDirectoryBuilder::new(&fs)
            .add_entry(b"load", SeLinuxNode::new(|| Ok(SeLoad)), mode!(IFREG, 0o600))
            .add_entry(b"enforce", SeLinuxNode::new(|| Ok(SeEnforce)), mode!(IFREG, 0o644))
            .add_entry(
                b"checkreqprot",
                SeLinuxNode::new(|| Ok(SeCheckReqProt)),
                mode!(IFREG, 0o644),
            )
            .add_entry(
                b"deny_unknown",
                // Allow all unknown object classes/permissions.
                ByteVecFile::new(b"0:0\n".to_vec()),
                mode!(IFREG, 0o444),
            )
            .add_entry(
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
            .add_node_entry(b"class", dynamic_directory(&fs, SeLinuxClassDirectoryDelegate))
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
    O: FileOps + 'static,
{
    pub fn new(create_file_ops: F) -> SeLinuxNode<F, O> {
        Self { create_file_ops }
    }
}
impl<F, O> FsNodeOps for SeLinuxNode<F, O>
where
    F: Fn() -> Result<O, Errno> + Send + Sync + 'static,
    O: FileOps + 'static,
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
        current_task.mm.read_all(&data, &mut buf)?;
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

struct SeLinuxClassDirectoryDelegate;

impl DirectoryDelegate for SeLinuxClassDirectoryDelegate {
    fn list(&self, _fs: &Arc<FileSystem>) -> Result<Vec<DynamicDirectoryEntry>, Errno> {
        Ok(vec![])
    }

    fn lookup(&self, fs: &Arc<FileSystem>, _name: &FsStr) -> Result<Arc<FsNode>, Errno> {
        Ok(StaticDirectoryBuilder::new(&fs)
            .add_entry(b"index", ByteVecFile::new(b"0\n".to_vec()), mode!(IFREG, 0o444))
            .add_node_entry(
                b"perms",
                StaticDirectoryBuilder::new(&fs).set_mode(mode!(IFDIR, 0o555)).build(),
            )
            .build())
    }
}

fn parse_int(buf: &[u8]) -> Result<u32, Errno> {
    let i = buf.iter().position(|c| !char::from(*c).is_digit(10)).unwrap_or(buf.len());
    std::str::from_utf8(&buf[..i]).unwrap().parse::<u32>().map_err(|_| errno!(EINVAL))
}

pub fn selinux_fs(kern: &Kernel) -> &FileSystemHandle {
    kern.selinux_fs.get_or_init(|| SeLinuxFs::new().expect("failed to construct selinuxfs"))
}
