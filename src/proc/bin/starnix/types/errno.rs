// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(dead_code)]

use std::fmt;

use crate::types::uapi;

#[derive(Debug)]
pub struct Errno {
    value: u32,
    name: &'static str,
    file: Option<String>,
    line: Option<u32>,
    context: Option<String>,
}

impl Errno {
    pub fn new(
        value: i32,
        name: &'static str,
        file: String,
        line: u32,
        context: Option<String>,
    ) -> Errno {
        Errno { value: value as u32, name, file: Some(file), line: Some(line), context }
    }

    pub fn value(&self) -> i32 {
        self.value as i32
    }
}

impl PartialEq for Errno {
    fn eq(&self, other: &Self) -> bool {
        self.value == other.value
    }
}

impl Eq for Errno {}

impl std::error::Error for Errno {}

impl fmt::Display for Errno {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match (self.file.as_ref(), self.line.as_ref()) {
            (Some(file), Some(line)) => {
                write!(
                    f,
                    "error {}: {} from {:?}:{:?}, context: {}",
                    self.value,
                    self.name,
                    file,
                    line,
                    self.context.as_ref().unwrap_or(&"None".to_string())
                )
            }
            _ => write!(f, "error {}: {} from unknown location", self.value, self.name),
        }
    }
}

pub const EPERM: Errno =
    Errno { value: uapi::EPERM, name: "EPERM", file: None, line: None, context: None };
pub const ENOENT: Errno =
    Errno { value: uapi::ENOENT, name: "ENOENT", file: None, line: None, context: None };
pub const ESRCH: Errno =
    Errno { value: uapi::ESRCH, name: "ESRCH", file: None, line: None, context: None };
pub const EINTR: Errno =
    Errno { value: uapi::EINTR, name: "EINTR", file: None, line: None, context: None };
pub const EIO: Errno =
    Errno { value: uapi::EIO, name: "EIO", file: None, line: None, context: None };
pub const ENXIO: Errno =
    Errno { value: uapi::ENXIO, name: "ENXIO", file: None, line: None, context: None };
pub const E2BIG: Errno =
    Errno { value: uapi::E2BIG, name: "E2BIG", file: None, line: None, context: None };
pub const ENOEXEC: Errno =
    Errno { value: uapi::ENOEXEC, name: "ENOEXEC", file: None, line: None, context: None };
pub const EBADF: Errno =
    Errno { value: uapi::EBADF, name: "EBADF", file: None, line: None, context: None };
pub const ECHILD: Errno =
    Errno { value: uapi::ECHILD, name: "ECHILD", file: None, line: None, context: None };
pub const EAGAIN: Errno =
    Errno { value: uapi::EAGAIN, name: "EAGAIN", file: None, line: None, context: None };
pub const ENOMEM: Errno =
    Errno { value: uapi::ENOMEM, name: "ENOMEM", file: None, line: None, context: None };
pub const EACCES: Errno =
    Errno { value: uapi::EACCES, name: "EACCES", file: None, line: None, context: None };
pub const EFAULT: Errno =
    Errno { value: uapi::EFAULT, name: "EFAULT", file: None, line: None, context: None };
pub const ENOTBLK: Errno =
    Errno { value: uapi::ENOTBLK, name: "ENOTBLK", file: None, line: None, context: None };
pub const EBUSY: Errno =
    Errno { value: uapi::EBUSY, name: "EBUSY", file: None, line: None, context: None };
pub const EEXIST: Errno =
    Errno { value: uapi::EEXIST, name: "EEXIST", file: None, line: None, context: None };
pub const EXDEV: Errno =
    Errno { value: uapi::EXDEV, name: "EXDEV", file: None, line: None, context: None };
pub const ENODEV: Errno =
    Errno { value: uapi::ENODEV, name: "ENODEV", file: None, line: None, context: None };
pub const ENOTDIR: Errno =
    Errno { value: uapi::ENOTDIR, name: "ENOTDIR", file: None, line: None, context: None };
pub const EISDIR: Errno =
    Errno { value: uapi::EISDIR, name: "EISDIR", file: None, line: None, context: None };
pub const EINVAL: Errno =
    Errno { value: uapi::EINVAL, name: "errno!(EINVAL)", file: None, line: None, context: None };
pub const ENFILE: Errno =
    Errno { value: uapi::ENFILE, name: "ENFILE", file: None, line: None, context: None };
pub const EMFILE: Errno =
    Errno { value: uapi::EMFILE, name: "EMFILE", file: None, line: None, context: None };
pub const ENOTTY: Errno =
    Errno { value: uapi::ENOTTY, name: "ENOTTY", file: None, line: None, context: None };
pub const ETXTBSY: Errno =
    Errno { value: uapi::ETXTBSY, name: "ETXTBSY", file: None, line: None, context: None };
pub const EFBIG: Errno =
    Errno { value: uapi::EFBIG, name: "EFBIG", file: None, line: None, context: None };
pub const ENOSPC: Errno =
    Errno { value: uapi::ENOSPC, name: "ENOSPC", file: None, line: None, context: None };
pub const ESPIPE: Errno =
    Errno { value: uapi::ESPIPE, name: "ESPIPE", file: None, line: None, context: None };
pub const EROFS: Errno =
    Errno { value: uapi::EROFS, name: "EROFS", file: None, line: None, context: None };
pub const EMLINK: Errno =
    Errno { value: uapi::EMLINK, name: "EMLINK", file: None, line: None, context: None };
pub const EPIPE: Errno =
    Errno { value: uapi::EPIPE, name: "EPIPE", file: None, line: None, context: None };
pub const EDOM: Errno =
    Errno { value: uapi::EDOM, name: "EDOM", file: None, line: None, context: None };
pub const ERANGE: Errno =
    Errno { value: uapi::ERANGE, name: "ERANGE", file: None, line: None, context: None };
pub const EDEADLK: Errno =
    Errno { value: uapi::EDEADLK, name: "EDEADLK", file: None, line: None, context: None };
pub const ENAMETOOLONG: Errno = Errno {
    value: uapi::ENAMETOOLONG,
    name: "ENAMETOOLONG",
    file: None,
    line: None,
    context: None,
};
pub const ENOLCK: Errno =
    Errno { value: uapi::ENOLCK, name: "ENOLCK", file: None, line: None, context: None };
pub const ENOSYS: Errno =
    Errno { value: uapi::ENOSYS, name: "ENOSYS", file: None, line: None, context: None };
pub const ENOTEMPTY: Errno =
    Errno { value: uapi::ENOTEMPTY, name: "ENOTEMPTY", file: None, line: None, context: None };
pub const ELOOP: Errno =
    Errno { value: uapi::ELOOP, name: "ELOOP", file: None, line: None, context: None };
pub const EWOULDBLOCK: Errno =
    Errno { value: uapi::EWOULDBLOCK, name: "EWOULDBLOCK", file: None, line: None, context: None };
pub const ENOMSG: Errno =
    Errno { value: uapi::ENOMSG, name: "ENOMSG", file: None, line: None, context: None };
pub const EIDRM: Errno =
    Errno { value: uapi::EIDRM, name: "EIDRM", file: None, line: None, context: None };
pub const ECHRNG: Errno =
    Errno { value: uapi::ECHRNG, name: "ECHRNG", file: None, line: None, context: None };
pub const EL2NSYNC: Errno =
    Errno { value: uapi::EL2NSYNC, name: "EL2NSYNC", file: None, line: None, context: None };
pub const EL3HLT: Errno =
    Errno { value: uapi::EL3HLT, name: "EL3HLT", file: None, line: None, context: None };
pub const EL3RST: Errno =
    Errno { value: uapi::EL3RST, name: "EL3RST", file: None, line: None, context: None };
pub const ELNRNG: Errno =
    Errno { value: uapi::ELNRNG, name: "ELNRNG", file: None, line: None, context: None };
pub const EUNATCH: Errno =
    Errno { value: uapi::EUNATCH, name: "EUNATCH", file: None, line: None, context: None };
pub const ENOCSI: Errno =
    Errno { value: uapi::ENOCSI, name: "ENOCSI", file: None, line: None, context: None };
pub const EL2HLT: Errno =
    Errno { value: uapi::EL2HLT, name: "EL2HLT", file: None, line: None, context: None };
pub const EBADE: Errno =
    Errno { value: uapi::EBADE, name: "EBADE", file: None, line: None, context: None };
pub const EBADR: Errno =
    Errno { value: uapi::EBADR, name: "EBADR", file: None, line: None, context: None };
pub const EXFULL: Errno =
    Errno { value: uapi::EXFULL, name: "EXFULL", file: None, line: None, context: None };
pub const ENOANO: Errno =
    Errno { value: uapi::ENOANO, name: "ENOANO", file: None, line: None, context: None };
pub const EBADRQC: Errno =
    Errno { value: uapi::EBADRQC, name: "EBADRQC", file: None, line: None, context: None };
pub const EBADSLT: Errno =
    Errno { value: uapi::EBADSLT, name: "EBADSLT", file: None, line: None, context: None };
pub const EDEADLOCK: Errno =
    Errno { value: uapi::EDEADLOCK, name: "EDEADLOCK", file: None, line: None, context: None };
pub const EBFONT: Errno =
    Errno { value: uapi::EBFONT, name: "EBFONT", file: None, line: None, context: None };
pub const ENOSTR: Errno =
    Errno { value: uapi::ENOSTR, name: "ENOSTR", file: None, line: None, context: None };
pub const ENODATA: Errno =
    Errno { value: uapi::ENODATA, name: "ENODATA", file: None, line: None, context: None };
pub const ETIME: Errno =
    Errno { value: uapi::ETIME, name: "ETIME", file: None, line: None, context: None };
pub const ENOSR: Errno =
    Errno { value: uapi::ENOSR, name: "ENOSR", file: None, line: None, context: None };
pub const ENONET: Errno =
    Errno { value: uapi::ENONET, name: "ENONET", file: None, line: None, context: None };
pub const ENOPKG: Errno =
    Errno { value: uapi::ENOPKG, name: "ENOPKG", file: None, line: None, context: None };
pub const EREMOTE: Errno =
    Errno { value: uapi::EREMOTE, name: "EREMOTE", file: None, line: None, context: None };
pub const ENOLINK: Errno =
    Errno { value: uapi::ENOLINK, name: "ENOLINK", file: None, line: None, context: None };
pub const EADV: Errno =
    Errno { value: uapi::EADV, name: "EADV", file: None, line: None, context: None };
pub const ESRMNT: Errno =
    Errno { value: uapi::ESRMNT, name: "ESRMNT", file: None, line: None, context: None };
pub const ECOMM: Errno =
    Errno { value: uapi::ECOMM, name: "ECOMM", file: None, line: None, context: None };
pub const EPROTO: Errno =
    Errno { value: uapi::EPROTO, name: "EPROTO", file: None, line: None, context: None };
pub const EMULTIHOP: Errno =
    Errno { value: uapi::EMULTIHOP, name: "EMULTIHOP", file: None, line: None, context: None };
pub const EDOTDOT: Errno =
    Errno { value: uapi::EDOTDOT, name: "EDOTDOT", file: None, line: None, context: None };
pub const EBADMSG: Errno =
    Errno { value: uapi::EBADMSG, name: "EBADMSG", file: None, line: None, context: None };
pub const EOVERFLOW: Errno =
    Errno { value: uapi::EOVERFLOW, name: "EOVERFLOW", file: None, line: None, context: None };
pub const ENOTUNIQ: Errno =
    Errno { value: uapi::ENOTUNIQ, name: "ENOTUNIQ", file: None, line: None, context: None };
pub const EBADFD: Errno =
    Errno { value: uapi::EBADFD, name: "EBADFD", file: None, line: None, context: None };
pub const EREMCHG: Errno =
    Errno { value: uapi::EREMCHG, name: "EREMCHG", file: None, line: None, context: None };
pub const ELIBACC: Errno =
    Errno { value: uapi::ELIBACC, name: "ELIBACC", file: None, line: None, context: None };
pub const ELIBBAD: Errno =
    Errno { value: uapi::ELIBBAD, name: "ELIBBAD", file: None, line: None, context: None };
pub const ELIBSCN: Errno =
    Errno { value: uapi::ELIBSCN, name: "ELIBSCN", file: None, line: None, context: None };
pub const ELIBMAX: Errno =
    Errno { value: uapi::ELIBMAX, name: "ELIBMAX", file: None, line: None, context: None };
pub const ELIBEXEC: Errno =
    Errno { value: uapi::ELIBEXEC, name: "ELIBEXEC", file: None, line: None, context: None };
pub const EILSEQ: Errno =
    Errno { value: uapi::EILSEQ, name: "EILSEQ", file: None, line: None, context: None };
pub const ERESTART: Errno =
    Errno { value: uapi::ERESTART, name: "ERESTART", file: None, line: None, context: None };
pub const ESTRPIPE: Errno =
    Errno { value: uapi::ESTRPIPE, name: "ESTRPIPE", file: None, line: None, context: None };
pub const EUSERS: Errno =
    Errno { value: uapi::EUSERS, name: "EUSERS", file: None, line: None, context: None };
pub const ENOTSOCK: Errno =
    Errno { value: uapi::ENOTSOCK, name: "ENOTSOCK", file: None, line: None, context: None };
pub const EDESTADDRREQ: Errno = Errno {
    value: uapi::EDESTADDRREQ,
    name: "EDESTADDRREQ",
    file: None,
    line: None,
    context: None,
};
pub const EMSGSIZE: Errno =
    Errno { value: uapi::EMSGSIZE, name: "EMSGSIZE", file: None, line: None, context: None };
pub const EPROTOTYPE: Errno =
    Errno { value: uapi::EPROTOTYPE, name: "EPROTOTYPE", file: None, line: None, context: None };
pub const ENOPROTOOPT: Errno =
    Errno { value: uapi::ENOPROTOOPT, name: "ENOPROTOOPT", file: None, line: None, context: None };
pub const EPROTONOSUPPORT: Errno = Errno {
    value: uapi::EPROTONOSUPPORT,
    name: "EPROTONOSUPPORT",
    file: None,
    line: None,
    context: None,
};
pub const ESOCKTNOSUPPORT: Errno = Errno {
    value: uapi::ESOCKTNOSUPPORT,
    name: "ESOCKTNOSUPPORT",
    file: None,
    line: None,
    context: None,
};
pub const EOPNOTSUPP: Errno =
    Errno { value: uapi::EOPNOTSUPP, name: "EOPNOTSUPP", file: None, line: None, context: None };
pub const EPFNOSUPPORT: Errno = Errno {
    value: uapi::EPFNOSUPPORT,
    name: "EPFNOSUPPORT",
    file: None,
    line: None,
    context: None,
};
pub const EAFNOSUPPORT: Errno = Errno {
    value: uapi::EAFNOSUPPORT,
    name: "EAFNOSUPPORT",
    file: None,
    line: None,
    context: None,
};
pub const EADDRINUSE: Errno =
    Errno { value: uapi::EADDRINUSE, name: "EADDRINUSE", file: None, line: None, context: None };
pub const EADDRNOTAVAIL: Errno = Errno {
    value: uapi::EADDRNOTAVAIL,
    name: "EADDRNOTAVAIL",
    file: None,
    line: None,
    context: None,
};
pub const ENETDOWN: Errno =
    Errno { value: uapi::ENETDOWN, name: "ENETDOWN", file: None, line: None, context: None };
pub const ENETUNREACH: Errno =
    Errno { value: uapi::ENETUNREACH, name: "ENETUNREACH", file: None, line: None, context: None };
pub const ENETRESET: Errno =
    Errno { value: uapi::ENETRESET, name: "ENETRESET", file: None, line: None, context: None };
pub const ECONNABORTED: Errno = Errno {
    value: uapi::ECONNABORTED,
    name: "ECONNABORTED",
    file: None,
    line: None,
    context: None,
};
pub const ECONNRESET: Errno =
    Errno { value: uapi::ECONNRESET, name: "ECONNRESET", file: None, line: None, context: None };
pub const ENOBUFS: Errno =
    Errno { value: uapi::ENOBUFS, name: "ENOBUFS", file: None, line: None, context: None };
pub const EISCONN: Errno =
    Errno { value: uapi::EISCONN, name: "EISCONN", file: None, line: None, context: None };
pub const ENOTCONN: Errno =
    Errno { value: uapi::ENOTCONN, name: "ENOTCONN", file: None, line: None, context: None };
pub const ESHUTDOWN: Errno =
    Errno { value: uapi::ESHUTDOWN, name: "ESHUTDOWN", file: None, line: None, context: None };
pub const ETOOMANYREFS: Errno = Errno {
    value: uapi::ETOOMANYREFS,
    name: "ETOOMANYREFS",
    file: None,
    line: None,
    context: None,
};
pub const ETIMEDOUT: Errno =
    Errno { value: uapi::ETIMEDOUT, name: "ETIMEDOUT", file: None, line: None, context: None };
pub const ECONNREFUSED: Errno = Errno {
    value: uapi::ECONNREFUSED,
    name: "ECONNREFUSED",
    file: None,
    line: None,
    context: None,
};
pub const EHOSTDOWN: Errno =
    Errno { value: uapi::EHOSTDOWN, name: "EHOSTDOWN", file: None, line: None, context: None };
pub const EHOSTUNREACH: Errno = Errno {
    value: uapi::EHOSTUNREACH,
    name: "EHOSTUNREACH",
    file: None,
    line: None,
    context: None,
};
pub const EALREADY: Errno =
    Errno { value: uapi::EALREADY, name: "EALREADY", file: None, line: None, context: None };
pub const EINPROGRESS: Errno =
    Errno { value: uapi::EINPROGRESS, name: "EINPROGRESS", file: None, line: None, context: None };
pub const ESTALE: Errno =
    Errno { value: uapi::ESTALE, name: "ESTALE", file: None, line: None, context: None };
pub const EUCLEAN: Errno =
    Errno { value: uapi::EUCLEAN, name: "EUCLEAN", file: None, line: None, context: None };
pub const ENOTNAM: Errno =
    Errno { value: uapi::ENOTNAM, name: "ENOTNAM", file: None, line: None, context: None };
pub const ENAVAIL: Errno =
    Errno { value: uapi::ENAVAIL, name: "ENAVAIL", file: None, line: None, context: None };
pub const EISNAM: Errno =
    Errno { value: uapi::EISNAM, name: "EISNAM", file: None, line: None, context: None };
pub const EREMOTEIO: Errno =
    Errno { value: uapi::EREMOTEIO, name: "EREMOTEIO", file: None, line: None, context: None };
pub const EDQUOT: Errno =
    Errno { value: uapi::EDQUOT, name: "EDQUOT", file: None, line: None, context: None };
pub const ENOMEDIUM: Errno =
    Errno { value: uapi::ENOMEDIUM, name: "ENOMEDIUM", file: None, line: None, context: None };
pub const EMEDIUMTYPE: Errno =
    Errno { value: uapi::EMEDIUMTYPE, name: "EMEDIUMTYPE", file: None, line: None, context: None };
pub const ECANCELED: Errno =
    Errno { value: uapi::ECANCELED, name: "ECANCELED", file: None, line: None, context: None };
pub const ENOKEY: Errno =
    Errno { value: uapi::ENOKEY, name: "ENOKEY", file: None, line: None, context: None };
pub const EKEYEXPIRED: Errno =
    Errno { value: uapi::EKEYEXPIRED, name: "EKEYEXPIRED", file: None, line: None, context: None };
pub const EKEYREVOKED: Errno =
    Errno { value: uapi::EKEYREVOKED, name: "EKEYREVOKED", file: None, line: None, context: None };
pub const EKEYREJECTED: Errno = Errno {
    value: uapi::EKEYREJECTED,
    name: "EKEYREJECTED",
    file: None,
    line: None,
    context: None,
};
pub const EOWNERDEAD: Errno =
    Errno { value: uapi::EOWNERDEAD, name: "EOWNERDEAD", file: None, line: None, context: None };
pub const ENOTRECOVERABLE: Errno = Errno {
    value: uapi::ENOTRECOVERABLE,
    name: "ENOTRECOVERABLE",
    file: None,
    line: None,
    context: None,
};
pub const ERFKILL: Errno =
    Errno { value: uapi::ERFKILL, name: "ERFKILL", file: None, line: None, context: None };
pub const EHWPOISON: Errno =
    Errno { value: uapi::EHWPOISON, name: "EHWPOISON", file: None, line: None, context: None };

// ENOTSUP is a different error in posix, but has the same value as EOPNOTSUPP in linux.
pub const ENOTSUP: Errno = EOPNOTSUPP;

/// `error` returns a `Err` containing an `Errno` struct tagged with the current file name and line
/// number.
///
/// Use `errno!` instead if you want an unwrapped, but still tagged, `Errno`.
macro_rules! error {
    ($err:ident) => {
        Err(Errno::new($err.value(), stringify!($err), file!().to_string(), line!(), None))
    };
    ($err:ident, $context:expr) => {
        Err(Errno::new(
            $err.value(),
            stringify!($err),
            file!().to_string(),
            line!(),
            Some($context.to_string()),
        ))
    };
}

/// `errno` returns an `Errno` struct tagged with the current file name and line number.
///
/// Use `error!` instead if you want the `Errno` to be wrapped in an `Err`.
macro_rules! errno {
    ($err:ident) => {
        Errno::new($err.value(), stringify!($err), file!().to_string(), line!(), None)
    };
    ($err:ident, $context:expr) => {
        Errno::new(
            $err.value(),
            stringify!($err),
            file!().to_string(),
            line!(),
            Some($context.to_string()),
        )
    };
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
