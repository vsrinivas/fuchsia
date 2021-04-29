// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(dead_code)]
#![allow(non_camel_case_types)]
#![allow(non_upper_case_globals)]

use fuchsia_zircon as zx;
use paste::paste;
use std::fmt;
use zerocopy::{AsBytes, FromBytes};

use crate::fs::FileDescriptor;
pub use crate::user_address::*;

#[cfg(target_arch = "x86_64")]
use linux_uapi::x86_64 as uapi;
pub use uapi::*;

pub type dev_t = uapi::__kernel_old_dev_t;
pub type gid_t = uapi::__kernel_gid_t;
pub type ino_t = uapi::__kernel_ino_t;
pub type mode_t = uapi::__kernel_mode_t;
pub type off_t = uapi::__kernel_off_t;
pub type pid_t = uapi::__kernel_pid_t;
pub type uid_t = uapi::__kernel_uid_t;

#[derive(Debug, Eq, PartialEq)]
pub struct Errno(u32);

impl Errno {
    pub fn value(&self) -> i32 {
        self.0 as i32
    }
}
impl fmt::Display for Errno {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        // TODO(tbodt): Return the string name of the error (would be nice to be able to do this
        // without typing them all twice.)
        write!(f, "error {}", self.0)
    }
}

pub const EPERM: Errno = Errno(uapi::EPERM);
pub const ENOENT: Errno = Errno(uapi::ENOENT);
pub const ESRCH: Errno = Errno(uapi::ESRCH);
pub const EINTR: Errno = Errno(uapi::EINTR);
pub const EIO: Errno = Errno(uapi::EIO);
pub const ENXIO: Errno = Errno(uapi::ENXIO);
pub const E2BIG: Errno = Errno(uapi::E2BIG);
pub const ENOEXEC: Errno = Errno(uapi::ENOEXEC);
pub const EBADF: Errno = Errno(uapi::EBADF);
pub const ECHILD: Errno = Errno(uapi::ECHILD);
pub const EAGAIN: Errno = Errno(uapi::EAGAIN);
pub const ENOMEM: Errno = Errno(uapi::ENOMEM);
pub const EACCES: Errno = Errno(uapi::EACCES);
pub const EFAULT: Errno = Errno(uapi::EFAULT);
pub const ENOTBLK: Errno = Errno(uapi::ENOTBLK);
pub const EBUSY: Errno = Errno(uapi::EBUSY);
pub const EEXIST: Errno = Errno(uapi::EEXIST);
pub const EXDEV: Errno = Errno(uapi::EXDEV);
pub const ENODEV: Errno = Errno(uapi::ENODEV);
pub const ENOTDIR: Errno = Errno(uapi::ENOTDIR);
pub const EISDIR: Errno = Errno(uapi::EISDIR);
pub const EINVAL: Errno = Errno(uapi::EINVAL);
pub const ENFILE: Errno = Errno(uapi::ENFILE);
pub const EMFILE: Errno = Errno(uapi::EMFILE);
pub const ENOTTY: Errno = Errno(uapi::ENOTTY);
pub const ETXTBSY: Errno = Errno(uapi::ETXTBSY);
pub const EFBIG: Errno = Errno(uapi::EFBIG);
pub const ENOSPC: Errno = Errno(uapi::ENOSPC);
pub const ESPIPE: Errno = Errno(uapi::ESPIPE);
pub const EROFS: Errno = Errno(uapi::EROFS);
pub const EMLINK: Errno = Errno(uapi::EMLINK);
pub const EPIPE: Errno = Errno(uapi::EPIPE);
pub const EDOM: Errno = Errno(uapi::EDOM);
pub const ERANGE: Errno = Errno(uapi::ERANGE);
pub const EDEADLK: Errno = Errno(uapi::EDEADLK);
pub const ENAMETOOLONG: Errno = Errno(uapi::ENAMETOOLONG);
pub const ENOLCK: Errno = Errno(uapi::ENOLCK);
pub const ENOSYS: Errno = Errno(uapi::ENOSYS);
pub const ENOTEMPTY: Errno = Errno(uapi::ENOTEMPTY);
pub const ELOOP: Errno = Errno(uapi::ELOOP);
pub const EWOULDBLOCK: Errno = Errno(uapi::EWOULDBLOCK);
pub const ENOMSG: Errno = Errno(uapi::ENOMSG);
pub const EIDRM: Errno = Errno(uapi::EIDRM);
pub const ECHRNG: Errno = Errno(uapi::ECHRNG);
pub const EL2NSYNC: Errno = Errno(uapi::EL2NSYNC);
pub const EL3HLT: Errno = Errno(uapi::EL3HLT);
pub const EL3RST: Errno = Errno(uapi::EL3RST);
pub const ELNRNG: Errno = Errno(uapi::ELNRNG);
pub const EUNATCH: Errno = Errno(uapi::EUNATCH);
pub const ENOCSI: Errno = Errno(uapi::ENOCSI);
pub const EL2HLT: Errno = Errno(uapi::EL2HLT);
pub const EBADE: Errno = Errno(uapi::EBADE);
pub const EBADR: Errno = Errno(uapi::EBADR);
pub const EXFULL: Errno = Errno(uapi::EXFULL);
pub const ENOANO: Errno = Errno(uapi::ENOANO);
pub const EBADRQC: Errno = Errno(uapi::EBADRQC);
pub const EBADSLT: Errno = Errno(uapi::EBADSLT);
pub const EDEADLOCK: Errno = Errno(uapi::EDEADLOCK);
pub const EBFONT: Errno = Errno(uapi::EBFONT);
pub const ENOSTR: Errno = Errno(uapi::ENOSTR);
pub const ENODATA: Errno = Errno(uapi::ENODATA);
pub const ETIME: Errno = Errno(uapi::ETIME);
pub const ENOSR: Errno = Errno(uapi::ENOSR);
pub const ENONET: Errno = Errno(uapi::ENONET);
pub const ENOPKG: Errno = Errno(uapi::ENOPKG);
pub const EREMOTE: Errno = Errno(uapi::EREMOTE);
pub const ENOLINK: Errno = Errno(uapi::ENOLINK);
pub const EADV: Errno = Errno(uapi::EADV);
pub const ESRMNT: Errno = Errno(uapi::ESRMNT);
pub const ECOMM: Errno = Errno(uapi::ECOMM);
pub const EPROTO: Errno = Errno(uapi::EPROTO);
pub const EMULTIHOP: Errno = Errno(uapi::EMULTIHOP);
pub const EDOTDOT: Errno = Errno(uapi::EDOTDOT);
pub const EBADMSG: Errno = Errno(uapi::EBADMSG);
pub const EOVERFLOW: Errno = Errno(uapi::EOVERFLOW);
pub const ENOTUNIQ: Errno = Errno(uapi::ENOTUNIQ);
pub const EBADFD: Errno = Errno(uapi::EBADFD);
pub const EREMCHG: Errno = Errno(uapi::EREMCHG);
pub const ELIBACC: Errno = Errno(uapi::ELIBACC);
pub const ELIBBAD: Errno = Errno(uapi::ELIBBAD);
pub const ELIBSCN: Errno = Errno(uapi::ELIBSCN);
pub const ELIBMAX: Errno = Errno(uapi::ELIBMAX);
pub const ELIBEXEC: Errno = Errno(uapi::ELIBEXEC);
pub const EILSEQ: Errno = Errno(uapi::EILSEQ);
pub const ERESTART: Errno = Errno(uapi::ERESTART);
pub const ESTRPIPE: Errno = Errno(uapi::ESTRPIPE);
pub const EUSERS: Errno = Errno(uapi::EUSERS);
pub const ENOTSOCK: Errno = Errno(uapi::ENOTSOCK);
pub const EDESTADDRREQ: Errno = Errno(uapi::EDESTADDRREQ);
pub const EMSGSIZE: Errno = Errno(uapi::EMSGSIZE);
pub const EPROTOTYPE: Errno = Errno(uapi::EPROTOTYPE);
pub const ENOPROTOOPT: Errno = Errno(uapi::ENOPROTOOPT);
pub const EPROTONOSUPPORT: Errno = Errno(uapi::EPROTONOSUPPORT);
pub const ESOCKTNOSUPPORT: Errno = Errno(uapi::ESOCKTNOSUPPORT);
pub const EOPNOTSUPP: Errno = Errno(uapi::EOPNOTSUPP);
pub const EPFNOSUPPORT: Errno = Errno(uapi::EPFNOSUPPORT);
pub const EAFNOSUPPORT: Errno = Errno(uapi::EAFNOSUPPORT);
pub const EADDRINUSE: Errno = Errno(uapi::EADDRINUSE);
pub const EADDRNOTAVAIL: Errno = Errno(uapi::EADDRNOTAVAIL);
pub const ENETDOWN: Errno = Errno(uapi::ENETDOWN);
pub const ENETUNREACH: Errno = Errno(uapi::ENETUNREACH);
pub const ENETRESET: Errno = Errno(uapi::ENETRESET);
pub const ECONNABORTED: Errno = Errno(uapi::ECONNABORTED);
pub const ECONNRESET: Errno = Errno(uapi::ECONNRESET);
pub const ENOBUFS: Errno = Errno(uapi::ENOBUFS);
pub const EISCONN: Errno = Errno(uapi::EISCONN);
pub const ENOTCONN: Errno = Errno(uapi::ENOTCONN);
pub const ESHUTDOWN: Errno = Errno(uapi::ESHUTDOWN);
pub const ETOOMANYREFS: Errno = Errno(uapi::ETOOMANYREFS);
pub const ETIMEDOUT: Errno = Errno(uapi::ETIMEDOUT);
pub const ECONNREFUSED: Errno = Errno(uapi::ECONNREFUSED);
pub const EHOSTDOWN: Errno = Errno(uapi::EHOSTDOWN);
pub const EHOSTUNREACH: Errno = Errno(uapi::EHOSTUNREACH);
pub const EALREADY: Errno = Errno(uapi::EALREADY);
pub const EINPROGRESS: Errno = Errno(uapi::EINPROGRESS);
pub const ESTALE: Errno = Errno(uapi::ESTALE);
pub const EUCLEAN: Errno = Errno(uapi::EUCLEAN);
pub const ENOTNAM: Errno = Errno(uapi::ENOTNAM);
pub const ENAVAIL: Errno = Errno(uapi::ENAVAIL);
pub const EISNAM: Errno = Errno(uapi::EISNAM);
pub const EREMOTEIO: Errno = Errno(uapi::EREMOTEIO);
pub const EDQUOT: Errno = Errno(uapi::EDQUOT);
pub const ENOMEDIUM: Errno = Errno(uapi::ENOMEDIUM);
pub const EMEDIUMTYPE: Errno = Errno(uapi::EMEDIUMTYPE);
pub const ECANCELED: Errno = Errno(uapi::ECANCELED);
pub const ENOKEY: Errno = Errno(uapi::ENOKEY);
pub const EKEYEXPIRED: Errno = Errno(uapi::EKEYEXPIRED);
pub const EKEYREVOKED: Errno = Errno(uapi::EKEYREVOKED);
pub const EKEYREJECTED: Errno = Errno(uapi::EKEYREJECTED);
pub const EOWNERDEAD: Errno = Errno(uapi::EOWNERDEAD);
pub const ENOTRECOVERABLE: Errno = Errno(uapi::ENOTRECOVERABLE);
pub const ERFKILL: Errno = Errno(uapi::ERFKILL);
pub const EHWPOISON: Errno = Errno(uapi::EHWPOISON);

// There isn't really a mapping from zx::Status to Errno. The correct mapping is context-speific
// but this converter is a reasonable first-approximation. The translation matches
// fdio_status_to_errno. See fxbug.dev/30921 for more context.
// TODO: Replace clients with more context-specific mappings.
impl Errno {
    pub fn from_status_like_fdio(status: zx::Status) -> Self {
        match status {
            zx::Status::NOT_FOUND => ENOENT,
            zx::Status::NO_MEMORY => ENOMEM,
            zx::Status::INVALID_ARGS => EINVAL,
            zx::Status::BUFFER_TOO_SMALL => EINVAL,
            zx::Status::TIMED_OUT => ETIMEDOUT,
            zx::Status::UNAVAILABLE => EBUSY,
            zx::Status::ALREADY_EXISTS => EEXIST,
            zx::Status::PEER_CLOSED => EPIPE,
            zx::Status::BAD_STATE => EPIPE,
            zx::Status::BAD_PATH => ENAMETOOLONG,
            zx::Status::IO => EIO,
            zx::Status::NOT_FILE => EISDIR,
            zx::Status::NOT_DIR => ENOTDIR,
            zx::Status::NOT_SUPPORTED => EOPNOTSUPP,
            zx::Status::WRONG_TYPE => EOPNOTSUPP,
            zx::Status::OUT_OF_RANGE => EINVAL,
            zx::Status::NO_RESOURCES => ENOMEM,
            zx::Status::BAD_HANDLE => EBADF,
            zx::Status::ACCESS_DENIED => EACCES,
            zx::Status::SHOULD_WAIT => EAGAIN,
            zx::Status::FILE_BIG => EFBIG,
            zx::Status::NO_SPACE => ENOSPC,
            zx::Status::NOT_EMPTY => ENOTEMPTY,
            zx::Status::IO_REFUSED => ECONNREFUSED,
            zx::Status::IO_INVALID => EIO,
            zx::Status::CANCELED => EBADF,
            zx::Status::PROTOCOL_NOT_SUPPORTED => EPROTONOSUPPORT,
            zx::Status::ADDRESS_UNREACHABLE => ENETUNREACH,
            zx::Status::ADDRESS_IN_USE => EADDRINUSE,
            zx::Status::NOT_CONNECTED => ENOTCONN,
            zx::Status::CONNECTION_REFUSED => ECONNREFUSED,
            zx::Status::CONNECTION_RESET => ECONNRESET,
            zx::Status::CONNECTION_ABORTED => ECONNABORTED,
            _ => EIO,
        }
    }
}

/// Calls the given callback with the given context, delimited with semicolons,
/// followed by each syscall, delimited with commas.
///
/// Intended to be used with other macros to produce code that needs to handle
/// each syscall.
macro_rules! for_each_syscall {
    {$callback:ident $(,$context:ident)*} => {
        $callback!{
            $($context;)*
            read,
            write,
            open,
            close,
            stat,
            fstat,
            lstat,
            poll,
            lseek,
            mmap,
            mprotect,
            munmap,
            brk,
            rt_sigaction,
            rt_sigprocmask,
            rt_sigreturn,
            ioctl,
            pread64,
            pwrite64,
            readv,
            writev,
            access,
            pipe,
            select,
            sched_yield,
            mremap,
            msync,
            mincore,
            madvise,
            shmget,
            shmat,
            shmctl,
            dup,
            dup2,
            pause,
            nanosleep,
            getitimer,
            alarm,
            setitimer,
            getpid,
            sendfile,
            socket,
            connect,
            accept,
            sendto,
            recvfrom,
            sendmsg,
            recvmsg,
            shutdown,
            bind,
            listen,
            getsockname,
            getpeername,
            socketpair,
            setsockopt,
            getsockopt,
            clone,
            fork,
            vfork,
            execve,
            exit,
            wait4,
            kill,
            uname,
            semget,
            semop,
            semctl,
            shmdt,
            msgget,
            msgsnd,
            msgrcv,
            msgctl,
            fcntl,
            flock,
            fsync,
            fdatasync,
            truncate,
            ftruncate,
            getdents,
            getcwd,
            chdir,
            fchdir,
            rename,
            mkdir,
            rmdir,
            creat,
            link,
            unlink,
            symlink,
            readlink,
            chmod,
            fchmod,
            chown,
            fchown,
            lchown,
            umask,
            gettimeofday,
            getrlimit,
            getrusage,
            sysinfo,
            times,
            ptrace,
            getuid,
            syslog,
            getgid,
            setuid,
            setgid,
            geteuid,
            getegid,
            setpgid,
            getppid,
            getpgrp,
            setsid,
            setreuid,
            setregid,
            getgroups,
            setgroups,
            setresuid,
            getresuid,
            setresgid,
            getresgid,
            getpgid,
            setfsuid,
            setfsgid,
            getsid,
            capget,
            capset,
            rt_sigpending,
            rt_sigtimedwait,
            rt_sigqueueinfo,
            rt_sigsuspend,
            sigaltstack,
            utime,
            mknod,
            uselib,
            personality,
            ustat,
            statfs,
            fstatfs,
            sysfs,
            getpriority,
            setpriority,
            sched_setparam,
            sched_getparam,
            sched_setscheduler,
            sched_getscheduler,
            sched_get_priority_max,
            sched_get_priority_min,
            sched_rr_get_interval,
            mlock,
            munlock,
            mlockall,
            munlockall,
            vhangup,
            modify_ldt,
            pivot_root,
            _sysctl,
            prctl,
            arch_prctl,
            adjtimex,
            setrlimit,
            chroot,
            sync,
            acct,
            settimeofday,
            mount,
            umount2,
            swapon,
            swapoff,
            reboot,
            sethostname,
            setdomainname,
            iopl,
            ioperm,
            create_module,
            init_module,
            delete_module,
            get_kernel_syms,
            query_module,
            quotactl,
            nfsservctl,
            getpmsg,
            putpmsg,
            afs_syscall,
            tuxcall,
            security,
            gettid,
            readahead,
            setxattr,
            lsetxattr,
            fsetxattr,
            getxattr,
            lgetxattr,
            fgetxattr,
            listxattr,
            llistxattr,
            flistxattr,
            removexattr,
            lremovexattr,
            fremovexattr,
            tkill,
            time,
            futex,
            sched_setaffinity,
            sched_getaffinity,
            set_thread_area,
            io_setup,
            io_destroy,
            io_getevents,
            io_submit,
            io_cancel,
            get_thread_area,
            lookup_dcookie,
            epoll_create,
            epoll_ctl_old,
            epoll_wait_old,
            remap_file_pages,
            getdents64,
            set_tid_address,
            restart_syscall,
            semtimedop,
            fadvise64,
            timer_create,
            timer_settime,
            timer_gettime,
            timer_getoverrun,
            timer_delete,
            clock_settime,
            clock_gettime,
            clock_getres,
            clock_nanosleep,
            exit_group,
            epoll_wait,
            epoll_ctl,
            tgkill,
            utimes,
            vserver,
            mbind,
            set_mempolicy,
            get_mempolicy,
            mq_open,
            mq_unlink,
            mq_timedsend,
            mq_timedreceive,
            mq_notify,
            mq_getsetattr,
            kexec_load,
            waitid,
            add_key,
            request_key,
            keyctl,
            ioprio_set,
            ioprio_get,
            inotify_init,
            inotify_add_watch,
            inotify_rm_watch,
            migrate_pages,
            openat,
            mkdirat,
            mknodat,
            fchownat,
            futimesat,
            newfstatat,
            unlinkat,
            renameat,
            linkat,
            symlinkat,
            readlinkat,
            fchmodat,
            faccessat,
            pselect6,
            ppoll,
            unshare,
            set_robust_list,
            get_robust_list,
            splice,
            tee,
            sync_file_range,
            vmsplice,
            move_pages,
            utimensat,
            epoll_pwait,
            signalfd,
            timerfd_create,
            eventfd,
            fallocate,
            timerfd_settime,
            timerfd_gettime,
            accept4,
            signalfd4,
            eventfd2,
            epoll_create1,
            dup3,
            pipe2,
            inotify_init1,
            preadv,
            pwritev,
            rt_tgsigqueueinfo,
            perf_event_open,
            recvmmsg,
            fanotify_init,
            fanotify_mark,
            prlimit64,
            name_to_handle_at,
            open_by_handle_at,
            clock_adjtime,
            syncfs,
            sendmmsg,
            setns,
            getcpu,
            process_vm_readv,
            process_vm_writev,
            kcmp,
            finit_module,
            sched_setattr,
            sched_getattr,
            renameat2,
            seccomp,
            getrandom,
            memfd_create,
            kexec_file_load,
            bpf,
            execveat,
            userfaultfd,
            membarrier,
            mlock2,
            copy_file_range,
            preadv2,
            pwritev2,
            pkey_mprotect,
            pkey_alloc,
            pkey_free,
            statx,
            io_pgetevents,
            rseq,
            pidfd_send_signal,
            io_uring_setup,
            io_uring_enter,
            io_uring_register,
            open_tree,
            move_mount,
            fsopen,
            fsconfig,
            fsmount,
            fspick,
            pidfd_open,
            clone3,
            close_range,
            openat2,
            pidfd_getfd,
            faccessat2,
            process_madvise,
        }
    }
}

/// A system call declaration.
///
/// Describes the name of the syscall and its number.
///
/// TODO: Add information about the number of arguments (and their types) so
/// that we can make strace more useful.
pub struct SyscallDecl {
    pub name: &'static str,
    pub number: u64,
}

/// A macro for declaring a const SyscallDecl for a given syscall.
///
/// The constant will be called DECL_<SYSCALL>.
macro_rules! syscall_decl {
    {$($name:ident,)*} => {
        paste! {
            $(pub const [<DECL_ $name:upper>]: SyscallDecl = SyscallDecl { name: stringify!($name), number: [<__NR_ $name>] as u64};)*
        }
    }
}

// Produce each syscall declaration.
for_each_syscall! {syscall_decl}

/// A declaration for an unknown syscall.
///
/// Useful so that functions that return a SyscallDecl have a sentinel
/// to return when they cannot find an appropriate syscall.
pub const DECL_UNKNOWN: SyscallDecl = SyscallDecl { name: "<unknown>", number: 0xFFFF };

/// A macro for the body of SyscallDecl::from_number.
///
/// Evaluates to the &'static SyscallDecl for the given number or to
/// &DECL_UNKNOWN if the number is unknown.
macro_rules! syscall_match {
    {$number:ident; $($name:ident,)*} => {
        paste! {
            match $number as u32 {
                $([<__NR_ $name>] => &[<DECL_ $name:upper>],)*
                _ => &DECL_UNKNOWN,
            }
        }
    }
}

impl SyscallDecl {
    /// The SyscallDecl for the given syscall number.
    ///
    /// Returns &DECL_UNKNOWN if the given syscall number is not known.
    pub fn from_number(number: u64) -> &'static SyscallDecl {
        for_each_syscall! { syscall_match, number }
    }
}

/// The result of executing a syscall.
///
/// It would be nice to have this also cover errors, but currently there is no stable way
/// to implement `std::ops::Try` (for the `?` operator) for custom enums, making it difficult
/// to work with.
pub enum SyscallResult {
    /// The process exited as a result of the syscall. The associated `u64` represents the process'
    /// exit code.
    Exit(i32),

    /// The syscall completed successfully. The associated `u64` is the return value from the
    /// syscall.
    Success(u64),
}

pub const SUCCESS: SyscallResult = SyscallResult::Success(0);

impl From<UserAddress> for SyscallResult {
    fn from(value: UserAddress) -> Self {
        SyscallResult::Success(value.ptr() as u64)
    }
}

impl From<FileDescriptor> for SyscallResult {
    fn from(value: FileDescriptor) -> Self {
        SyscallResult::Success(value.raw() as u64)
    }
}

impl From<i32> for SyscallResult {
    fn from(value: i32) -> Self {
        SyscallResult::Success(value as u64)
    }
}

impl From<u32> for SyscallResult {
    fn from(value: u32) -> Self {
        SyscallResult::Success(value as u64)
    }
}

impl From<u64> for SyscallResult {
    fn from(value: u64) -> Self {
        SyscallResult::Success(value)
    }
}

impl From<usize> for SyscallResult {
    fn from(value: usize) -> Self {
        SyscallResult::Success(value as u64)
    }
}

#[derive(Debug, Eq, PartialEq, Hash, Copy, Clone, AsBytes, FromBytes)]
#[repr(C)]
pub struct utsname_t {
    pub sysname: [u8; 65],
    pub nodename: [u8; 65],
    pub release: [u8; 65],
    pub version: [u8; 65],
    pub machine: [u8; 65],
}

#[derive(Debug, Default, Clone, Copy, AsBytes, FromBytes)]
#[repr(C)]
pub struct stat_t {
    pub st_dev: dev_t,
    pub st_ino: ino_t,
    pub st_nlink: u64,
    pub st_mode: mode_t,
    pub st_uid: uid_t,
    pub st_gid: gid_t,
    pub _pad0: u32,
    pub st_rdev: dev_t,
    pub st_size: off_t,
    pub st_blksize: i64,
    pub st_blocks: i64,
    pub st_atim: timespec,
    pub st_mtim: timespec,
    pub st_ctim: timespec,
    pub _pad3: [i64; 3],
}

#[derive(Debug, Default, Clone, Copy, AsBytes, FromBytes)]
#[repr(C)]
pub struct iovec_t {
    pub iov_base: UserAddress,
    pub iov_len: usize,
}

#[derive(Debug, Default, AsBytes, FromBytes)]
#[repr(C)]
pub struct statfs {
    f_type: i64,
    f_bsize: i64,
    f_blocks: i64,
    f_bfree: i64,
    f_bavail: i64,
    f_files: i64,
    f_ffree: i64,
    f_fsid: i64,
    f_namelen: i64,
    f_frsize: i64,
    f_flags: i64,
    f_spare: [i64; 4],
}
