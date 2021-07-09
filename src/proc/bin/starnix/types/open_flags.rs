// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use bitflags::bitflags;

use crate::types::uapi;

const O_ACCESS_MASK: u32 = 0x3;

bitflags! {
    pub struct OpenFlags: u32 {
      const ACCESS_MASK = O_ACCESS_MASK;

      // The access modes are not really bits. Instead, they're an enum
      // embedded in the bitfield. Use ACCESS_MASK to extract the enum
      // or use the OpenFlags::can_read and OpenFlags::can_write functions.
      const RDONLY = uapi::O_RDONLY;
      const WRONLY = uapi::O_WRONLY;
      const RDWR = uapi::O_RDWR;

      const CREAT = uapi::O_CREAT;
      const EXCL = uapi::O_EXCL;
      const NOCTTY = uapi::O_NOCTTY;
      const TRUNC = uapi::O_TRUNC;
      const APPEND = uapi::O_APPEND;
      const NONBLOCK = uapi::O_NONBLOCK;
      const DSYNC = uapi::O_DSYNC;
      const DIRECT = uapi::O_DIRECT;
      const LARGEFILE = uapi::O_LARGEFILE;
      const DIRECTORY = uapi::O_DIRECTORY;
      const NOFOLLOW = uapi::O_NOFOLLOW;
      const NOATIME = uapi::O_NOATIME;
      const CLOEXEC = uapi::O_CLOEXEC;
      const SYNC = uapi::O_SYNC;
      const PATH = uapi::O_PATH;
      const TMPFILE = uapi::O_TMPFILE;
      const NDELAY = uapi::O_NDELAY;
    }
}

impl OpenFlags {
    pub fn can_read(&self) -> bool {
        let access_mode = self.bits() & O_ACCESS_MASK;
        return access_mode == uapi::O_RDONLY || access_mode == uapi::O_RDWR;
    }

    pub fn can_write(&self) -> bool {
        let access_mode = self.bits() & O_ACCESS_MASK;
        return access_mode == uapi::O_WRONLY || access_mode == uapi::O_RDWR;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_access() {
        let read_only = OpenFlags::from_bits_truncate(uapi::O_RDONLY);
        assert!(read_only.can_read());
        assert!(!read_only.can_write());

        let write_only = OpenFlags::from_bits_truncate(uapi::O_WRONLY);
        assert!(!write_only.can_read());
        assert!(write_only.can_write());

        let read_write = OpenFlags::from_bits_truncate(uapi::O_RDWR);
        assert!(read_write.can_read());
        assert!(read_write.can_write());
    }
}
