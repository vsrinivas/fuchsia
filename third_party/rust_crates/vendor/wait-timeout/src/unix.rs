//! Unix implementation of waiting for children with timeouts
//!
//! On unix, wait() and its friends have no timeout parameters, so there is
//! no way to time out a thread in wait(). From some googling and some
//! thinking, it appears that there are a few ways to handle timeouts in
//! wait(), but the only real reasonable one for a multi-threaded program is
//! to listen for SIGCHLD.
//!
//! With this in mind, the waiting mechanism with a timeout only uses
//! waitpid() with WNOHANG, but otherwise all the necessary blocking is done by
//! waiting for a SIGCHLD to arrive (and that blocking has a timeout). Note,
//! however, that waitpid() is still used to actually reap the child.
//!
//! Signal handling is super tricky in general, and this is no exception. Due
//! to the async nature of SIGCHLD, we use the self-pipe trick to transmit
//! data out of the signal handler to the rest of the application.

#![allow(bad_style)]

use std::cmp;
use std::collections::HashMap;
use std::fs::File;
use std::io::{self, Write, Read};
use std::mem;
use std::os::unix::prelude::*;
use std::process::Child;
use std::sync::{Once, ONCE_INIT, Mutex};
use std::time::Duration;

use libc::{self, c_int};

static INIT: Once = ONCE_INIT;
static mut STATE: *mut State = 0 as *mut _;

struct State {
    prev: libc::sigaction,
    write: File,
    read: File,
    map: Mutex<StateMap>,
}

type StateMap = HashMap<c_int, (File, Option<ExitStatus>)>;

#[derive(Eq, PartialEq, Copy, Clone, Debug)]
pub struct ExitStatus(c_int);

pub fn wait_timeout(child: &mut Child, dur: Duration)
                    -> io::Result<Option<ExitStatus>> {
    INIT.call_once(State::init);
    unsafe {
        (*STATE).wait_timeout(child, dur)
    }
}

// Do $value as type_of($target)
macro_rules! _as {
    ($value:expr, $target:expr) => (
        {
            let mut x = $target;
            x = $value as _;
            x
        }
    )
}

impl State {
    #[allow(unused_assignments)]
    fn init() {
        unsafe {
            // Create our "self pipe" and then set both ends to nonblocking
            // mode.
            let (read, write) = file_pair().unwrap();

            let mut state = Box::new(State {
                prev: mem::zeroed(),
                write: write,
                read: read,
                map: Mutex::new(HashMap::new()),
            });

            // Register our sigchld handler
            let mut new: libc::sigaction = mem::zeroed();
            new.sa_sigaction = sigchld_handler as usize;

            // FIXME: remove this workaround when the PR to libc get merged and released
            //
            // This is a workaround for the type mismatch in the definition of SA_*
            // constants for android. See https://github.com/rust-lang/libc/pull/511
            //
            let sa_flags = new.sa_flags;
            new.sa_flags = _as!(libc::SA_NOCLDSTOP, sa_flags) |
                           _as!(libc::SA_RESTART, sa_flags) |
                           _as!(libc::SA_SIGINFO, sa_flags);

            assert_eq!(libc::sigaction(libc::SIGCHLD, &new, &mut state.prev), 0);

            STATE = mem::transmute(state);
        }
    }

    fn wait_timeout(&self, child: &mut Child, dur: Duration)
                       -> io::Result<Option<ExitStatus>> {
        // First up, prep our notification pipe which will tell us when our
        // child has been reaped (other threads may signal this pipe).
        let (read, write) = try!(file_pair());
        let id = child.id() as c_int;

        // Next, take a lock on the map of children currently waiting. Right
        // after this, **before** we add ourselves to the map, we check to see
        // if our child has actually already exited via a `try_wait`. If the
        // child has exited then we return immediately as we'll never otherwise
        // receive a SIGCHLD notification.
        //
        // If the wait reports the child is still running, however, we add
        // ourselves to the map and then block in `select` waiting for something
        // to happen.
        let mut map = self.map.lock().unwrap();
        if let Some(status) = try!(try_wait(id)) {
            return Ok(Some(status))
        }
        assert!(map.insert(id, (write, None)).is_none());
        drop(map);


        // Alright, we're guaranteed that we'll eventually get a SIGCHLD due
        // to our `try_wait` failing, and we're also guaranteed that we'll
        // get notified about this because we're in the map. Next up wait
        // for an event.
        //
        // Note that this happens in a loop for two reasons; we could
        // receive EINTR or we could pick up a SIGCHLD for other threads but not
        // actually be ready oureslves.
        let end_time = now_ns();
        loop {
            let cur_time = now_ns();
            let nanos = cur_time - end_time;
            let elapsed = Duration::new(nanos / 1_000_000_000,
                                        (nanos % 1_000_000_000) as u32);
            if elapsed >= dur {
                break
            }
            let timeout = dur - elapsed;
            let mut timeout = libc::timeval {
                tv_sec: timeout.as_secs() as libc::time_t,
                tv_usec: (timeout.subsec_nanos() / 1000) as libc::suseconds_t,
            };
            let r = unsafe {
                let mut set: libc::fd_set = mem::uninitialized();
                libc::FD_ZERO(&mut set);
                libc::FD_SET(self.read.as_raw_fd(), &mut set);
                libc::FD_SET(read.as_raw_fd(), &mut set);
                let max = cmp::max(self.read.as_raw_fd(), read.as_raw_fd()) + 1;
                libc::select(max, &mut set, 0 as *mut _, 0 as *mut _, &mut timeout)
            };
            let timeout = match r {
                0 => true,
                1 | 2 => false,
                n => {
                    let err = io::Error::last_os_error();
                    if err.kind() == io::ErrorKind::Interrupted {
                        continue
                    } else {
                        panic!("error in select = {}: {}", n, err)
                    }
                }
            };

            // Now that something has happened, we need to process what actually
            // happened. There's are three reasons we could have woken up:
            //
            // 1. The file descriptor in our SIGCHLD handler was written to.
            //    This means that a SIGCHLD was received and we need to poll the
            //    entire list of waiting processes to figure out which ones
            //    actually exited.
            // 2. Our file descriptor was written to. This means that another
            //    thread reaped our child and listed the exit status in the
            //    local map.
            // 3. We timed out. This means we need to remove ourselves from the
            //    map and simply carry on.
            //
            // In the case that a SIGCHLD signal was received, we do that
            // processing and keep going. If our fd was written to or a timeout
            // was received then we break out of the loop and return from this
            // call.
            let mut map = self.map.lock().unwrap();
            if drain(&self.read) {
                self.process_sigchlds(&mut map);
            }

            if drain(&read) || timeout {
                break
            }
        }

        let mut map = self.map.lock().unwrap();
        let (_write, ret) = map.remove(&id).unwrap();
        Ok(ret)
    }

    fn process_sigchlds(&self, map: &mut StateMap) {
        for (&k, &mut (ref write, ref mut status)) in map {
            // Already reaped, nothing to do here
            if status.is_some() {
                continue
            }

            *status = try_wait(k).unwrap();
            if status.is_some() {
                notify(write);
            }
        }
    }
}

fn file_pair() -> io::Result<(File, File)> {
    // TODO: CLOEXEC
    unsafe {
        let mut pipes = [0; 2];
        if libc::pipe(pipes.as_mut_ptr()) != 0 {
            return Err(io::Error::last_os_error())
        }
        let set = 1 as c_int;
        assert_eq!(libc::ioctl(pipes[0], libc::FIONBIO, &set), 0);
        assert_eq!(libc::ioctl(pipes[1], libc::FIONBIO, &set), 0);
        Ok((File::from_raw_fd(pipes[0]), File::from_raw_fd(pipes[1])))
    }
}

fn try_wait(id: c_int) -> io::Result<Option<ExitStatus>> {
    let mut status = 0;
    match unsafe { libc::waitpid(id, &mut status, libc::WNOHANG) } {
        0 => Ok(None),
        n if n < 0 => return Err(io::Error::last_os_error()),
        n => {
            assert_eq!(n, id);
            Ok(Some(ExitStatus(status)))
        }
    }
}

fn drain(mut file: &File) -> bool {
    let mut ret = false;
    let mut buf = [0u8; 16];
    loop {
        match file.read(&mut buf) {
            Ok(0) => return true, // EOF == something happened
            Ok(..) => ret = true, // data read, but keep draining
            Err(e) => {
                if e.kind() == io::ErrorKind::WouldBlock {
                    return ret
                } else {
                    panic!("bad read: {}", e)
                }
            }
        }
    }
}

fn notify(mut file: &File) {
    match file.write(&[1]) {
        Ok(..) => {}
        Err(e) => {
            if e.kind() != io::ErrorKind::WouldBlock {
                panic!("bad error on write fd: {}", e)
            }
        }
    }
}

fn now_ns() -> u64 {
    unsafe {
        let mut now: libc::timeval = mem::zeroed();
        libc::gettimeofday(&mut now, 0 as *mut _);
        (now.tv_sec as u64 * 1_000_000_000) + (now.tv_usec as u64 * 1_000)
    }
}

// Signal handler for SIGCHLD signals, must be async-signal-safe!
//
// This function will write to the writing half of the "self pipe" to wake
// up the helper thread if it's waiting. Note that this write must be
// nonblocking because if it blocks and the reader is the thread we
// interrupted, then we'll deadlock.
//
// When writing, if the write returns EWOULDBLOCK then we choose to ignore
// it. At that point we're guaranteed that there's something in the pipe
// which will wake up the other end at some point, so we just allow this
// signal to be coalesced with the pending signals on the pipe.
#[allow(unused_assignments)]
extern fn sigchld_handler(signum: c_int,
                          info: *mut libc::siginfo_t,
                          ptr: *mut libc::c_void) {
    type FnSigaction = extern fn(c_int, *mut libc::siginfo_t, *mut libc::c_void);
    type FnHandler = extern fn(c_int);

    unsafe {
        let state = &*STATE;
        notify(&state.write);

        let fnptr = state.prev.sa_sigaction;
        if fnptr == 0 {
            return
        }
        // FIXME: remove this workaround when the PR to libc get merged and released
        //
        // This is a workaround for the type mismatch in the definition of SA_*
        // constants for android. See https://github.com/rust-lang/libc/pull/511
        //
        if state.prev.sa_flags & _as!(libc::SA_SIGINFO, state.prev.sa_flags) == 0 {
            let action = mem::transmute::<usize, FnHandler>(fnptr);
            action(signum)
        } else {
            let action = mem::transmute::<usize, FnSigaction>(fnptr);
            action(signum, info, ptr)
        }
    }
}

impl ExitStatus {
    pub fn success(&self) -> bool {
        self.code() == Some(0)
    }

    pub fn code(&self) -> Option<i32> {
        unsafe {
            if libc::WIFEXITED(self.0) {
                Some(libc::WEXITSTATUS(self.0))
            } else {
                None
            }
        }
    }

    pub fn unix_signal(&self) -> Option<i32> {
        unsafe {
            if !libc::WIFEXITED(self.0) {
                Some(libc::WTERMSIG(self.0))
            } else {
                None
            }
        }
    }
}
