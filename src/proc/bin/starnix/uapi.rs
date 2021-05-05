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

use crate::fs::FdNumber;
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
pub struct Errno {
    value: u32,
    name: &'static str,
}

impl Errno {
    pub fn value(&self) -> i32 {
        self.value as i32
    }
}

impl fmt::Display for Errno {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "error {}: {}", self.value, self.name)
    }
}

pub const EPERM: Errno = Errno { value: uapi::EPERM, name: "EPERM" };
pub const ENOENT: Errno = Errno { value: uapi::ENOENT, name: "ENOENT" };
pub const ESRCH: Errno = Errno { value: uapi::ESRCH, name: "ESRCH" };
pub const EINTR: Errno = Errno { value: uapi::EINTR, name: "EINTR" };
pub const EIO: Errno = Errno { value: uapi::EIO, name: "EIO" };
pub const ENXIO: Errno = Errno { value: uapi::ENXIO, name: "ENXIO" };
pub const E2BIG: Errno = Errno { value: uapi::E2BIG, name: "E2BIG" };
pub const ENOEXEC: Errno = Errno { value: uapi::ENOEXEC, name: "ENOEXEC" };
pub const EBADF: Errno = Errno { value: uapi::EBADF, name: "EBADF" };
pub const ECHILD: Errno = Errno { value: uapi::ECHILD, name: "ECHILD" };
pub const EAGAIN: Errno = Errno { value: uapi::EAGAIN, name: "EAGAIN" };
pub const ENOMEM: Errno = Errno { value: uapi::ENOMEM, name: "ENOMEM" };
pub const EACCES: Errno = Errno { value: uapi::EACCES, name: "EACCES" };
pub const EFAULT: Errno = Errno { value: uapi::EFAULT, name: "EFAULT" };
pub const ENOTBLK: Errno = Errno { value: uapi::ENOTBLK, name: "ENOTBLK" };
pub const EBUSY: Errno = Errno { value: uapi::EBUSY, name: "EBUSY" };
pub const EEXIST: Errno = Errno { value: uapi::EEXIST, name: "EEXIST" };
pub const EXDEV: Errno = Errno { value: uapi::EXDEV, name: "EXDEV" };
pub const ENODEV: Errno = Errno { value: uapi::ENODEV, name: "ENODEV" };
pub const ENOTDIR: Errno = Errno { value: uapi::ENOTDIR, name: "ENOTDIR" };
pub const EISDIR: Errno = Errno { value: uapi::EISDIR, name: "EISDIR" };
pub const EINVAL: Errno = Errno { value: uapi::EINVAL, name: "EINVAL" };
pub const ENFILE: Errno = Errno { value: uapi::ENFILE, name: "ENFILE" };
pub const EMFILE: Errno = Errno { value: uapi::EMFILE, name: "EMFILE" };
pub const ENOTTY: Errno = Errno { value: uapi::ENOTTY, name: "ENOTTY" };
pub const ETXTBSY: Errno = Errno { value: uapi::ETXTBSY, name: "ETXTBSY" };
pub const EFBIG: Errno = Errno { value: uapi::EFBIG, name: "EFBIG" };
pub const ENOSPC: Errno = Errno { value: uapi::ENOSPC, name: "ENOSPC" };
pub const ESPIPE: Errno = Errno { value: uapi::ESPIPE, name: "ESPIPE" };
pub const EROFS: Errno = Errno { value: uapi::EROFS, name: "EROFS" };
pub const EMLINK: Errno = Errno { value: uapi::EMLINK, name: "EMLINK" };
pub const EPIPE: Errno = Errno { value: uapi::EPIPE, name: "EPIPE" };
pub const EDOM: Errno = Errno { value: uapi::EDOM, name: "EDOM" };
pub const ERANGE: Errno = Errno { value: uapi::ERANGE, name: "ERANGE" };
pub const EDEADLK: Errno = Errno { value: uapi::EDEADLK, name: "EDEADLK" };
pub const ENAMETOOLONG: Errno = Errno { value: uapi::ENAMETOOLONG, name: "ENAMETOOLONG" };
pub const ENOLCK: Errno = Errno { value: uapi::ENOLCK, name: "ENOLCK" };
pub const ENOSYS: Errno = Errno { value: uapi::ENOSYS, name: "ENOSYS" };
pub const ENOTEMPTY: Errno = Errno { value: uapi::ENOTEMPTY, name: "ENOTEMPTY" };
pub const ELOOP: Errno = Errno { value: uapi::ELOOP, name: "ELOOP" };
pub const EWOULDBLOCK: Errno = Errno { value: uapi::EWOULDBLOCK, name: "EWOULDBLOCK" };
pub const ENOMSG: Errno = Errno { value: uapi::ENOMSG, name: "ENOMSG" };
pub const EIDRM: Errno = Errno { value: uapi::EIDRM, name: "EIDRM" };
pub const ECHRNG: Errno = Errno { value: uapi::ECHRNG, name: "ECHRNG" };
pub const EL2NSYNC: Errno = Errno { value: uapi::EL2NSYNC, name: "EL2NSYNC" };
pub const EL3HLT: Errno = Errno { value: uapi::EL3HLT, name: "EL3HLT" };
pub const EL3RST: Errno = Errno { value: uapi::EL3RST, name: "EL3RST" };
pub const ELNRNG: Errno = Errno { value: uapi::ELNRNG, name: "ELNRNG" };
pub const EUNATCH: Errno = Errno { value: uapi::EUNATCH, name: "EUNATCH" };
pub const ENOCSI: Errno = Errno { value: uapi::ENOCSI, name: "ENOCSI" };
pub const EL2HLT: Errno = Errno { value: uapi::EL2HLT, name: "EL2HLT" };
pub const EBADE: Errno = Errno { value: uapi::EBADE, name: "EBADE" };
pub const EBADR: Errno = Errno { value: uapi::EBADR, name: "EBADR" };
pub const EXFULL: Errno = Errno { value: uapi::EXFULL, name: "EXFULL" };
pub const ENOANO: Errno = Errno { value: uapi::ENOANO, name: "ENOANO" };
pub const EBADRQC: Errno = Errno { value: uapi::EBADRQC, name: "EBADRQC" };
pub const EBADSLT: Errno = Errno { value: uapi::EBADSLT, name: "EBADSLT" };
pub const EDEADLOCK: Errno = Errno { value: uapi::EDEADLOCK, name: "EDEADLOCK" };
pub const EBFONT: Errno = Errno { value: uapi::EBFONT, name: "EBFONT" };
pub const ENOSTR: Errno = Errno { value: uapi::ENOSTR, name: "ENOSTR" };
pub const ENODATA: Errno = Errno { value: uapi::ENODATA, name: "ENODATA" };
pub const ETIME: Errno = Errno { value: uapi::ETIME, name: "ETIME" };
pub const ENOSR: Errno = Errno { value: uapi::ENOSR, name: "ENOSR" };
pub const ENONET: Errno = Errno { value: uapi::ENONET, name: "ENONET" };
pub const ENOPKG: Errno = Errno { value: uapi::ENOPKG, name: "ENOPKG" };
pub const EREMOTE: Errno = Errno { value: uapi::EREMOTE, name: "EREMOTE" };
pub const ENOLINK: Errno = Errno { value: uapi::ENOLINK, name: "ENOLINK" };
pub const EADV: Errno = Errno { value: uapi::EADV, name: "EADV" };
pub const ESRMNT: Errno = Errno { value: uapi::ESRMNT, name: "ESRMNT" };
pub const ECOMM: Errno = Errno { value: uapi::ECOMM, name: "ECOMM" };
pub const EPROTO: Errno = Errno { value: uapi::EPROTO, name: "EPROTO" };
pub const EMULTIHOP: Errno = Errno { value: uapi::EMULTIHOP, name: "EMULTIHOP" };
pub const EDOTDOT: Errno = Errno { value: uapi::EDOTDOT, name: "EDOTDOT" };
pub const EBADMSG: Errno = Errno { value: uapi::EBADMSG, name: "EBADMSG" };
pub const EOVERFLOW: Errno = Errno { value: uapi::EOVERFLOW, name: "EOVERFLOW" };
pub const ENOTUNIQ: Errno = Errno { value: uapi::ENOTUNIQ, name: "ENOTUNIQ" };
pub const EBADFD: Errno = Errno { value: uapi::EBADFD, name: "EBADFD" };
pub const EREMCHG: Errno = Errno { value: uapi::EREMCHG, name: "EREMCHG" };
pub const ELIBACC: Errno = Errno { value: uapi::ELIBACC, name: "ELIBACC" };
pub const ELIBBAD: Errno = Errno { value: uapi::ELIBBAD, name: "ELIBBAD" };
pub const ELIBSCN: Errno = Errno { value: uapi::ELIBSCN, name: "ELIBSCN" };
pub const ELIBMAX: Errno = Errno { value: uapi::ELIBMAX, name: "ELIBMAX" };
pub const ELIBEXEC: Errno = Errno { value: uapi::ELIBEXEC, name: "ELIBEXEC" };
pub const EILSEQ: Errno = Errno { value: uapi::EILSEQ, name: "EILSEQ" };
pub const ERESTART: Errno = Errno { value: uapi::ERESTART, name: "ERESTART" };
pub const ESTRPIPE: Errno = Errno { value: uapi::ESTRPIPE, name: "ESTRPIPE" };
pub const EUSERS: Errno = Errno { value: uapi::EUSERS, name: "EUSERS" };
pub const ENOTSOCK: Errno = Errno { value: uapi::ENOTSOCK, name: "ENOTSOCK" };
pub const EDESTADDRREQ: Errno = Errno { value: uapi::EDESTADDRREQ, name: "EDESTADDRREQ" };
pub const EMSGSIZE: Errno = Errno { value: uapi::EMSGSIZE, name: "EMSGSIZE" };
pub const EPROTOTYPE: Errno = Errno { value: uapi::EPROTOTYPE, name: "EPROTOTYPE" };
pub const ENOPROTOOPT: Errno = Errno { value: uapi::ENOPROTOOPT, name: "ENOPROTOOPT" };
pub const EPROTONOSUPPORT: Errno = Errno { value: uapi::EPROTONOSUPPORT, name: "EPROTONOSUPPORT" };
pub const ESOCKTNOSUPPORT: Errno = Errno { value: uapi::ESOCKTNOSUPPORT, name: "ESOCKTNOSUPPORT" };
pub const EOPNOTSUPP: Errno = Errno { value: uapi::EOPNOTSUPP, name: "EOPNOTSUPP" };
pub const EPFNOSUPPORT: Errno = Errno { value: uapi::EPFNOSUPPORT, name: "EPFNOSUPPORT" };
pub const EAFNOSUPPORT: Errno = Errno { value: uapi::EAFNOSUPPORT, name: "EAFNOSUPPORT" };
pub const EADDRINUSE: Errno = Errno { value: uapi::EADDRINUSE, name: "EADDRINUSE" };
pub const EADDRNOTAVAIL: Errno = Errno { value: uapi::EADDRNOTAVAIL, name: "EADDRNOTAVAIL" };
pub const ENETDOWN: Errno = Errno { value: uapi::ENETDOWN, name: "ENETDOWN" };
pub const ENETUNREACH: Errno = Errno { value: uapi::ENETUNREACH, name: "ENETUNREACH" };
pub const ENETRESET: Errno = Errno { value: uapi::ENETRESET, name: "ENETRESET" };
pub const ECONNABORTED: Errno = Errno { value: uapi::ECONNABORTED, name: "ECONNABORTED" };
pub const ECONNRESET: Errno = Errno { value: uapi::ECONNRESET, name: "ECONNRESET" };
pub const ENOBUFS: Errno = Errno { value: uapi::ENOBUFS, name: "ENOBUFS" };
pub const EISCONN: Errno = Errno { value: uapi::EISCONN, name: "EISCONN" };
pub const ENOTCONN: Errno = Errno { value: uapi::ENOTCONN, name: "ENOTCONN" };
pub const ESHUTDOWN: Errno = Errno { value: uapi::ESHUTDOWN, name: "ESHUTDOWN" };
pub const ETOOMANYREFS: Errno = Errno { value: uapi::ETOOMANYREFS, name: "ETOOMANYREFS" };
pub const ETIMEDOUT: Errno = Errno { value: uapi::ETIMEDOUT, name: "ETIMEDOUT" };
pub const ECONNREFUSED: Errno = Errno { value: uapi::ECONNREFUSED, name: "ECONNREFUSED" };
pub const EHOSTDOWN: Errno = Errno { value: uapi::EHOSTDOWN, name: "EHOSTDOWN" };
pub const EHOSTUNREACH: Errno = Errno { value: uapi::EHOSTUNREACH, name: "EHOSTUNREACH" };
pub const EALREADY: Errno = Errno { value: uapi::EALREADY, name: "EALREADY" };
pub const EINPROGRESS: Errno = Errno { value: uapi::EINPROGRESS, name: "EINPROGRESS" };
pub const ESTALE: Errno = Errno { value: uapi::ESTALE, name: "ESTALE" };
pub const EUCLEAN: Errno = Errno { value: uapi::EUCLEAN, name: "EUCLEAN" };
pub const ENOTNAM: Errno = Errno { value: uapi::ENOTNAM, name: "ENOTNAM" };
pub const ENAVAIL: Errno = Errno { value: uapi::ENAVAIL, name: "ENAVAIL" };
pub const EISNAM: Errno = Errno { value: uapi::EISNAM, name: "EISNAM" };
pub const EREMOTEIO: Errno = Errno { value: uapi::EREMOTEIO, name: "EREMOTEIO" };
pub const EDQUOT: Errno = Errno { value: uapi::EDQUOT, name: "EDQUOT" };
pub const ENOMEDIUM: Errno = Errno { value: uapi::ENOMEDIUM, name: "ENOMEDIUM" };
pub const EMEDIUMTYPE: Errno = Errno { value: uapi::EMEDIUMTYPE, name: "EMEDIUMTYPE" };
pub const ECANCELED: Errno = Errno { value: uapi::ECANCELED, name: "ECANCELED" };
pub const ENOKEY: Errno = Errno { value: uapi::ENOKEY, name: "ENOKEY" };
pub const EKEYEXPIRED: Errno = Errno { value: uapi::EKEYEXPIRED, name: "EKEYEXPIRED" };
pub const EKEYREVOKED: Errno = Errno { value: uapi::EKEYREVOKED, name: "EKEYREVOKED" };
pub const EKEYREJECTED: Errno = Errno { value: uapi::EKEYREJECTED, name: "EKEYREJECTED" };
pub const EOWNERDEAD: Errno = Errno { value: uapi::EOWNERDEAD, name: "EOWNERDEAD" };
pub const ENOTRECOVERABLE: Errno = Errno { value: uapi::ENOTRECOVERABLE, name: "ENOTRECOVERABLE" };
pub const ERFKILL: Errno = Errno { value: uapi::ERFKILL, name: "ERFKILL" };
pub const EHWPOISON: Errno = Errno { value: uapi::EHWPOISON, name: "EHWPOISON" };

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
#[derive(Debug, Eq, PartialEq)]
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

impl From<FdNumber> for SyscallResult {
    fn from(value: FdNumber) -> Self {
        SyscallResult::Success(value.raw() as u64)
    }
}

impl From<bool> for SyscallResult {
    fn from(value: bool) -> Self {
        SyscallResult::Success(if value { 1 } else { 0 })
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

#[derive(Debug, Default, Clone, Copy, AsBytes, FromBytes)]
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

#[derive(Debug, Default, Clone, Copy, PartialEq, Eq, AsBytes, FromBytes)]
#[repr(C)]
pub struct sigaltstack_t {
    pub ss_sp: UserAddress,
    pub ss_flags: u32,
    pub _pad0: u32,
    pub ss_size: usize,
}
