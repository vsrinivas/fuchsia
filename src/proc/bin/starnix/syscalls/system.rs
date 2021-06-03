// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_runtime::utc_time;
use fuchsia_zircon as zx;
use log::warn;

use crate::logging::*;
use crate::not_implemented;
use crate::syscalls::decls::SyscallDecl;
use crate::syscalls::*;
use crate::types::*;

pub fn sys_uname(
    ctx: &SyscallContext<'_>,
    name: UserRef<utsname_t>,
) -> Result<SyscallResult, Errno> {
    fn init_array(fixed: &mut [u8; 65], init: &'static str) {
        let init_bytes = init.as_bytes();
        let len = init.len();
        fixed[..len].copy_from_slice(init_bytes)
    }

    let mut result = utsname_t {
        sysname: [0; 65],
        nodename: [0; 65],
        release: [0; 65],
        version: [0; 65],
        machine: [0; 65],
    };
    init_array(&mut result.sysname, "Linux");
    init_array(&mut result.nodename, "local");
    init_array(&mut result.release, "5.7.17-starnix");
    init_array(&mut result.version, "starnix");
    init_array(&mut result.machine, "x86_64");
    ctx.task.mm.write_object(name, &result)?;
    return Ok(SUCCESS);
}

pub fn sys_getrandom(
    ctx: &SyscallContext<'_>,
    buf_addr: UserAddress,
    size: usize,
    _flags: i32,
) -> Result<SyscallResult, Errno> {
    let mut buf = vec![0; size];
    let size = zx::cprng_draw(&mut buf).map_err(impossible_error)?;
    ctx.task.mm.write_memory(buf_addr, &buf[0..size])?;
    Ok(size.into())
}

const NANOS_PER_SECOND: i64 = 1000 * 1000 * 1000;

pub fn sys_clock_gettime(
    ctx: &SyscallContext<'_>,
    which_clock: u32,
    tp_addr: UserRef<timespec>,
) -> Result<SyscallResult, Errno> {
    let time = match which_clock {
        CLOCK_REALTIME => utc_time(),
        CLOCK_MONOTONIC => zx::Time::get_monotonic(),
        _ => return Err(EINVAL),
    };
    let nanos = time.into_nanos();
    let tv = timespec { tv_sec: nanos / NANOS_PER_SECOND, tv_nsec: nanos % NANOS_PER_SECOND };
    return ctx.task.mm.write_object(tp_addr, &tv).map(|_| SUCCESS);
}

pub fn sys_gettimeofday(
    ctx: &SyscallContext<'_>,
    user_tv: UserRef<timeval>,
    user_tz: UserRef<timezone>,
) -> Result<SyscallResult, Errno> {
    if !user_tv.is_null() {
        let now = utc_time().into_nanos();
        let tv =
            timeval { tv_sec: now / NANOS_PER_SECOND, tv_usec: (now % NANOS_PER_SECOND) / 1000 };
        ctx.task.mm.write_object(user_tv, &tv)?;
    }
    if !user_tz.is_null() {
        not_implemented!("gettimeofday does not implement tz argument");
    }
    return Ok(SUCCESS);
}

fn get_time_from_timespec(ts: timespec) -> Result<zx::Time, Errno> {
    if ts.tv_nsec >= NANOS_PER_SECOND {
        return Err(EINVAL);
    }
    return Ok(zx::Time::ZERO
        + zx::Duration::from_seconds(ts.tv_sec)
        + zx::Duration::from_nanos(ts.tv_nsec));
}

pub fn sys_nanosleep(
    ctx: &SyscallContext<'_>,
    user_request: UserRef<timespec>,
    _user_remaining: UserRef<timespec>,
) -> Result<SyscallResult, Errno> {
    let mut request = timespec::default();
    ctx.task.mm.read_object(user_request, &mut request)?;
    let time = get_time_from_timespec(request)?;
    // TODO: We should be waiting on an object that can wake us up if we get a signal.
    time.sleep();
    Ok(SUCCESS)
}

pub fn sys_unknown(_ctx: &SyscallContext<'_>, syscall_number: u64) -> Result<SyscallResult, Errno> {
    warn!(target: "unknown_syscall", "UNKNOWN syscall({}): {}", syscall_number, SyscallDecl::from_number(syscall_number).name);
    // TODO: We should send SIGSYS once we have signals.
    Err(ENOSYS)
}
