// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::file_target::FileBlockingTarget,
    crate::operations::Target,
    crate::operations::TargetOps,
    crate::target::AvailableTargets,
    libc::c_void,
    log::debug,
    std::{
        io::{Error, ErrorKind, Result},
        ops::Range,
        os::unix::io::RawFd,
        sync::Arc,
        time::Instant,
    },
};

pub fn pwrite(raw_fd: RawFd, buffer: &mut Vec<u8>, offset: i64) -> Result<()> {
    let ret =
        unsafe { libc::pwrite(raw_fd, buffer.as_ptr() as *const c_void, buffer.len(), offset) };
    debug!("safe_pwrite: {:?} {}", offset, ret);
    if ret < 0 {
        return Err(Error::last_os_error());
    } else if ret < buffer.len() as isize {
        // TODO(auradkar): Define a set of error codes to be used throughout the app.
        return Err(Error::new(ErrorKind::Other, "pwrite wrote less bytes than requested!"));
    }
    Ok(())
}

/// Based on the input args, create_target searches available Targets and
/// creates an appropriate Target trait.
pub fn create_target(
    target_type: AvailableTargets,
    id: u64,
    name: String,
    offset: Range<u64>,
    start: Instant,
) -> Arc<Box<Target + Send + Sync>> {
    match target_type {
        AvailableTargets::FileTarget => FileBlockingTarget::new(name, id, offset, start),
    }
}

/// Returned allowed TargetOps for giver Target `target_type`.
/// Allowed are the operations for which generator can generate io packets.
/// There maybe operations *supported* by the target for which generator
/// is not allowed to generate IO packets.
pub fn allowed_ops(target_type: AvailableTargets) -> &'static TargetOps {
    match target_type {
        AvailableTargets::FileTarget => FileBlockingTarget::allowed_ops(),
    }
}
