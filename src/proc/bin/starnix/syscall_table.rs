// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::executive::ThreadContext;
use crate::syscalls::*;
use crate::types::*;

trait FromSyscallArg {
    fn from_arg(arg: u64) -> Self;
}
impl FromSyscallArg for i32 {
    fn from_arg(arg: u64) -> i32 {
        arg as i32
    }
}
impl FromSyscallArg for usize {
    fn from_arg(arg: u64) -> usize {
        arg as usize
    }
}
impl FromSyscallArg for UserAddress {
    fn from_arg(arg: u64) -> UserAddress {
        UserAddress::from(arg)
    }
}
trait IntoSyscallArg<T> {
    fn into_arg(self) -> T;
}

impl<T> IntoSyscallArg<T> for u64
where
    T: FromSyscallArg,
{
    fn into_arg(self) -> T {
        T::from_arg(self)
    }
}

macro_rules! syscall_match {
    {
        $ctx:ident; $syscall_number:ident; $args:ident;
        $($call:ident => $func:ident[$num_args:tt],)*
    } => {
        match $syscall_number {
            $($call => syscall_match!(@call $ctx; $args; $func[$num_args]),)*
            _ => sys_unknown($ctx, $syscall_number),
        }
    };

    (@call $ctx:ident; $args:ident; $func:ident [0]) => ($func($ctx));
    (@call $ctx:ident; $args:ident; $func:ident [1]) => ($func($ctx, $args.0.into_arg()));
    (@call $ctx:ident; $args:ident; $func:ident [2]) => ($func($ctx, $args.0.into_arg(), $args.1.into_arg()));
    (@call $ctx:ident; $args:ident; $func:ident [3]) => ($func($ctx, $args.0.into_arg(), $args.1.into_arg(), $args.2.into_arg()));
    (@call $ctx:ident; $args:ident; $func:ident [4]) => ($func($ctx, $args.0.into_arg(), $args.1.into_arg(), $args.2.into_arg(), $args.3.into_arg()));
    (@call $ctx:ident; $args:ident; $func:ident [5]) => ($func($ctx, $args.0.into_arg(), $args.1.into_arg(), $args.2.into_arg(), $args.3.into_arg(), $args.4.into_arg()));
    (@call $ctx:ident; $args:ident; $func:ident [6]) => ($func($ctx, $args.0.into_arg(), $args.1.into_arg(), $args.2.into_arg(), $args.3.into_arg(), $args.4.into_arg(), $args.5.into_arg()));
}

pub fn dispatch_syscall(
    ctx: &mut ThreadContext,
    syscall_number: syscall_number_t,
    args: (u64, u64, u64, u64, u64, u64),
) -> Result<SyscallResult, Errno> {
    syscall_match! {
        ctx; syscall_number; args;
        SYS_WRITE => sys_write[3],
        SYS_FSTAT => sys_fstat[2],
        SYS_MMAP => sys_mmap[6],
        SYS_MPROTECT => sys_mprotect[3],
        SYS_BRK => sys_brk[1],
        SYS_WRITEV => sys_writev[3],
        SYS_ACCESS => sys_access[2],
        SYS_GETPID => sys_getpid[0],
        SYS_EXIT => sys_exit[1],
        SYS_UNAME => sys_uname[1],
        SYS_READLINK => sys_readlink[3],
        SYS_GETUID => sys_getuid[0],
        SYS_GETGID => sys_getgid[0],
        SYS_GETEUID => sys_geteuid[0],
        SYS_GETEGID => sys_getegid[0],
        SYS_SCHED_GETSCHEDULER => sys_sched_getscheduler[1],
        SYS_ARCH_PRCTL => sys_arch_prctl[2],
        SYS_EXIT_GROUP => sys_exit_group[1],
        SYS_GETRANDOM => sys_getrandom[3],
    }
}
