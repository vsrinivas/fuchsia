// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::file_target::FileBlockingTarget,
    crate::target::{AvailableTargets, Error, Target, TargetOps},
    libc::c_void,
    log::debug,
    std::{ops::Range, os::unix::io::RawFd, result::Result, sync::Arc, time::Instant},
};

pub fn pwrite(raw_fd: RawFd, buffer: &mut Vec<u8>, offset: i64) -> Result<(), Error> {
    let ret =
        unsafe { libc::pwrite(raw_fd, buffer.as_ptr() as *const c_void, buffer.len(), offset) };
    debug!("safe_pwrite: {:?} {}", offset, ret);
    if ret < 0 {
        return Err(Error::DoIoError(std::io::Error::last_os_error().kind()));
    } else if ret < buffer.len() as isize {
        // TODO(auradkar): Define a set of error codes to be used throughout the app.
        return Err(Error::ShortWrite);
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
) -> Arc<Box<dyn Target + Send + Sync>> {
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

#[cfg(test)]
mod tests {

    use {
        crate::common_operations::pwrite,
        crate::target::Error,
        std::{fs::File, fs::OpenOptions, io::ErrorKind, os::unix::io::AsRawFd},
    };

    #[test]
    fn test_pwrite_error_write_to_read_only_file() {
        let file_name =
            "/tmp/odu-common_operations-test_pwrite_error_write_to_read_only_file-file01"
                .to_string();

        // Create a file in rw mode if it doesn't exists.
        File::create(&file_name).unwrap();

        // Open the file in read-only mode and try to write to it.
        let f = OpenOptions::new().read(true).write(false).open(file_name).unwrap();
        let mut buffer = vec![0; 100];

        let ret = pwrite(f.as_raw_fd(), &mut buffer, 0);
        assert!(ret.is_err());
        assert_eq!(ret.err(), Some(Error::DoIoError(ErrorKind::Other)));
    }
}
