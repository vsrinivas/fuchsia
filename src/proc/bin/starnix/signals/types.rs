// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(dead_code)]

use crate::errno;
use crate::error;
use crate::types::*;
use parking_lot::RwLock;
use std::convert::TryFrom;
use std::fmt;
use std::hash::{Hash, Hasher};
use std::sync::Arc;
use zerocopy::{AsBytes, FromBytes};

/// An unchecked signal represents a signal that has not been through verification, and may
/// represent an invalid signal number.
#[derive(Copy, Clone, PartialEq)]
pub struct UncheckedSignal(u64);

impl UncheckedSignal {
    pub fn new(value: u64) -> UncheckedSignal {
        UncheckedSignal(value)
    }
}
impl From<Signal> for UncheckedSignal {
    fn from(signal: Signal) -> UncheckedSignal {
        UncheckedSignal(signal.number as u64)
    }
}
impl From<u32> for UncheckedSignal {
    fn from(value: u32) -> UncheckedSignal {
        UncheckedSignal(value as u64)
    }
}

/// The `Signal` struct represents a valid signal.
#[derive(Debug, Copy, Clone)]
pub struct Signal {
    number: u32,
}

impl Hash for Signal {
    fn hash<H: Hasher>(&self, state: &mut H) {
        self.number.hash(state);
    }
}

impl PartialEq for Signal {
    fn eq(&self, other: &Self) -> bool {
        self.number == other.number
    }
}

impl Eq for Signal {}

impl Signal {
    /// The signal number, guaranteed to be a value between 1..=NUM_SIGNALS.
    pub fn number(&self) -> u32 {
        self.number
    }

    /// Returns the bitmask for this signal number.
    pub fn mask(&self) -> u64 {
        1 << (self.number - 1)
    }

    /// Returns true if the signal passes the provided mask.
    pub fn passes_mask(&self, mask: u64) -> bool {
        self.mask() & mask == 0
    }

    /// Returns true if the signal is a real-time signal.
    pub fn is_real_time(&self) -> bool {
        self.number >= SIGRTMIN
    }

    /// Returns true if this signal can't be blocked. This means either SIGKILL or SIGSTOP.
    pub fn is_unblockable(&self) -> bool {
        self.number == uapi::SIGKILL || self.number == uapi::SIGSTOP
    }

    /// The number of signals, also the highest valid signal number.
    pub const NUM_SIGNALS: u32 = 64;

    pub const SIGHUP: Signal = Signal { number: uapi::SIGHUP };
    pub const SIGINT: Signal = Signal { number: uapi::SIGINT };
    pub const SIGQUIT: Signal = Signal { number: uapi::SIGQUIT };
    pub const SIGILL: Signal = Signal { number: uapi::SIGILL };
    pub const SIGTRAP: Signal = Signal { number: uapi::SIGTRAP };
    pub const SIGABRT: Signal = Signal { number: uapi::SIGABRT };
    pub const SIGIOT: Signal = Signal { number: uapi::SIGIOT };
    pub const SIGBUS: Signal = Signal { number: uapi::SIGBUS };
    pub const SIGFPE: Signal = Signal { number: uapi::SIGFPE };
    pub const SIGKILL: Signal = Signal { number: uapi::SIGKILL };
    pub const SIGUSR1: Signal = Signal { number: uapi::SIGUSR1 };
    pub const SIGSEGV: Signal = Signal { number: uapi::SIGSEGV };
    pub const SIGUSR2: Signal = Signal { number: uapi::SIGUSR2 };
    pub const SIGPIPE: Signal = Signal { number: uapi::SIGPIPE };
    pub const SIGALRM: Signal = Signal { number: uapi::SIGALRM };
    pub const SIGTERM: Signal = Signal { number: uapi::SIGTERM };
    pub const SIGSTKFLT: Signal = Signal { number: uapi::SIGSTKFLT };
    pub const SIGCHLD: Signal = Signal { number: uapi::SIGCHLD };
    pub const SIGCONT: Signal = Signal { number: uapi::SIGCONT };
    pub const SIGSTOP: Signal = Signal { number: uapi::SIGSTOP };
    pub const SIGTSTP: Signal = Signal { number: uapi::SIGTSTP };
    pub const SIGTTIN: Signal = Signal { number: uapi::SIGTTIN };
    pub const SIGTTOU: Signal = Signal { number: uapi::SIGTTOU };
    pub const SIGURG: Signal = Signal { number: uapi::SIGURG };
    pub const SIGXCPU: Signal = Signal { number: uapi::SIGXCPU };
    pub const SIGXFSZ: Signal = Signal { number: uapi::SIGXFSZ };
    pub const SIGVTALRM: Signal = Signal { number: uapi::SIGVTALRM };
    pub const SIGPROF: Signal = Signal { number: uapi::SIGPROF };
    pub const SIGWINCH: Signal = Signal { number: uapi::SIGWINCH };
    pub const SIGIO: Signal = Signal { number: uapi::SIGIO };
    pub const SIGPWR: Signal = Signal { number: uapi::SIGPWR };
    pub const SIGSYS: Signal = Signal { number: uapi::SIGSYS };
    pub const SIGRTMIN: Signal = Signal { number: uapi::SIGRTMIN };
}

impl TryFrom<UncheckedSignal> for Signal {
    type Error = Errno;

    fn try_from(value: UncheckedSignal) -> Result<Self, Self::Error> {
        let value = u32::try_from(value.0).map_err(|_| errno!(EINVAL))?;
        if value >= 1 && value <= Signal::NUM_SIGNALS {
            Ok(Signal { number: u32::from(value) })
        } else {
            error!(EINVAL)
        }
    }
}

impl TryFrom<&UncheckedSignal> for Signal {
    type Error = Errno;

    fn try_from(value: &UncheckedSignal) -> Result<Self, Self::Error> {
        Self::try_from(*value)
    }
}

impl fmt::Display for Signal {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        if !self.is_real_time() {
            let name = match self.number {
                uapi::SIGHUP => "SIGHUP",
                uapi::SIGINT => "SIGINT",
                uapi::SIGQUIT => "SIGQUIT",
                uapi::SIGILL => "SIGILL",
                uapi::SIGTRAP => "SIGTRAP",
                uapi::SIGABRT => "SIGABRT",
                uapi::SIGBUS => "SIGBUS",
                uapi::SIGFPE => "SIGFPE",
                uapi::SIGKILL => "SIGKILL",
                uapi::SIGUSR1 => "SIGUSR1",
                uapi::SIGSEGV => "SIGSEGV",
                uapi::SIGUSR2 => "SIGUSR2",
                uapi::SIGPIPE => "SIGPIPE",
                uapi::SIGALRM => "SIGALRM",
                uapi::SIGTERM => "SIGTERM",
                uapi::SIGSTKFLT => "SIGSTKFLT",
                uapi::SIGCHLD => "SIGCHLD",
                uapi::SIGCONT => "SIGCONT",
                uapi::SIGSTOP => "SIGSTOP",
                uapi::SIGTSTP => "SIGTSTP",
                uapi::SIGTTIN => "SIGTTIN",
                uapi::SIGTTOU => "SIGTTOU",
                uapi::SIGURG => "SIGURG",
                uapi::SIGXCPU => "SIGXCPU",
                uapi::SIGXFSZ => "SIGXFSZ",
                uapi::SIGVTALRM => "SIGVTALRM",
                uapi::SIGPROF => "SIGPROF",
                uapi::SIGWINCH => "SIGWINCH",
                uapi::SIGIO => "SIGIO",
                uapi::SIGPWR => "SIGPWR",
                uapi::SIGSYS => "SIGSYS",
                _ => panic!("invalid signal number!"),
            };
            write!(f, "signal {}: {}", self.number, name)
        } else {
            write!(f, "signal {}: SIGRTMIN+{}", self.number, self.number - uapi::SIGRTMIN)
        }
    }
}

/// `SignalActions` contains a `sigaction_t` for each valid signal.
#[derive(Debug)]
pub struct SignalActions {
    actions: RwLock<[sigaction_t; Signal::NUM_SIGNALS as usize + 1]>,
}

impl SignalActions {
    /// Returns a collection of `sigaction_t`s that contains default values for each signal.
    pub fn default() -> Arc<SignalActions> {
        Arc::new(SignalActions {
            actions: RwLock::new([sigaction_t::default(); Signal::NUM_SIGNALS as usize + 1]),
        })
    }

    /// Returns the `sigaction_t` that is currently set for `signal`.
    pub fn get(&self, signal: Signal) -> sigaction_t {
        // This is safe, since the actions always contain a value for each signal.
        self.actions.read()[signal.number() as usize]
    }

    /// Update the action for `signal`. Returns the previously configured action.
    pub fn set(&self, signal: Signal, new_action: sigaction_t) -> sigaction_t {
        let mut actions = self.actions.write();
        let old_action = actions[signal.number() as usize];
        actions[signal.number() as usize] = new_action;
        old_action
    }

    pub fn fork(&self) -> Arc<SignalActions> {
        Arc::new(SignalActions { actions: RwLock::new(self.actions.read().clone()) })
    }
}

pub const CLD_EXITED: i32 = 1;
pub const CLD_KILLED: i32 = 2;
pub const CLD_DUMPED: i32 = 3;
pub const CLD_TRAPPED: i32 = 4;
pub const CLD_STOPPED: i32 = 5;
pub const CLD_CONTINUED: i32 = 6;

/// `siginfo_t` is defined here to avoid gnarly bindgen union generation.
#[repr(C)]
#[derive(AsBytes, FromBytes, Debug, Default)]
pub struct siginfo_t {
    pub si_signo: c_int,
    pub si_errno: c_int,
    pub si_code: c_int,
    padding1: [u8; 12],
    pub si_status: c_int,
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_signal() {
        assert!(Signal::try_from(UncheckedSignal::from(0)).is_err());
        assert!(Signal::try_from(UncheckedSignal::from(1)).is_ok());
        assert!(Signal::try_from(UncheckedSignal::from(Signal::NUM_SIGNALS)).is_ok());
        assert!(Signal::try_from(UncheckedSignal::from(Signal::NUM_SIGNALS + 1)).is_err());
        assert!(!Signal::SIGCHLD.is_real_time());
        assert!(Signal::try_from(UncheckedSignal::from(uapi::SIGRTMIN + 12))
            .unwrap()
            .is_real_time());
        assert_eq!(format!("{}", Signal::SIGPWR), "signal 30: SIGPWR");
        assert_eq!(
            format!("{}", Signal::try_from(UncheckedSignal::from(uapi::SIGRTMIN + 10)).unwrap()),
            "signal 42: SIGRTMIN+10"
        );
    }
}
