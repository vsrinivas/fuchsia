// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use paste::paste;

use crate::executive::ThreadContext;
use crate::syscalls::*;
use crate::uapi::*;

trait FromSyscallArg {
    fn from_arg(arg: u64) -> Self;
}
impl FromSyscallArg for i32 {
    fn from_arg(arg: u64) -> i32 {
        arg as i32
    }
}
impl FromSyscallArg for u32 {
    fn from_arg(arg: u64) -> u32 {
        arg as u32
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
        $($call:ident [$num_args:tt],)*
    } => {
        paste! {
            match $syscall_number as u32 {
                $(crate::uapi::[<__NR_ $call>] => syscall_match!(@call $ctx; $args; [<sys_ $call>][$num_args]),)*
                _ => sys_unknown($ctx, $syscall_number),
            }
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
    syscall_number: u64,
    args: (u64, u64, u64, u64, u64, u64),
) -> Result<SyscallResult, Errno> {
    syscall_match! {
        ctx; syscall_number; args;
        write[3],
        fstat[2],
        mmap[6],
        mprotect[3],
        brk[1],
        writev[3],
        access[2],
        getpid[0],
        exit[1],
        uname[1],
        readlink[3],
        getuid[0],
        getgid[0],
        geteuid[0],
        getegid[0],
        sched_getscheduler[1],
        arch_prctl[2],
        exit_group[1],
        getrandom[3],
        clock_gettime[2],
        gettimeofday[2],
    }
}
