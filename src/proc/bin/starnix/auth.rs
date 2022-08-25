// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use bitflags::bitflags;

use crate::types::{errno, error, gid_t, uapi, uid_t, Capabilities, Errno};

#[derive(Debug, Clone)]
pub struct Credentials {
    pub uid: uid_t,
    pub gid: gid_t,
    pub euid: uid_t,
    pub egid: gid_t,
    pub saved_uid: uid_t,
    pub saved_gid: gid_t,
    pub groups: Vec<gid_t>,

    /// From https://man7.org/linux/man-pages/man7/capabilities.7.html
    ///
    /// > This is a limiting superset for the effective capabilities that the thread may assume. It
    /// > is also a limiting superset for the capabilities that may be added to the inheritable set
    /// > by a thread that does not have the CAP_SETPCAP capability in its effective set.
    ///
    /// > If a thread drops a capability from its permitted set, it can never reacquire that
    /// > capability (unless it execve(2)s either a set-user-ID-root program, or a program whose
    /// > associated file capabilities grant that capability).
    pub cap_permitted: Capabilities,

    /// From https://man7.org/linux/man-pages/man7/capabilities.7.html
    ///
    /// > This is the set of capabilities used by the kernel to perform permission checks for the
    /// > thread.
    pub cap_effective: Capabilities,

    /// From https://man7.org/linux/man-pages/man7/capabilities.7.html
    ///
    /// > This is a set of capabilities preserved across an execve(2).  Inheritable capabilities
    /// > remain inheritable when executing any program, and inheritable capabilities are added to
    /// > the permitted set when executing a program that has the corresponding bits set in the file
    /// > inheritable set.
    ///
    /// > Because inheritable capabilities are not generally preserved across execve(2) when running
    /// > as a non-root user, applications that wish to run helper programs with elevated
    /// > capabilities should consider using ambient capabilities, described below.
    pub cap_inheritable: Capabilities,

    /// From https://man7.org/linux/man-pages/man7/capabilities.7.html
    ///
    /// > The capability bounding set is a mechanism that can be used to limit the capabilities that
    /// > are gained during execve(2).
    ///
    /// > Since Linux 2.6.25, this is a per-thread capability set. In older kernels, the capability
    /// > bounding set was a system wide attribute shared by all threads on the system.
    pub cap_bounding: Capabilities,

    /// From https://man7.org/linux/man-pages/man7/capabilities.7.html
    ///
    /// > This is a set of capabilities that are preserved across an execve(2) of a program that is
    /// > not privileged.  The ambient capability set obeys the invariant that no capability can
    /// > ever be ambient if it is not both permitted and inheritable.
    ///
    /// > Executing a program that changes UID or GID due to the set-user-ID or set-group-ID bits
    /// > or executing a program that has any file capabilities set will clear the ambient set.
    pub cap_ambient: Capabilities,

    /// From https://man7.org/linux/man-pages/man7/capabilities.7.html
    ///
    /// > Starting with kernel 2.6.26, and with a kernel in which file capabilities are enabled,
    /// > Linux implements a set of per-thread securebits flags that can be used to disable special
    /// > handling of capabilities for UID 0 (root).
    ///
    /// > The securebits flags can be modified and retrieved using the prctl(2)
    /// > PR_SET_SECUREBITS and PR_GET_SECUREBITS operations.  The CAP_SETPCAP capability is
    /// > required to modify the flags.
    pub securebits: SecureBits,
}

bitflags! {
    pub struct SecureBits: u32 {
        const KEEP_CAPS = 1 << uapi::SECURE_KEEP_CAPS;
        const KEEP_CAPS_LOCKED = 1 <<  uapi::SECURE_KEEP_CAPS_LOCKED;
        const NO_SETUID_FIXUP = 1 << uapi::SECURE_NO_SETUID_FIXUP;
        const NO_SETUID_FIXUP_LOCKED = 1 << uapi::SECURE_NO_SETUID_FIXUP_LOCKED;
        const NOROOT = 1 << uapi::SECURE_NOROOT;
        const NOROOT_LOCKED = 1 << uapi::SECURE_NOROOT_LOCKED;
        const NO_CAP_AMBIENT_RAISE = 1 << uapi::SECURE_NO_CAP_AMBIENT_RAISE;
        const NO_CAP_AMBIENT_RAISE_LOCKED = 1 << uapi::SECURE_NO_CAP_AMBIENT_RAISE_LOCKED;
    }
}

fn parse_id_number(id: Option<&str>) -> Result<u32, Errno> {
    let id_str = id.ok_or_else(|| errno!(EINVAL))?;
    let id_no: u32 = id_str.parse().map_err(|_| errno!(EINVAL))?;
    if id_str != id_no.to_string() {
        return error!(EINVAL);
    }
    Ok(id_no)
}

impl Credentials {
    /// Creates a set of credentials with all possible permissions and capabilities.
    pub fn root() -> Self {
        Self::with_ids(0, 0)
    }

    /// Creates a set of credentials with the given uid and gid. If the uid is 0, the credentials
    /// will grant superuser access.
    pub fn with_ids(uid: uid_t, gid: gid_t) -> Credentials {
        let caps = if uid == 0 { Capabilities::all() } else { Capabilities::empty() };
        Credentials {
            uid,
            gid,
            euid: uid,
            egid: gid,
            saved_uid: uid,
            saved_gid: gid,
            groups: vec![],
            cap_permitted: caps,
            cap_effective: caps,
            cap_inheritable: caps,
            cap_bounding: Capabilities::all(),
            cap_ambient: Capabilities::empty(),
            securebits: SecureBits::empty(),
        }
    }

    // Creates a new set of credentials from the start of an
    // /etc/passwd line.
    pub fn from_passwd(passwd_line: &str) -> Result<Credentials, Errno> {
        let mut fields = passwd_line.split(':');
        let name = fields.next().ok_or_else(|| errno!(EINVAL))?;
        let passwd = fields.next().ok_or_else(|| errno!(EINVAL))?;
        if name.is_empty() || passwd.is_empty() {
            return error!(EINVAL);
        }
        let uid: uid_t = parse_id_number(fields.next())?;
        let gid: gid_t = parse_id_number(fields.next())?;
        Ok(Self::with_ids(uid, gid))
    }

    /// Compares the user ID of `self` to that of `other`.
    ///
    /// Used to check whether a task can signal another.
    ///
    /// From https://man7.org/linux/man-pages/man2/kill.2.html:
    ///
    /// > For a process to have permission to send a signal, it must either be
    /// > privileged (under Linux: have the CAP_KILL capability in the user
    /// > namespace of the target process), or the real or effective user ID of
    /// > the sending process must equal the real or saved set- user-ID of the
    /// > target process.
    ///
    /// Returns true if the credentials are considered to have the same user ID.
    pub fn has_same_uid(&self, other: &Credentials) -> bool {
        self.euid == other.saved_uid
            || self.euid == other.uid
            || self.uid == other.uid
            || self.uid == other.saved_uid
    }

    pub fn is_superuser(&self) -> bool {
        self.euid == 0
    }

    pub fn is_in_group(&self, gid: gid_t) -> bool {
        self.egid == gid || self.groups.contains(&gid)
    }

    /// Returns whether or not the task has the given `capability`.
    pub fn has_capability(&self, capability: Capabilities) -> bool {
        self.cap_effective.contains(capability)
    }

    pub fn exec(&mut self) {
        // > Ambient capabilities are added to the permitted set and assigned to the effective set
        // > when execve(2) is called.
        // https://man7.org/linux/man-pages/man7/capabilities.7.html

        // TODO(security): This should take file capabilities into account.
        // (inheritable & file.inheritable) | (file.permitted & bounding) | ambient
        self.cap_permitted = self.cap_inheritable | self.cap_ambient;

        // TODO(security): This should take file capabilities into account.
        // if file.effective { permitted | ambient } else { 0 }
        self.cap_effective = self.cap_permitted;

        self.securebits.remove(SecureBits::KEEP_CAPS);
    }
}

/// The owner and group of a file. Used as a parameter for functions that create files.
#[derive(Debug, Clone)]
pub struct FsCred {
    pub uid: uid_t,
    pub gid: gid_t,
}

impl FsCred {
    pub fn root() -> Self {
        Self { uid: 0, gid: 0 }
    }
}
