// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::convert::TryFrom;
use std::fmt;
use std::hash::{Hash, Hasher};

use crate::types::*;
use crate::{errno, error};

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
#[derive(Debug, Copy, Clone, PartialEq, Eq)]
pub struct Signal {
    number: u32,
}

impl Hash for Signal {
    fn hash<H: Hasher>(&self, state: &mut H) {
        self.number.hash(state);
    }
}

impl Signal {
    /// The signal number, guaranteed to be a value between 1..=NUM_SIGNALS.
    pub fn number(&self) -> u32 {
        self.number
    }

    /// Returns the bitmask for this signal number.
    pub fn mask(&self) -> u64 {
        1 << (self.number - 1)
    }

    /// Returns whether the signal is in the specified signal set.
    pub fn is_in_set(&self, set: sigset_t) -> bool {
        set & self.mask() != 0
    }

    /// Returns true if the signal is a real-time signal.
    pub fn is_real_time(&self) -> bool {
        self.number >= uapi::SIGRTMIN
    }

    /// Returns true if this signal can't be blocked. This means either SIGKILL or SIGSTOP.
    pub fn is_unblockable(&self) -> bool {
        self.number == uapi::SIGKILL || self.number == uapi::SIGSTOP
    }

    /// The number of signals, also the highest valid signal number.
    pub const NUM_SIGNALS: u32 = 64;
}

pub const SIGHUP: Signal = Signal { number: uapi::SIGHUP };
pub const SIGINT: Signal = Signal { number: uapi::SIGINT };
pub const SIGQUIT: Signal = Signal { number: uapi::SIGQUIT };
pub const SIGILL: Signal = Signal { number: uapi::SIGILL };
pub const SIGTRAP: Signal = Signal { number: uapi::SIGTRAP };
pub const SIGABRT: Signal = Signal { number: uapi::SIGABRT };
#[allow(dead_code)]
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
#[allow(dead_code)]
pub const SIGRTMIN: Signal = Signal { number: uapi::SIGRTMIN };

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
