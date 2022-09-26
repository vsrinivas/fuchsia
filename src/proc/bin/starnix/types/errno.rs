// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(dead_code)]

use std::fmt::{Debug, Display, Formatter};

use crate::types::uapi;
use static_assertions::const_assert_eq;

/// Represents the location at which an error was generated.
///
/// This is useful if the anyhow error associated with the `Errno` can't print backtraces.
pub struct ErrnoSource {
    pub file: String,
    pub line: u32,
}

impl Debug for ErrnoSource {
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        write!(f, "{:?}:{:?}", self.file, self.line)
    }
}

pub struct Errno {
    pub code: ErrnoCode,
    anyhow: Option<anyhow::Error>,
}

impl Errno {
    pub fn new(
        code: ErrnoCode,
        name: &'static str,
        context: Option<String>,
        source: ErrnoSource,
    ) -> Errno {
        Errno {
            code,
            anyhow: Some(anyhow::format_err!(
                "{:?} {} ({}), context: {}",
                source,
                name,
                code,
                context.as_ref().unwrap_or(&"None".to_string())
            )),
        }
    }

    pub fn return_value(&self) -> u64 {
        self.code.return_value()
    }
}

impl PartialEq for Errno {
    fn eq(&self, other: &Self) -> bool {
        self.code == other.code
    }
}

impl PartialEq<ErrnoCode> for Errno {
    fn eq(&self, other: &ErrnoCode) -> bool {
        self.code == *other
    }
}

impl Eq for Errno {}

impl From<Errno> for anyhow::Error {
    fn from(e: Errno) -> anyhow::Error {
        let code = e.code;
        e.anyhow.unwrap_or_else(|| anyhow::format_err!("errno {} from unknown location", code))
    }
}

impl Debug for Errno {
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        if let Some(err) = self.anyhow.as_ref() {
            Debug::fmt(&err, f)
        } else {
            write!(f, "error {} from unknown location", self.code)
        }
    }
}

#[derive(Debug, Eq, PartialEq, Copy, Clone)]
pub struct ErrnoCode(u32);
impl ErrnoCode {
    pub fn from_return_value(retval: u64) -> Self {
        let retval = retval as i64;
        if retval >= 0 {
            // Collapse all success codes to 0. This is the only value in the u32 range which
            // is guaranteed to not be an error code.
            return Self(0);
        }
        Self(-retval as u32)
    }

    pub fn from_error_code(code: i16) -> Self {
        Self(code as u32)
    }

    pub fn return_value(&self) -> u64 {
        -(self.0 as i32) as u64
    }
}

impl Display for ErrnoCode {
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", self.0)
    }
}

// Special errors indicating a blocking syscall was interrupted, but it can be restarted.
//
// They are not defined in uapi, but can be observed by ptrace on Linux.
//
// If the syscall is restartable, it might not be restarted, depending on the value of SA_RESTART
// for the signal handler and the specific restartable error code.
// But it will always be restarted if the signal did not call a userspace signal handler.
// If not restarted, this error code is converted into EINTR.
//
// More information can be found at
// https://cs.opensource.google/gvisor/gvisor/+/master:pkg/errors/linuxerr/internal.go;l=71;drc=2bb73c7bd7dcf0b36e774d8e82e464d04bc81f4b.

/// Convert to EINTR if interrupted by a signal handler without SA_RESTART enabled, otherwise
/// restart.
pub const ERESTARTSYS: ErrnoCode = ErrnoCode(512);

/// Always restart, regardless of the signal handler.
pub const ERESTARTNOINTR: ErrnoCode = ErrnoCode(513);

/// Convert to EINTR if interrupted by a signal handler. SA_RESTART is ignored. Otherwise restart.
pub const ERESTARTNOHAND: ErrnoCode = ErrnoCode(514);

/// Like `ERESTARTNOHAND`, but restart by invoking a closure instead of calling the syscall
/// implementation again.
pub const ERESTART_RESTARTBLOCK: ErrnoCode = ErrnoCode(515);

/// An extension trait for `Result<T, Errno>`.
pub trait ErrnoResultExt<T> {
    /// Maps `Err(EINTR)` to the specified errno.
    fn map_eintr(self, errno: Errno) -> Result<T, Errno>;
}

impl<T> ErrnoResultExt<T> for Result<T, Errno> {
    fn map_eintr(self, errno: Errno) -> Result<T, Errno> {
        self.map_err(|err| if err == EINTR { errno } else { err })
    }
}

pub const EPERM: ErrnoCode = ErrnoCode(uapi::EPERM);
pub const ENOENT: ErrnoCode = ErrnoCode(uapi::ENOENT);
pub const ESRCH: ErrnoCode = ErrnoCode(uapi::ESRCH);
pub const EINTR: ErrnoCode = ErrnoCode(uapi::EINTR);
pub const EIO: ErrnoCode = ErrnoCode(uapi::EIO);
pub const ENXIO: ErrnoCode = ErrnoCode(uapi::ENXIO);
pub const E2BIG: ErrnoCode = ErrnoCode(uapi::E2BIG);
pub const ENOEXEC: ErrnoCode = ErrnoCode(uapi::ENOEXEC);
pub const EBADF: ErrnoCode = ErrnoCode(uapi::EBADF);
pub const ECHILD: ErrnoCode = ErrnoCode(uapi::ECHILD);
pub const EAGAIN: ErrnoCode = ErrnoCode(uapi::EAGAIN);
pub const ENOMEM: ErrnoCode = ErrnoCode(uapi::ENOMEM);
pub const EACCES: ErrnoCode = ErrnoCode(uapi::EACCES);
pub const EFAULT: ErrnoCode = ErrnoCode(uapi::EFAULT);
pub const ENOTBLK: ErrnoCode = ErrnoCode(uapi::ENOTBLK);
pub const EBUSY: ErrnoCode = ErrnoCode(uapi::EBUSY);
pub const EEXIST: ErrnoCode = ErrnoCode(uapi::EEXIST);
pub const EXDEV: ErrnoCode = ErrnoCode(uapi::EXDEV);
pub const ENODEV: ErrnoCode = ErrnoCode(uapi::ENODEV);
pub const ENOTDIR: ErrnoCode = ErrnoCode(uapi::ENOTDIR);
pub const EISDIR: ErrnoCode = ErrnoCode(uapi::EISDIR);
pub const EINVAL: ErrnoCode = ErrnoCode(uapi::EINVAL);
pub const ENFILE: ErrnoCode = ErrnoCode(uapi::ENFILE);
pub const EMFILE: ErrnoCode = ErrnoCode(uapi::EMFILE);
pub const ENOTTY: ErrnoCode = ErrnoCode(uapi::ENOTTY);
pub const ETXTBSY: ErrnoCode = ErrnoCode(uapi::ETXTBSY);
pub const EFBIG: ErrnoCode = ErrnoCode(uapi::EFBIG);
pub const ENOSPC: ErrnoCode = ErrnoCode(uapi::ENOSPC);
pub const ESPIPE: ErrnoCode = ErrnoCode(uapi::ESPIPE);
pub const EROFS: ErrnoCode = ErrnoCode(uapi::EROFS);
pub const EMLINK: ErrnoCode = ErrnoCode(uapi::EMLINK);
pub const EPIPE: ErrnoCode = ErrnoCode(uapi::EPIPE);
pub const EDOM: ErrnoCode = ErrnoCode(uapi::EDOM);
pub const ERANGE: ErrnoCode = ErrnoCode(uapi::ERANGE);
pub const EDEADLK: ErrnoCode = ErrnoCode(uapi::EDEADLK);
pub const ENAMETOOLONG: ErrnoCode = ErrnoCode(uapi::ENAMETOOLONG);
pub const ENOLCK: ErrnoCode = ErrnoCode(uapi::ENOLCK);
pub const ENOSYS: ErrnoCode = ErrnoCode(uapi::ENOSYS);
pub const ENOTEMPTY: ErrnoCode = ErrnoCode(uapi::ENOTEMPTY);
pub const ELOOP: ErrnoCode = ErrnoCode(uapi::ELOOP);
pub const EWOULDBLOCK: ErrnoCode = ErrnoCode(uapi::EWOULDBLOCK);
pub const ENOMSG: ErrnoCode = ErrnoCode(uapi::ENOMSG);
pub const EIDRM: ErrnoCode = ErrnoCode(uapi::EIDRM);
pub const ECHRNG: ErrnoCode = ErrnoCode(uapi::ECHRNG);
pub const EL2NSYNC: ErrnoCode = ErrnoCode(uapi::EL2NSYNC);
pub const EL3HLT: ErrnoCode = ErrnoCode(uapi::EL3HLT);
pub const EL3RST: ErrnoCode = ErrnoCode(uapi::EL3RST);
pub const ELNRNG: ErrnoCode = ErrnoCode(uapi::ELNRNG);
pub const EUNATCH: ErrnoCode = ErrnoCode(uapi::EUNATCH);
pub const ENOCSI: ErrnoCode = ErrnoCode(uapi::ENOCSI);
pub const EL2HLT: ErrnoCode = ErrnoCode(uapi::EL2HLT);
pub const EBADE: ErrnoCode = ErrnoCode(uapi::EBADE);
pub const EBADR: ErrnoCode = ErrnoCode(uapi::EBADR);
pub const EXFULL: ErrnoCode = ErrnoCode(uapi::EXFULL);
pub const ENOANO: ErrnoCode = ErrnoCode(uapi::ENOANO);
pub const EBADRQC: ErrnoCode = ErrnoCode(uapi::EBADRQC);
pub const EBADSLT: ErrnoCode = ErrnoCode(uapi::EBADSLT);
pub const EDEADLOCK: ErrnoCode = ErrnoCode(uapi::EDEADLOCK);
pub const EBFONT: ErrnoCode = ErrnoCode(uapi::EBFONT);
pub const ENOSTR: ErrnoCode = ErrnoCode(uapi::ENOSTR);
pub const ENODATA: ErrnoCode = ErrnoCode(uapi::ENODATA);
pub const ETIME: ErrnoCode = ErrnoCode(uapi::ETIME);
pub const ENOSR: ErrnoCode = ErrnoCode(uapi::ENOSR);
pub const ENONET: ErrnoCode = ErrnoCode(uapi::ENONET);
pub const ENOPKG: ErrnoCode = ErrnoCode(uapi::ENOPKG);
pub const EREMOTE: ErrnoCode = ErrnoCode(uapi::EREMOTE);
pub const ENOLINK: ErrnoCode = ErrnoCode(uapi::ENOLINK);
pub const EADV: ErrnoCode = ErrnoCode(uapi::EADV);
pub const ESRMNT: ErrnoCode = ErrnoCode(uapi::ESRMNT);
pub const ECOMM: ErrnoCode = ErrnoCode(uapi::ECOMM);
pub const EPROTO: ErrnoCode = ErrnoCode(uapi::EPROTO);
pub const EMULTIHOP: ErrnoCode = ErrnoCode(uapi::EMULTIHOP);
pub const EDOTDOT: ErrnoCode = ErrnoCode(uapi::EDOTDOT);
pub const EBADMSG: ErrnoCode = ErrnoCode(uapi::EBADMSG);
pub const EOVERFLOW: ErrnoCode = ErrnoCode(uapi::EOVERFLOW);
pub const ENOTUNIQ: ErrnoCode = ErrnoCode(uapi::ENOTUNIQ);
pub const EBADFD: ErrnoCode = ErrnoCode(uapi::EBADFD);
pub const EREMCHG: ErrnoCode = ErrnoCode(uapi::EREMCHG);
pub const ELIBACC: ErrnoCode = ErrnoCode(uapi::ELIBACC);
pub const ELIBBAD: ErrnoCode = ErrnoCode(uapi::ELIBBAD);
pub const ELIBSCN: ErrnoCode = ErrnoCode(uapi::ELIBSCN);
pub const ELIBMAX: ErrnoCode = ErrnoCode(uapi::ELIBMAX);
pub const ELIBEXEC: ErrnoCode = ErrnoCode(uapi::ELIBEXEC);
pub const EILSEQ: ErrnoCode = ErrnoCode(uapi::EILSEQ);
pub const ERESTART: ErrnoCode = ErrnoCode(uapi::ERESTART);
pub const ESTRPIPE: ErrnoCode = ErrnoCode(uapi::ESTRPIPE);
pub const EUSERS: ErrnoCode = ErrnoCode(uapi::EUSERS);
pub const ENOTSOCK: ErrnoCode = ErrnoCode(uapi::ENOTSOCK);
pub const EDESTADDRREQ: ErrnoCode = ErrnoCode(uapi::EDESTADDRREQ);
pub const EMSGSIZE: ErrnoCode = ErrnoCode(uapi::EMSGSIZE);
pub const EPROTOTYPE: ErrnoCode = ErrnoCode(uapi::EPROTOTYPE);
pub const ENOPROTOOPT: ErrnoCode = ErrnoCode(uapi::ENOPROTOOPT);
pub const EPROTONOSUPPORT: ErrnoCode = ErrnoCode(uapi::EPROTONOSUPPORT);
pub const ESOCKTNOSUPPORT: ErrnoCode = ErrnoCode(uapi::ESOCKTNOSUPPORT);
pub const EOPNOTSUPP: ErrnoCode = ErrnoCode(uapi::EOPNOTSUPP);
pub const EPFNOSUPPORT: ErrnoCode = ErrnoCode(uapi::EPFNOSUPPORT);
pub const EAFNOSUPPORT: ErrnoCode = ErrnoCode(uapi::EAFNOSUPPORT);
pub const EADDRINUSE: ErrnoCode = ErrnoCode(uapi::EADDRINUSE);
pub const EADDRNOTAVAIL: ErrnoCode = ErrnoCode(uapi::EADDRNOTAVAIL);
pub const ENETDOWN: ErrnoCode = ErrnoCode(uapi::ENETDOWN);
pub const ENETUNREACH: ErrnoCode = ErrnoCode(uapi::ENETUNREACH);
pub const ENETRESET: ErrnoCode = ErrnoCode(uapi::ENETRESET);
pub const ECONNABORTED: ErrnoCode = ErrnoCode(uapi::ECONNABORTED);
pub const ECONNRESET: ErrnoCode = ErrnoCode(uapi::ECONNRESET);
pub const ENOBUFS: ErrnoCode = ErrnoCode(uapi::ENOBUFS);
pub const EISCONN: ErrnoCode = ErrnoCode(uapi::EISCONN);
pub const ENOTCONN: ErrnoCode = ErrnoCode(uapi::ENOTCONN);
pub const ESHUTDOWN: ErrnoCode = ErrnoCode(uapi::ESHUTDOWN);
pub const ETOOMANYREFS: ErrnoCode = ErrnoCode(uapi::ETOOMANYREFS);
pub const ETIMEDOUT: ErrnoCode = ErrnoCode(uapi::ETIMEDOUT);
pub const ECONNREFUSED: ErrnoCode = ErrnoCode(uapi::ECONNREFUSED);
pub const EHOSTDOWN: ErrnoCode = ErrnoCode(uapi::EHOSTDOWN);
pub const EHOSTUNREACH: ErrnoCode = ErrnoCode(uapi::EHOSTUNREACH);
pub const EALREADY: ErrnoCode = ErrnoCode(uapi::EALREADY);
pub const EINPROGRESS: ErrnoCode = ErrnoCode(uapi::EINPROGRESS);
pub const ESTALE: ErrnoCode = ErrnoCode(uapi::ESTALE);
pub const EUCLEAN: ErrnoCode = ErrnoCode(uapi::EUCLEAN);
pub const ENOTNAM: ErrnoCode = ErrnoCode(uapi::ENOTNAM);
pub const ENAVAIL: ErrnoCode = ErrnoCode(uapi::ENAVAIL);
pub const EISNAM: ErrnoCode = ErrnoCode(uapi::EISNAM);
pub const EREMOTEIO: ErrnoCode = ErrnoCode(uapi::EREMOTEIO);
pub const EDQUOT: ErrnoCode = ErrnoCode(uapi::EDQUOT);
pub const ENOMEDIUM: ErrnoCode = ErrnoCode(uapi::ENOMEDIUM);
pub const EMEDIUMTYPE: ErrnoCode = ErrnoCode(uapi::EMEDIUMTYPE);
pub const ECANCELED: ErrnoCode = ErrnoCode(uapi::ECANCELED);
pub const ENOKEY: ErrnoCode = ErrnoCode(uapi::ENOKEY);
pub const EKEYEXPIRED: ErrnoCode = ErrnoCode(uapi::EKEYEXPIRED);
pub const EKEYREVOKED: ErrnoCode = ErrnoCode(uapi::EKEYREVOKED);
pub const EKEYREJECTED: ErrnoCode = ErrnoCode(uapi::EKEYREJECTED);
pub const EOWNERDEAD: ErrnoCode = ErrnoCode(uapi::EOWNERDEAD);
pub const ENOTRECOVERABLE: ErrnoCode = ErrnoCode(uapi::ENOTRECOVERABLE);
pub const ERFKILL: ErrnoCode = ErrnoCode(uapi::ERFKILL);
pub const EHWPOISON: ErrnoCode = ErrnoCode(uapi::EHWPOISON);

// ENOTSUP is a different error in posix, but has the same value as EOPNOTSUPP in linux.
pub const ENOTSUP: ErrnoCode = EOPNOTSUPP;

/// `errno` returns an `Errno` struct tagged with the current file name and line number.
///
/// Use `error!` instead if you want the `Errno` to be wrapped in an `Err`.
macro_rules! errno {
    ($err:ident) => {
        Errno::new(
            crate::types::errno::$err,
            stringify!($err),
            None,
            crate::types::errno::ErrnoSource { file: file!().to_string(), line: line!() },
        )
    };
    ($err:ident, $context:expr) => {
        Errno::new(
            crate::types::errno::$err,
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

/// `error_from_code` returns a `Err` containing an `Errno` struct with the given error code and is
/// tagged with the current file name and line number.
macro_rules! error_from_code {
    ($err:expr) => {{
        let errno = ErrnoCode::from_error_code($err);
        Err(Errno::new(
            errno,
            stringify!($err),
            None,
            crate::types::errno::ErrnoSource { file: file!().to_string(), line: line!() },
        ))
    }};
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
        use fuchsia_zircon as zx;
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

// Fuchsia error codes should match Linux.
const_assert_eq!(syncio::zxio::EPERM, uapi::EPERM);
const_assert_eq!(syncio::zxio::ENOENT, uapi::ENOENT);
const_assert_eq!(syncio::zxio::ESRCH, uapi::ESRCH);
const_assert_eq!(syncio::zxio::EINTR, uapi::EINTR);
const_assert_eq!(syncio::zxio::EIO, uapi::EIO);
const_assert_eq!(syncio::zxio::ENXIO, uapi::ENXIO);
const_assert_eq!(syncio::zxio::ENOEXEC, uapi::ENOEXEC);
const_assert_eq!(syncio::zxio::EBADF, uapi::EBADF);
const_assert_eq!(syncio::zxio::ECHILD, uapi::ECHILD);
const_assert_eq!(syncio::zxio::EAGAIN, uapi::EAGAIN);
const_assert_eq!(syncio::zxio::ENOMEM, uapi::ENOMEM);
const_assert_eq!(syncio::zxio::EACCES, uapi::EACCES);
const_assert_eq!(syncio::zxio::EFAULT, uapi::EFAULT);
const_assert_eq!(syncio::zxio::ENOTBLK, uapi::ENOTBLK);
const_assert_eq!(syncio::zxio::EBUSY, uapi::EBUSY);
const_assert_eq!(syncio::zxio::EEXIST, uapi::EEXIST);
const_assert_eq!(syncio::zxio::EXDEV, uapi::EXDEV);
const_assert_eq!(syncio::zxio::ENODEV, uapi::ENODEV);
const_assert_eq!(syncio::zxio::ENOTDIR, uapi::ENOTDIR);
const_assert_eq!(syncio::zxio::EISDIR, uapi::EISDIR);
const_assert_eq!(syncio::zxio::EINVAL, uapi::EINVAL);
const_assert_eq!(syncio::zxio::ENFILE, uapi::ENFILE);
const_assert_eq!(syncio::zxio::EMFILE, uapi::EMFILE);
const_assert_eq!(syncio::zxio::ENOTTY, uapi::ENOTTY);
const_assert_eq!(syncio::zxio::ETXTBSY, uapi::ETXTBSY);
const_assert_eq!(syncio::zxio::EFBIG, uapi::EFBIG);
const_assert_eq!(syncio::zxio::ENOSPC, uapi::ENOSPC);
const_assert_eq!(syncio::zxio::ESPIPE, uapi::ESPIPE);
const_assert_eq!(syncio::zxio::EROFS, uapi::EROFS);
const_assert_eq!(syncio::zxio::EMLINK, uapi::EMLINK);
const_assert_eq!(syncio::zxio::EPIPE, uapi::EPIPE);
const_assert_eq!(syncio::zxio::EDOM, uapi::EDOM);
const_assert_eq!(syncio::zxio::ERANGE, uapi::ERANGE);
const_assert_eq!(syncio::zxio::EDEADLK, uapi::EDEADLK);
const_assert_eq!(syncio::zxio::ENAMETOOLONG, uapi::ENAMETOOLONG);
const_assert_eq!(syncio::zxio::ENOLCK, uapi::ENOLCK);
const_assert_eq!(syncio::zxio::ENOSYS, uapi::ENOSYS);
const_assert_eq!(syncio::zxio::ENOTEMPTY, uapi::ENOTEMPTY);
const_assert_eq!(syncio::zxio::ELOOP, uapi::ELOOP);
const_assert_eq!(syncio::zxio::ENOMSG, uapi::ENOMSG);
const_assert_eq!(syncio::zxio::EIDRM, uapi::EIDRM);
const_assert_eq!(syncio::zxio::ECHRNG, uapi::ECHRNG);
const_assert_eq!(syncio::zxio::ELNRNG, uapi::ELNRNG);
const_assert_eq!(syncio::zxio::EUNATCH, uapi::EUNATCH);
const_assert_eq!(syncio::zxio::ENOCSI, uapi::ENOCSI);
const_assert_eq!(syncio::zxio::EBADE, uapi::EBADE);
const_assert_eq!(syncio::zxio::EBADR, uapi::EBADR);
const_assert_eq!(syncio::zxio::EXFULL, uapi::EXFULL);
const_assert_eq!(syncio::zxio::ENOANO, uapi::ENOANO);
const_assert_eq!(syncio::zxio::EBADRQC, uapi::EBADRQC);
const_assert_eq!(syncio::zxio::EBADSLT, uapi::EBADSLT);
const_assert_eq!(syncio::zxio::EBFONT, uapi::EBFONT);
const_assert_eq!(syncio::zxio::ENOSTR, uapi::ENOSTR);
const_assert_eq!(syncio::zxio::ENODATA, uapi::ENODATA);
const_assert_eq!(syncio::zxio::ETIME, uapi::ETIME);
const_assert_eq!(syncio::zxio::ENOSR, uapi::ENOSR);
const_assert_eq!(syncio::zxio::ENONET, uapi::ENONET);
const_assert_eq!(syncio::zxio::ENOPKG, uapi::ENOPKG);
const_assert_eq!(syncio::zxio::EREMOTE, uapi::EREMOTE);
const_assert_eq!(syncio::zxio::ENOLINK, uapi::ENOLINK);
const_assert_eq!(syncio::zxio::EADV, uapi::EADV);
const_assert_eq!(syncio::zxio::ESRMNT, uapi::ESRMNT);
const_assert_eq!(syncio::zxio::ECOMM, uapi::ECOMM);
const_assert_eq!(syncio::zxio::EPROTO, uapi::EPROTO);
const_assert_eq!(syncio::zxio::EMULTIHOP, uapi::EMULTIHOP);
const_assert_eq!(syncio::zxio::EDOTDOT, uapi::EDOTDOT);
const_assert_eq!(syncio::zxio::EBADMSG, uapi::EBADMSG);
const_assert_eq!(syncio::zxio::EOVERFLOW, uapi::EOVERFLOW);
const_assert_eq!(syncio::zxio::ENOTUNIQ, uapi::ENOTUNIQ);
const_assert_eq!(syncio::zxio::EBADFD, uapi::EBADFD);
const_assert_eq!(syncio::zxio::EREMCHG, uapi::EREMCHG);
const_assert_eq!(syncio::zxio::ELIBACC, uapi::ELIBACC);
const_assert_eq!(syncio::zxio::ELIBBAD, uapi::ELIBBAD);
const_assert_eq!(syncio::zxio::ELIBSCN, uapi::ELIBSCN);
const_assert_eq!(syncio::zxio::ELIBMAX, uapi::ELIBMAX);
const_assert_eq!(syncio::zxio::ELIBEXEC, uapi::ELIBEXEC);
const_assert_eq!(syncio::zxio::EILSEQ, uapi::EILSEQ);
const_assert_eq!(syncio::zxio::ERESTART, uapi::ERESTART);
const_assert_eq!(syncio::zxio::ESTRPIPE, uapi::ESTRPIPE);
const_assert_eq!(syncio::zxio::EUSERS, uapi::EUSERS);
const_assert_eq!(syncio::zxio::ENOTSOCK, uapi::ENOTSOCK);
const_assert_eq!(syncio::zxio::EDESTADDRREQ, uapi::EDESTADDRREQ);
const_assert_eq!(syncio::zxio::EMSGSIZE, uapi::EMSGSIZE);
const_assert_eq!(syncio::zxio::EPROTOTYPE, uapi::EPROTOTYPE);
const_assert_eq!(syncio::zxio::ENOPROTOOPT, uapi::ENOPROTOOPT);
const_assert_eq!(syncio::zxio::EPROTONOSUPPORT, uapi::EPROTONOSUPPORT);
const_assert_eq!(syncio::zxio::ESOCKTNOSUPPORT, uapi::ESOCKTNOSUPPORT);
const_assert_eq!(syncio::zxio::EOPNOTSUPP, uapi::EOPNOTSUPP);
const_assert_eq!(syncio::zxio::EPFNOSUPPORT, uapi::EPFNOSUPPORT);
const_assert_eq!(syncio::zxio::EAFNOSUPPORT, uapi::EAFNOSUPPORT);
const_assert_eq!(syncio::zxio::EADDRINUSE, uapi::EADDRINUSE);
const_assert_eq!(syncio::zxio::EADDRNOTAVAIL, uapi::EADDRNOTAVAIL);
const_assert_eq!(syncio::zxio::ENETDOWN, uapi::ENETDOWN);
const_assert_eq!(syncio::zxio::ENETUNREACH, uapi::ENETUNREACH);
const_assert_eq!(syncio::zxio::ENETRESET, uapi::ENETRESET);
const_assert_eq!(syncio::zxio::ECONNABORTED, uapi::ECONNABORTED);
const_assert_eq!(syncio::zxio::ECONNRESET, uapi::ECONNRESET);
const_assert_eq!(syncio::zxio::ENOBUFS, uapi::ENOBUFS);
const_assert_eq!(syncio::zxio::EISCONN, uapi::EISCONN);
const_assert_eq!(syncio::zxio::ENOTCONN, uapi::ENOTCONN);
const_assert_eq!(syncio::zxio::ESHUTDOWN, uapi::ESHUTDOWN);
const_assert_eq!(syncio::zxio::ETOOMANYREFS, uapi::ETOOMANYREFS);
const_assert_eq!(syncio::zxio::ETIMEDOUT, uapi::ETIMEDOUT);
const_assert_eq!(syncio::zxio::ECONNREFUSED, uapi::ECONNREFUSED);
const_assert_eq!(syncio::zxio::EHOSTDOWN, uapi::EHOSTDOWN);
const_assert_eq!(syncio::zxio::EHOSTUNREACH, uapi::EHOSTUNREACH);
const_assert_eq!(syncio::zxio::EALREADY, uapi::EALREADY);
const_assert_eq!(syncio::zxio::EINPROGRESS, uapi::EINPROGRESS);
const_assert_eq!(syncio::zxio::ESTALE, uapi::ESTALE);
const_assert_eq!(syncio::zxio::EUCLEAN, uapi::EUCLEAN);
const_assert_eq!(syncio::zxio::ENOTNAM, uapi::ENOTNAM);
const_assert_eq!(syncio::zxio::ENAVAIL, uapi::ENAVAIL);
const_assert_eq!(syncio::zxio::EISNAM, uapi::EISNAM);
const_assert_eq!(syncio::zxio::EREMOTEIO, uapi::EREMOTEIO);
const_assert_eq!(syncio::zxio::EDQUOT, uapi::EDQUOT);
const_assert_eq!(syncio::zxio::ENOMEDIUM, uapi::ENOMEDIUM);
const_assert_eq!(syncio::zxio::EMEDIUMTYPE, uapi::EMEDIUMTYPE);
const_assert_eq!(syncio::zxio::ECANCELED, uapi::ECANCELED);
const_assert_eq!(syncio::zxio::ENOKEY, uapi::ENOKEY);
const_assert_eq!(syncio::zxio::EKEYEXPIRED, uapi::EKEYEXPIRED);
const_assert_eq!(syncio::zxio::EKEYREVOKED, uapi::EKEYREVOKED);
const_assert_eq!(syncio::zxio::EKEYREJECTED, uapi::EKEYREJECTED);
const_assert_eq!(syncio::zxio::EOWNERDEAD, uapi::EOWNERDEAD);
const_assert_eq!(syncio::zxio::ENOTRECOVERABLE, uapi::ENOTRECOVERABLE);
const_assert_eq!(syncio::zxio::ERFKILL, uapi::ERFKILL);
const_assert_eq!(syncio::zxio::EHWPOISON, uapi::EHWPOISON);

// Public re-export of macros allows them to be used like regular rust items.
pub(crate) use errno;
pub(crate) use error;
pub(crate) use error_from_code;
pub(crate) use from_status_like_fdio;
