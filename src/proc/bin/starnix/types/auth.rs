// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(dead_code)]

use std::ops;

use crate::types::*;

// We don't use bitflags for this because capability sets can have bits set that don't have defined
// meaning as capabilities. init has all 64 bits set, even though only 40 of them are valid.
#[derive(Clone, Copy, Eq, PartialEq)]
pub struct Capabilities {
    mask: u64,
}

impl Capabilities {
    pub fn empty() -> Self {
        Self { mask: 0 }
    }

    pub fn all() -> Self {
        Self { mask: u64::MAX }
    }

    pub fn union(&self, caps: Capabilities) -> Self {
        let mut new_caps = *self;
        new_caps.insert(caps);
        new_caps
    }

    pub fn difference(&self, caps: Capabilities) -> Self {
        let mut new_caps = *self;
        new_caps.remove(caps);
        new_caps
    }

    pub fn contains(self, caps: Capabilities) -> bool {
        (self & caps) == caps
    }

    pub fn insert(&mut self, caps: Capabilities) {
        *self |= caps;
    }

    pub fn remove(&mut self, caps: Capabilities) {
        *self &= !caps;
    }

    pub fn as_abi_v3(self) -> (u32, u32) {
        (self.mask as u32, (self.mask >> 32) as u32)
    }

    pub fn from_abi_v3(u32s: (u32, u32)) -> Self {
        Self { mask: u32s.0 as u64 | ((u32s.1 as u64) << 32) }
    }
}

impl std::convert::TryFrom<u64> for Capabilities {
    type Error = Errno;

    fn try_from(capability_num: u64) -> Result<Self, Self::Error> {
        match 1u64.checked_shl(capability_num as u32) {
            Some(mask) => Ok(Self { mask }),
            _ => error!(EINVAL),
        }
    }
}

impl ops::BitAnd for Capabilities {
    type Output = Self;

    // rhs is the "right-hand side" of the expression `a & b`
    fn bitand(self, rhs: Self) -> Self::Output {
        Self { mask: self.mask & rhs.mask }
    }
}

impl ops::BitAndAssign for Capabilities {
    // rhs is the "right-hand side" of the expression `a & b`
    fn bitand_assign(&mut self, rhs: Self) {
        self.mask &= rhs.mask;
    }
}

impl ops::BitOr for Capabilities {
    type Output = Self;

    fn bitor(self, rhs: Self) -> Self::Output {
        Self { mask: self.mask | rhs.mask }
    }
}

impl ops::BitOrAssign for Capabilities {
    fn bitor_assign(&mut self, rhs: Self) {
        self.mask |= rhs.mask;
    }
}

impl ops::Not for Capabilities {
    type Output = Self;

    fn not(self) -> Self::Output {
        Self { mask: !self.mask }
    }
}

impl std::fmt::Debug for Capabilities {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> Result<(), std::fmt::Error> {
        write!(f, "Capabilities({:#x})", self.mask)
    }
}

pub const CAP_CHOWN: Capabilities = Capabilities { mask: 1u64 << uapi::CAP_CHOWN };
pub const CAP_DAC_OVERRIDE: Capabilities = Capabilities { mask: 1u64 << uapi::CAP_DAC_OVERRIDE };
pub const CAP_DAC_READ_SEARCH: Capabilities =
    Capabilities { mask: 1u64 << uapi::CAP_DAC_READ_SEARCH };
pub const CAP_FOWNER: Capabilities = Capabilities { mask: 1u64 << uapi::CAP_FOWNER };
pub const CAP_FSETID: Capabilities = Capabilities { mask: 1u64 << uapi::CAP_FSETID };
pub const CAP_KILL: Capabilities = Capabilities { mask: 1u64 << uapi::CAP_KILL };
pub const CAP_SETGID: Capabilities = Capabilities { mask: 1u64 << uapi::CAP_SETGID };
pub const CAP_SETUID: Capabilities = Capabilities { mask: 1u64 << uapi::CAP_SETUID };
pub const CAP_SETPCAP: Capabilities = Capabilities { mask: 1u64 << uapi::CAP_SETPCAP };
pub const CAP_LINUX_IMMUTABLE: Capabilities =
    Capabilities { mask: 1u64 << uapi::CAP_LINUX_IMMUTABLE };
pub const CAP_NET_BIND_SERVICE: Capabilities =
    Capabilities { mask: 1u64 << uapi::CAP_NET_BIND_SERVICE };
pub const CAP_NET_BROADCAST: Capabilities = Capabilities { mask: 1u64 << uapi::CAP_NET_BROADCAST };
pub const CAP_NET_ADMIN: Capabilities = Capabilities { mask: 1u64 << uapi::CAP_NET_ADMIN };
pub const CAP_NET_RAW: Capabilities = Capabilities { mask: 1u64 << uapi::CAP_NET_RAW };
pub const CAP_IPC_LOCK: Capabilities = Capabilities { mask: 1u64 << uapi::CAP_IPC_LOCK };
pub const CAP_IPC_OWNER: Capabilities = Capabilities { mask: 1u64 << uapi::CAP_IPC_OWNER };
pub const CAP_SYS_MODULE: Capabilities = Capabilities { mask: 1u64 << uapi::CAP_SYS_MODULE };
pub const CAP_SYS_RAWIO: Capabilities = Capabilities { mask: 1u64 << uapi::CAP_SYS_RAWIO };
pub const CAP_SYS_CHROOT: Capabilities = Capabilities { mask: 1u64 << uapi::CAP_SYS_CHROOT };
pub const CAP_SYS_PTRACE: Capabilities = Capabilities { mask: 1u64 << uapi::CAP_SYS_PTRACE };
pub const CAP_SYS_PACCT: Capabilities = Capabilities { mask: 1u64 << uapi::CAP_SYS_PACCT };
pub const CAP_SYS_ADMIN: Capabilities = Capabilities { mask: 1u64 << uapi::CAP_SYS_ADMIN };
pub const CAP_SYS_BOOT: Capabilities = Capabilities { mask: 1u64 << uapi::CAP_SYS_BOOT };
pub const CAP_SYS_NICE: Capabilities = Capabilities { mask: 1u64 << uapi::CAP_SYS_NICE };
pub const CAP_SYS_RESOURCE: Capabilities = Capabilities { mask: 1u64 << uapi::CAP_SYS_RESOURCE };
pub const CAP_SYS_TIME: Capabilities = Capabilities { mask: 1u64 << uapi::CAP_SYS_TIME };
pub const CAP_SYS_TTY_CONFIG: Capabilities =
    Capabilities { mask: 1u64 << uapi::CAP_SYS_TTY_CONFIG };
pub const CAP_MKNOD: Capabilities = Capabilities { mask: 1u64 << uapi::CAP_MKNOD };
pub const CAP_LEASE: Capabilities = Capabilities { mask: 1u64 << uapi::CAP_LEASE };
pub const CAP_AUDIT_WRITE: Capabilities = Capabilities { mask: 1u64 << uapi::CAP_AUDIT_WRITE };
pub const CAP_AUDIT_CONTROL: Capabilities = Capabilities { mask: 1u64 << uapi::CAP_AUDIT_CONTROL };
pub const CAP_SETFCAP: Capabilities = Capabilities { mask: 1u64 << uapi::CAP_SETFCAP };
pub const CAP_MAC_OVERRIDE: Capabilities = Capabilities { mask: 1u64 << uapi::CAP_MAC_OVERRIDE };
pub const CAP_MAC_ADMIN: Capabilities = Capabilities { mask: 1u64 << uapi::CAP_MAC_ADMIN };
pub const CAP_SYSLOG: Capabilities = Capabilities { mask: 1u64 << uapi::CAP_SYSLOG };
pub const CAP_WAKE_ALARM: Capabilities = Capabilities { mask: 1u64 << uapi::CAP_WAKE_ALARM };
pub const CAP_BLOCK_SUSPEND: Capabilities = Capabilities { mask: 1u64 << uapi::CAP_BLOCK_SUSPEND };
pub const CAP_AUDIT_READ: Capabilities = Capabilities { mask: 1u64 << uapi::CAP_AUDIT_READ };
pub const CAP_PERFMON: Capabilities = Capabilities { mask: 1u64 << uapi::CAP_PERFMON };
pub const CAP_BPF: Capabilities = Capabilities { mask: 1u64 << uapi::CAP_BPF };
pub const CAP_CHECKPOINT_RESTORE: Capabilities =
    Capabilities { mask: 1u64 << uapi::CAP_CHECKPOINT_RESTORE };

#[cfg(test)]
mod tests {
    use super::*;

    #[::fuchsia::test]
    fn test_empty() {
        assert_eq!(Capabilities::empty().mask, 0);
    }

    #[::fuchsia::test]
    fn test_all() {
        // all() should be every bit set, not just all the CAP_* constants.
        assert_eq!(Capabilities::all().mask, u64::MAX);
    }

    #[::fuchsia::test]
    fn test_union() {
        let expected = Capabilities { mask: CAP_BLOCK_SUSPEND.mask | CAP_AUDIT_READ.mask };
        assert_eq!(CAP_BLOCK_SUSPEND.union(CAP_AUDIT_READ), expected);
        assert_eq!(CAP_BLOCK_SUSPEND.union(CAP_BLOCK_SUSPEND), CAP_BLOCK_SUSPEND);
    }

    #[::fuchsia::test]
    fn test_difference() {
        let base = CAP_BPF | CAP_AUDIT_WRITE;
        let expected = CAP_BPF;
        assert_eq!(base.difference(CAP_AUDIT_WRITE), expected);
        assert_eq!(base.difference(CAP_AUDIT_WRITE | CAP_BPF), Capabilities::empty());
    }

    #[::fuchsia::test]
    fn test_contains() {
        let base = CAP_BPF | CAP_AUDIT_WRITE;
        assert!(base.contains(CAP_AUDIT_WRITE));
        assert!(base.contains(CAP_BPF));
        assert!(base.contains(CAP_AUDIT_WRITE | CAP_BPF));

        assert!(!base.contains(CAP_AUDIT_CONTROL));
        assert!(!base.contains(CAP_AUDIT_WRITE | CAP_BPF | CAP_AUDIT_CONTROL));
    }

    #[::fuchsia::test]
    fn test_insert() {
        let mut capabilities = CAP_BLOCK_SUSPEND;
        capabilities.insert(CAP_BLOCK_SUSPEND);
        assert_eq!(capabilities, CAP_BLOCK_SUSPEND);

        capabilities.insert(CAP_AUDIT_READ);
        let expected = Capabilities { mask: CAP_BLOCK_SUSPEND.mask | CAP_AUDIT_READ.mask };
        assert_eq!(capabilities, expected);
    }

    #[::fuchsia::test]
    fn test_remove() {
        let mut capabilities = CAP_BLOCK_SUSPEND;
        capabilities.remove(CAP_BLOCK_SUSPEND);
        assert_eq!(capabilities, Capabilities::empty());

        let mut capabilities = CAP_BLOCK_SUSPEND | CAP_AUDIT_READ;
        capabilities.remove(CAP_AUDIT_READ);
        assert_eq!(capabilities, CAP_BLOCK_SUSPEND);
    }

    #[::fuchsia::test]
    fn test_try_from() {
        let capabilities = CAP_BLOCK_SUSPEND;
        assert_eq!(Capabilities::try_from(uapi::CAP_BLOCK_SUSPEND as u64), Ok(capabilities));

        assert_eq!(Capabilities::try_from(200000), Err(EINVAL));
    }
}
