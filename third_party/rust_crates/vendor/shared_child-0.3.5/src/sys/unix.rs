use std;
use std::io;
use std::process::Child;

// A handle on Unix is just the PID.
pub struct Handle(u32);

pub fn get_handle(child: &Child) -> Handle {
    Handle(child.id())
}

// This blocks until a child exits, without reaping the child.
pub fn wait_without_reaping(handle: Handle) -> io::Result<()> {
    loop {
        let ret = unsafe {
            let mut siginfo = std::mem::zeroed();
            libc::waitid(
                libc::P_PID,
                handle.0 as libc::id_t,
                &mut siginfo,
                libc::WEXITED | libc::WNOWAIT,
            )
        };
        if ret == 0 {
            return Ok(());
        }
        let error = io::Error::last_os_error();
        if error.kind() != io::ErrorKind::Interrupted {
            return Err(error);
        }
        // We were interrupted. Loop and retry.
    }
}

// This checks whether the child has already exited, without reaping the child.
pub fn try_wait_without_reaping(handle: Handle) -> io::Result<bool> {
    let mut siginfo: libc::siginfo_t;
    let ret = unsafe {
        // Darwin doesn't touch the siginfo_t struct if the child hasn't exited
        // yet. It expects us to have zeroed it ahead of time:
        //
        //   The state of the siginfo structure in this case
        //   is undefined.  Some implementations bzero it, some
        //   (like here) leave it untouched for efficiency.
        //
        //   Thus the most portable check for "no matching pid with
        //   WNOHANG" is to store a zero into si_pid before
        //   invocation, then check for a non-zero value afterwards.
        //
        // https://github.com/opensource-apple/xnu/blob/0a798f6738bc1db01281fc08ae024145e84df927/bsd/kern/kern_exit.c#L2150-L2156
        //
        // XXX: The siginfo_t struct has padding. Does that make it unsound to
        // initialize it this way?
        siginfo = std::mem::zeroed();
        libc::waitid(
            libc::P_PID,
            handle.0 as libc::id_t,
            &mut siginfo,
            libc::WEXITED | libc::WNOWAIT | libc::WNOHANG,
        )
    };
    if ret != 0 {
        // EINTR should be impossible here
        Err(io::Error::last_os_error())
    } else if siginfo.si_signo == libc::SIGCHLD {
        // The child has exited.
        Ok(true)
    } else if siginfo.si_signo == 0 {
        // The child has not exited.
        Ok(false)
    } else {
        // This should be impossible if we called waitid correctly. But it will
        // show up on macOS if we forgot to zero the siginfo_t above, for example.
        Err(io::Error::new(
            io::ErrorKind::Other,
            format!("unexpected si_signo from waitid: {}", siginfo.si_signo),
        ))
    }
}
