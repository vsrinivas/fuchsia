use std::io;
use std::os::windows::prelude::*;
use std::process::Child;
use std::time::Duration;

type DWORD = u32;
type HANDLE = *mut u8;
type BOOL = i32;
type LPDWORD = *mut DWORD;

const FALSE: BOOL = 0;
const WAIT_OBJECT_0: DWORD = 0x00000000;
const WAIT_TIMEOUT: DWORD = 258;

extern "system" {
    fn WaitForSingleObject(hHandle: HANDLE, dwMilliseconds: DWORD) -> DWORD;
    fn GetExitCodeProcess(hProcess: HANDLE, lpExitCode: LPDWORD) -> BOOL;
}

#[derive(Eq, PartialEq, Copy, Clone, Debug)]
pub struct ExitStatus(DWORD);

pub fn wait_timeout(child: &mut Child, dur: Duration)
                       -> io::Result<Option<ExitStatus>> {
    let ms = dur.as_secs().checked_mul(1000).and_then(|amt| {
        amt.checked_add((dur.subsec_nanos() / 1_000_000) as u64)
    }).expect("failed to convert duration to milliseconds");
    let ms = if ms > (DWORD::max_value() as u64) {
        DWORD::max_value()
    } else {
        ms as DWORD
    };
    unsafe {
        match WaitForSingleObject(child.as_raw_handle() as *mut _, ms) {
            WAIT_OBJECT_0 => {}
            WAIT_TIMEOUT => return Ok(None),
            _ => return Err(io::Error::last_os_error()),
        }
        let mut status = 0;
        if GetExitCodeProcess(child.as_raw_handle() as *mut _, &mut status) == FALSE {
            Err(io::Error::last_os_error())
        } else {
            Ok(Some(ExitStatus(status)))
        }
    }
}

impl ExitStatus {
    pub fn success(&self) -> bool { self.code() == Some(0) }
    pub fn code(&self) -> Option<i32> { Some(self.0 as i32) }
    pub fn unix_signal(&self) -> Option<i32> { None }
}

