// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(dead_code)]

use crate::types::uapi;

/// Represents the location at which an error was generated.
///
/// This is useful if the anyhow error associated with the `Errno` can't print backtraces.
pub struct ErrnoSource {
    pub file: String,
    pub line: u32,
}

impl std::fmt::Debug for ErrnoSource {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{:?}:{:?}", self.file, self.line)
    }
}

pub struct Errno {
    value: u32,
    anyhow: Option<anyhow::Error>,
}

impl Errno {
    pub fn new(
        value: u32,
        name: &'static str,
        context: Option<String>,
        source: ErrnoSource,
    ) -> Errno {
        Errno {
            value,
            anyhow: Some(anyhow::format_err!(
                "{:?} {} ({}), context: {}",
                source,
                name,
                value,
                context.as_ref().unwrap_or(&"None".to_string())
            )),
        }
    }

    const fn constant(value: u32) -> Self {
        Self { value, anyhow: None }
    }

    pub fn value(&self) -> u32 {
        self.value
    }

    pub fn return_value(&self) -> u64 {
        -(self.value as i32) as u64
    }
}

impl PartialEq for Errno {
    fn eq(&self, other: &Self) -> bool {
        self.value == other.value
    }
}

impl Eq for Errno {}

impl From<Errno> for anyhow::Error {
    fn from(e: Errno) -> anyhow::Error {
        let value = e.value;
        e.anyhow.unwrap_or_else(|| anyhow::format_err!("errno {} from unknown location", value))
    }
}

impl std::fmt::Debug for Errno {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> Result<(), std::fmt::Error> {
        if let Some(err) = self.anyhow.as_ref() {
            std::fmt::Debug::fmt(&err, f)
        } else {
            write!(f, "error {} from unknown location", self.value)
        }
    }
}

pub const EPERM: Errno = Errno::constant(uapi::EPERM);
pub const ENOENT: Errno = Errno::constant(uapi::ENOENT);
pub const ESRCH: Errno = Errno::constant(uapi::ESRCH);
pub const EINTR: Errno = Errno::constant(uapi::EINTR);
pub const EIO: Errno = Errno::constant(uapi::EIO);
pub const ENXIO: Errno = Errno::constant(uapi::ENXIO);
pub const E2BIG: Errno = Errno::constant(uapi::E2BIG);
pub const ENOEXEC: Errno = Errno::constant(uapi::ENOEXEC);
pub const EBADF: Errno = Errno::constant(uapi::EBADF);
pub const ECHILD: Errno = Errno::constant(uapi::ECHILD);
pub const EAGAIN: Errno = Errno::constant(uapi::EAGAIN);
pub const ENOMEM: Errno = Errno::constant(uapi::ENOMEM);
pub const EACCES: Errno = Errno::constant(uapi::EACCES);
pub const EFAULT: Errno = Errno::constant(uapi::EFAULT);
pub const ENOTBLK: Errno = Errno::constant(uapi::ENOTBLK);
pub const EBUSY: Errno = Errno::constant(uapi::EBUSY);
pub const EEXIST: Errno = Errno::constant(uapi::EEXIST);
pub const EXDEV: Errno = Errno::constant(uapi::EXDEV);
pub const ENODEV: Errno = Errno::constant(uapi::ENODEV);
pub const ENOTDIR: Errno = Errno::constant(uapi::ENOTDIR);
pub const EISDIR: Errno = Errno::constant(uapi::EISDIR);
pub const EINVAL: Errno = Errno::constant(uapi::EINVAL);
pub const ENFILE: Errno = Errno::constant(uapi::ENFILE);
pub const EMFILE: Errno = Errno::constant(uapi::EMFILE);
pub const ENOTTY: Errno = Errno::constant(uapi::ENOTTY);
pub const ETXTBSY: Errno = Errno::constant(uapi::ETXTBSY);
pub const EFBIG: Errno = Errno::constant(uapi::EFBIG);
pub const ENOSPC: Errno = Errno::constant(uapi::ENOSPC);
pub const ESPIPE: Errno = Errno::constant(uapi::ESPIPE);
pub const EROFS: Errno = Errno::constant(uapi::EROFS);
pub const EMLINK: Errno = Errno::constant(uapi::EMLINK);
pub const EPIPE: Errno = Errno::constant(uapi::EPIPE);
pub const EDOM: Errno = Errno::constant(uapi::EDOM);
pub const ERANGE: Errno = Errno::constant(uapi::ERANGE);
pub const EDEADLK: Errno = Errno::constant(uapi::EDEADLK);
pub const ENAMETOOLONG: Errno = Errno::constant(uapi::ENAMETOOLONG);
pub const ENOLCK: Errno = Errno::constant(uapi::ENOLCK);
pub const ENOSYS: Errno = Errno::constant(uapi::ENOSYS);
pub const ENOTEMPTY: Errno = Errno::constant(uapi::ENOTEMPTY);
pub const ELOOP: Errno = Errno::constant(uapi::ELOOP);
pub const EWOULDBLOCK: Errno = Errno::constant(uapi::EWOULDBLOCK);
pub const ENOMSG: Errno = Errno::constant(uapi::ENOMSG);
pub const EIDRM: Errno = Errno::constant(uapi::EIDRM);
pub const ECHRNG: Errno = Errno::constant(uapi::ECHRNG);
pub const EL2NSYNC: Errno = Errno::constant(uapi::EL2NSYNC);
pub const EL3HLT: Errno = Errno::constant(uapi::EL3HLT);
pub const EL3RST: Errno = Errno::constant(uapi::EL3RST);
pub const ELNRNG: Errno = Errno::constant(uapi::ELNRNG);
pub const EUNATCH: Errno = Errno::constant(uapi::EUNATCH);
pub const ENOCSI: Errno = Errno::constant(uapi::ENOCSI);
pub const EL2HLT: Errno = Errno::constant(uapi::EL2HLT);
pub const EBADE: Errno = Errno::constant(uapi::EBADE);
pub const EBADR: Errno = Errno::constant(uapi::EBADR);
pub const EXFULL: Errno = Errno::constant(uapi::EXFULL);
pub const ENOANO: Errno = Errno::constant(uapi::ENOANO);
pub const EBADRQC: Errno = Errno::constant(uapi::EBADRQC);
pub const EBADSLT: Errno = Errno::constant(uapi::EBADSLT);
pub const EDEADLOCK: Errno = Errno::constant(uapi::EDEADLOCK);
pub const EBFONT: Errno = Errno::constant(uapi::EBFONT);
pub const ENOSTR: Errno = Errno::constant(uapi::ENOSTR);
pub const ENODATA: Errno = Errno::constant(uapi::ENODATA);
pub const ETIME: Errno = Errno::constant(uapi::ETIME);
pub const ENOSR: Errno = Errno::constant(uapi::ENOSR);
pub const ENONET: Errno = Errno::constant(uapi::ENONET);
pub const ENOPKG: Errno = Errno::constant(uapi::ENOPKG);
pub const EREMOTE: Errno = Errno::constant(uapi::EREMOTE);
pub const ENOLINK: Errno = Errno::constant(uapi::ENOLINK);
pub const EADV: Errno = Errno::constant(uapi::EADV);
pub const ESRMNT: Errno = Errno::constant(uapi::ESRMNT);
pub const ECOMM: Errno = Errno::constant(uapi::ECOMM);
pub const EPROTO: Errno = Errno::constant(uapi::EPROTO);
pub const EMULTIHOP: Errno = Errno::constant(uapi::EMULTIHOP);
pub const EDOTDOT: Errno = Errno::constant(uapi::EDOTDOT);
pub const EBADMSG: Errno = Errno::constant(uapi::EBADMSG);
pub const EOVERFLOW: Errno = Errno::constant(uapi::EOVERFLOW);
pub const ENOTUNIQ: Errno = Errno::constant(uapi::ENOTUNIQ);
pub const EBADFD: Errno = Errno::constant(uapi::EBADFD);
pub const EREMCHG: Errno = Errno::constant(uapi::EREMCHG);
pub const ELIBACC: Errno = Errno::constant(uapi::ELIBACC);
pub const ELIBBAD: Errno = Errno::constant(uapi::ELIBBAD);
pub const ELIBSCN: Errno = Errno::constant(uapi::ELIBSCN);
pub const ELIBMAX: Errno = Errno::constant(uapi::ELIBMAX);
pub const ELIBEXEC: Errno = Errno::constant(uapi::ELIBEXEC);
pub const EILSEQ: Errno = Errno::constant(uapi::EILSEQ);
pub const ERESTART: Errno = Errno::constant(uapi::ERESTART);
pub const ESTRPIPE: Errno = Errno::constant(uapi::ESTRPIPE);
pub const EUSERS: Errno = Errno::constant(uapi::EUSERS);
pub const ENOTSOCK: Errno = Errno::constant(uapi::ENOTSOCK);
pub const EDESTADDRREQ: Errno = Errno::constant(uapi::EDESTADDRREQ);
pub const EMSGSIZE: Errno = Errno::constant(uapi::EMSGSIZE);
pub const EPROTOTYPE: Errno = Errno::constant(uapi::EPROTOTYPE);
pub const ENOPROTOOPT: Errno = Errno::constant(uapi::ENOPROTOOPT);
pub const EPROTONOSUPPORT: Errno = Errno::constant(uapi::EPROTONOSUPPORT);
pub const ESOCKTNOSUPPORT: Errno = Errno::constant(uapi::ESOCKTNOSUPPORT);
pub const EOPNOTSUPP: Errno = Errno::constant(uapi::EOPNOTSUPP);
pub const EPFNOSUPPORT: Errno = Errno::constant(uapi::EPFNOSUPPORT);
pub const EAFNOSUPPORT: Errno = Errno::constant(uapi::EAFNOSUPPORT);
pub const EADDRINUSE: Errno = Errno::constant(uapi::EADDRINUSE);
pub const EADDRNOTAVAIL: Errno = Errno::constant(uapi::EADDRNOTAVAIL);
pub const ENETDOWN: Errno = Errno::constant(uapi::ENETDOWN);
pub const ENETUNREACH: Errno = Errno::constant(uapi::ENETUNREACH);
pub const ENETRESET: Errno = Errno::constant(uapi::ENETRESET);
pub const ECONNABORTED: Errno = Errno::constant(uapi::ECONNABORTED);
pub const ECONNRESET: Errno = Errno::constant(uapi::ECONNRESET);
pub const ENOBUFS: Errno = Errno::constant(uapi::ENOBUFS);
pub const EISCONN: Errno = Errno::constant(uapi::EISCONN);
pub const ENOTCONN: Errno = Errno::constant(uapi::ENOTCONN);
pub const ESHUTDOWN: Errno = Errno::constant(uapi::ESHUTDOWN);
pub const ETOOMANYREFS: Errno = Errno::constant(uapi::ETOOMANYREFS);
pub const ETIMEDOUT: Errno = Errno::constant(uapi::ETIMEDOUT);
pub const ECONNREFUSED: Errno = Errno::constant(uapi::ECONNREFUSED);
pub const EHOSTDOWN: Errno = Errno::constant(uapi::EHOSTDOWN);
pub const EHOSTUNREACH: Errno = Errno::constant(uapi::EHOSTUNREACH);
pub const EALREADY: Errno = Errno::constant(uapi::EALREADY);
pub const EINPROGRESS: Errno = Errno::constant(uapi::EINPROGRESS);
pub const ESTALE: Errno = Errno::constant(uapi::ESTALE);
pub const EUCLEAN: Errno = Errno::constant(uapi::EUCLEAN);
pub const ENOTNAM: Errno = Errno::constant(uapi::ENOTNAM);
pub const ENAVAIL: Errno = Errno::constant(uapi::ENAVAIL);
pub const EISNAM: Errno = Errno::constant(uapi::EISNAM);
pub const EREMOTEIO: Errno = Errno::constant(uapi::EREMOTEIO);
pub const EDQUOT: Errno = Errno::constant(uapi::EDQUOT);
pub const ENOMEDIUM: Errno = Errno::constant(uapi::ENOMEDIUM);
pub const EMEDIUMTYPE: Errno = Errno::constant(uapi::EMEDIUMTYPE);
pub const ECANCELED: Errno = Errno::constant(uapi::ECANCELED);
pub const ENOKEY: Errno = Errno::constant(uapi::ENOKEY);
pub const EKEYEXPIRED: Errno = Errno::constant(uapi::EKEYEXPIRED);
pub const EKEYREVOKED: Errno = Errno::constant(uapi::EKEYREVOKED);
pub const EKEYREJECTED: Errno = Errno::constant(uapi::EKEYREJECTED);
pub const EOWNERDEAD: Errno = Errno::constant(uapi::EOWNERDEAD);
pub const ENOTRECOVERABLE: Errno = Errno::constant(uapi::ENOTRECOVERABLE);
pub const ERFKILL: Errno = Errno::constant(uapi::ERFKILL);
pub const EHWPOISON: Errno = Errno::constant(uapi::EHWPOISON);

// ENOTSUP is a different error in posix, but has the same value as EOPNOTSUPP in linux.
pub const ENOTSUP: Errno = EOPNOTSUPP;

/// A special error that represents an interrupted syscall that should be restarted if possible.
// This is not defined in uapi as it is never exposed to userspace.
pub const ERESTARTSYS: Errno = Errno { value: 512, anyhow: None };

/// `errno` returns an `Errno` struct tagged with the current file name and line number.
///
/// Use `error!` instead if you want the `Errno` to be wrapped in an `Err`.
macro_rules! errno {
    ($err:ident) => {
        Errno::new(
            crate::types::errno::$err.value(),
            stringify!($err),
            None,
            crate::types::errno::ErrnoSource { file: file!().to_string(), line: line!() },
        )
    };
    ($err:ident, $context:expr) => {
        Errno::new(
            crate::types::errno::$err.value(),
            stringify!($err),
            Some($context.to_string()),
            crate::types::errno::ErrnoSource { file: file!().to_string(), line: line!() },
        )
    };
}

/// `error` returns a `Err` containing an `Errno` struct tagged with the current file name and line
/// number.
///
/// Use `errno!` instead if you want an unwrapped, but still tagged, `Errno`.
macro_rules! error {
    ($($args:tt)*) => { Err(errno!($($args)*)) };
}

// There isn't really a mapping from zx::Status to Errno. The correct mapping is context-speific
// but this converter is a reasonable first-approximation. The translation matches
// fdio_status_to_errno. See fxbug.dev/30921 for more context.
// TODO: Replace clients with more context-specific mappings.
macro_rules! from_status_like_fdio {
    ($status:ident) => {{
        from_status_like_fdio!($status, "")
    }};
    ($status:ident, $context:expr) => {{
        match $status {
            zx::Status::NOT_FOUND => errno!(ENOENT, $context),
            zx::Status::NO_MEMORY => errno!(ENOMEM, $context),
            zx::Status::INVALID_ARGS => errno!(EINVAL, $context),
            zx::Status::BUFFER_TOO_SMALL => errno!(EINVAL, $context),
            zx::Status::TIMED_OUT => errno!(ETIMEDOUT, $context),
            zx::Status::UNAVAILABLE => errno!(EBUSY, $context),
            zx::Status::ALREADY_EXISTS => errno!(EEXIST, $context),
            zx::Status::PEER_CLOSED => errno!(EPIPE, $context),
            zx::Status::BAD_STATE => errno!(EPIPE, $context),
            zx::Status::BAD_PATH => errno!(ENAMETOOLONG, $context),
            zx::Status::IO => errno!(EIO, $context),
            zx::Status::NOT_FILE => errno!(EISDIR, $context),
            zx::Status::NOT_DIR => errno!(ENOTDIR, $context),
            zx::Status::NOT_SUPPORTED => errno!(EOPNOTSUPP, $context),
            zx::Status::WRONG_TYPE => errno!(EOPNOTSUPP, $context),
            zx::Status::OUT_OF_RANGE => errno!(EINVAL, $context),
            zx::Status::NO_RESOURCES => errno!(ENOMEM, $context),
            zx::Status::BAD_HANDLE => errno!(EBADF, $context),
            zx::Status::ACCESS_DENIED => errno!(EACCES, $context),
            zx::Status::SHOULD_WAIT => errno!(EAGAIN, $context),
            zx::Status::FILE_BIG => errno!(EFBIG, $context),
            zx::Status::NO_SPACE => errno!(ENOSPC, $context),
            zx::Status::NOT_EMPTY => errno!(ENOTEMPTY, $context),
            zx::Status::IO_REFUSED => errno!(ECONNREFUSED, $context),
            zx::Status::IO_INVALID => errno!(EIO, $context),
            zx::Status::CANCELED => errno!(EBADF, $context),
            zx::Status::PROTOCOL_NOT_SUPPORTED => errno!(EPROTONOSUPPORT, $context),
            zx::Status::ADDRESS_UNREACHABLE => errno!(ENETUNREACH, $context),
            zx::Status::ADDRESS_IN_USE => errno!(EADDRINUSE, $context),
            zx::Status::NOT_CONNECTED => errno!(ENOTCONN, $context),
            zx::Status::CONNECTION_REFUSED => errno!(ECONNREFUSED, $context),
            zx::Status::CONNECTION_RESET => errno!(ECONNRESET, $context),
            zx::Status::CONNECTION_ABORTED => errno!(ECONNABORTED, $context),
            _ => errno!(EIO, $context),
        }
    }};
}

// Public re-export of macros allows them to be used like regular rust items.
pub(crate) use errno;
pub(crate) use error;
pub(crate) use from_status_like_fdio;

/// An extension trait for `Result<T, Errno>`.
pub trait ErrnoResultExt<T> {
    /// Maps `Err(EINTR)` to `Err(ERESTARTSYS)`, which tells the kernel to restart the syscall if
    /// the dispatched signal action that interrupted the syscall has the `SA_RESTART` flag.
    fn restartable(self) -> Result<T, Errno>;
}

impl<T> ErrnoResultExt<T> for Result<T, Errno> {
    fn restartable(self) -> Result<T, Errno> {
        self.map_err(|err| if err == EINTR { errno!(ERESTARTSYS) } else { err })
    }
}
