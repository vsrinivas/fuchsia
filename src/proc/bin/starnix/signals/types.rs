// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(dead_code)]

use crate::types::*;
use std::convert::TryFrom;
use std::fmt;
use std::ops::{Index, IndexMut};

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
#[derive(Debug, Eq, PartialEq)]
pub struct Signal {
    /// The signal number, guaranteed to be a value between 1..=NUM_SIGNALS.
    number: u32,

    /// The name of the signal, if one was supplied when the signal was constructed.
    name: &'static str,
}

impl Signal {
    /// Returns the bitmask for this signal number.
    pub fn mask(&self) -> u64 {
        1 << self.number
    }

    /// The number of signals, also the highest valid signal number.
    pub const NUM_SIGNALS: u32 = 64;

    pub const SIGHUP: Signal = Signal { number: uapi::SIGHUP, name: "SIGHUP" };
    pub const SIGINT: Signal = Signal { number: uapi::SIGINT, name: "SIGINT" };
    pub const SIGQUIT: Signal = Signal { number: uapi::SIGQUIT, name: "SIGQUIT" };
    pub const SIGILL: Signal = Signal { number: uapi::SIGILL, name: "SIGILL" };
    pub const SIGTRAP: Signal = Signal { number: uapi::SIGTRAP, name: "SIGTRAP" };
    pub const SIGABRT: Signal = Signal { number: uapi::SIGABRT, name: "SIGABRT" };
    pub const SIGIOT: Signal = Signal { number: uapi::SIGIOT, name: "SIGIOT" };
    pub const SIGBUS: Signal = Signal { number: uapi::SIGBUS, name: "SIGBUS" };
    pub const SIGFPE: Signal = Signal { number: uapi::SIGFPE, name: "SIGFPE" };
    pub const SIGKILL: Signal = Signal { number: uapi::SIGKILL, name: "SIGKILL" };
    pub const SIGUSR1: Signal = Signal { number: uapi::SIGUSR1, name: "SIGUSR1" };
    pub const SIGSEGV: Signal = Signal { number: uapi::SIGSEGV, name: "SIGSEGV" };
    pub const SIGUSR2: Signal = Signal { number: uapi::SIGUSR2, name: "SIGUSR2" };
    pub const SIGPIPE: Signal = Signal { number: uapi::SIGPIPE, name: "SIGPIPE" };
    pub const SIGALRM: Signal = Signal { number: uapi::SIGALRM, name: "SIGALRM" };
    pub const SIGTERM: Signal = Signal { number: uapi::SIGTERM, name: "SIGTERM" };
    pub const SIGSTKFLT: Signal = Signal { number: uapi::SIGSTKFLT, name: "SIGSTKFLT" };
    pub const SIGCHLD: Signal = Signal { number: uapi::SIGCHLD, name: "SIGCHLD" };
    pub const SIGCONT: Signal = Signal { number: uapi::SIGCONT, name: "SIGCONT" };
    pub const SIGSTOP: Signal = Signal { number: uapi::SIGSTOP, name: "SIGSTOP" };
    pub const SIGTSTP: Signal = Signal { number: uapi::SIGTSTP, name: "SIGTSTP" };
    pub const SIGTTIN: Signal = Signal { number: uapi::SIGTTIN, name: "SIGTTIN" };
    pub const SIGTTOU: Signal = Signal { number: uapi::SIGTTOU, name: "SIGTTOU" };
    pub const SIGURG: Signal = Signal { number: uapi::SIGURG, name: "SIGURG" };
    pub const SIGXCPU: Signal = Signal { number: uapi::SIGXCPU, name: "SIGXCPU" };
    pub const SIGXFSZ: Signal = Signal { number: uapi::SIGXFSZ, name: "SIGXFSZ" };
    pub const SIGVTALRM: Signal = Signal { number: uapi::SIGVTALRM, name: "SIGVTALRM" };
    pub const SIGPROF: Signal = Signal { number: uapi::SIGPROF, name: "SIGPROF" };
    pub const SIGWINCH: Signal = Signal { number: uapi::SIGWINCH, name: "SIGWINCH" };
    pub const SIGIO: Signal = Signal { number: uapi::SIGIO, name: "SIGIO" };
    pub const SIGPOLL: Signal = Signal { number: uapi::SIGPOLL, name: "SIGPOLL" };
    pub const SIGPWR: Signal = Signal { number: uapi::SIGPWR, name: "SIGPWR" };
    pub const SIGSYS: Signal = Signal { number: uapi::SIGSYS, name: "SIGSYS" };
    pub const SIGUNUSED: Signal = Signal { number: uapi::SIGUNUSED, name: "SIGUNUSED" };
    pub const SIGRTMIN: Signal = Signal { number: uapi::SIGRTMIN, name: "SIGRTMIN" };
}

impl TryFrom<UncheckedSignal> for Signal {
    type Error = Errno;

    fn try_from(value: UncheckedSignal) -> Result<Self, Self::Error> {
        let value = u32::try_from(value.0).map_err(|_| EINVAL)?;
        match value {
            uapi::SIGHUP => Ok(Signal::SIGHUP),
            uapi::SIGINT => Ok(Signal::SIGINT),
            uapi::SIGQUIT => Ok(Signal::SIGQUIT),
            uapi::SIGILL => Ok(Signal::SIGILL),
            uapi::SIGTRAP => Ok(Signal::SIGTRAP),
            uapi::SIGABRT => Ok(Signal::SIGABRT),
            // SIGIOT shares the same number as SIGABRT.
            // uapi::SIGIOT => Ok(Signal::SIGIOT),
            uapi::SIGBUS => Ok(Signal::SIGBUS),
            uapi::SIGFPE => Ok(Signal::SIGFPE),
            uapi::SIGKILL => Ok(Signal::SIGKILL),
            uapi::SIGUSR1 => Ok(Signal::SIGUSR1),
            uapi::SIGSEGV => Ok(Signal::SIGSEGV),
            uapi::SIGUSR2 => Ok(Signal::SIGUSR2),
            uapi::SIGPIPE => Ok(Signal::SIGPIPE),
            uapi::SIGALRM => Ok(Signal::SIGALRM),
            uapi::SIGTERM => Ok(Signal::SIGTERM),
            uapi::SIGSTKFLT => Ok(Signal::SIGSTKFLT),
            uapi::SIGCHLD => Ok(Signal::SIGCHLD),
            uapi::SIGCONT => Ok(Signal::SIGCONT),
            uapi::SIGSTOP => Ok(Signal::SIGSTOP),
            uapi::SIGTSTP => Ok(Signal::SIGTSTP),
            uapi::SIGTTIN => Ok(Signal::SIGTTIN),
            uapi::SIGTTOU => Ok(Signal::SIGTTOU),
            uapi::SIGURG => Ok(Signal::SIGURG),
            uapi::SIGXCPU => Ok(Signal::SIGXCPU),
            uapi::SIGXFSZ => Ok(Signal::SIGXFSZ),
            uapi::SIGVTALRM => Ok(Signal::SIGVTALRM),
            uapi::SIGPROF => Ok(Signal::SIGPROF),
            uapi::SIGWINCH => Ok(Signal::SIGWINCH),
            uapi::SIGPOLL => Ok(Signal::SIGPOLL),
            // SIGIO shares the same number as SIGPOLL
            // uapi::SIGIO => Ok(Signal::SIGIO),
            uapi::SIGPWR => Ok(Signal::SIGPWR),
            uapi::SIGSYS => Ok(Signal::SIGSYS),
            uapi::SIGRTMIN..=Signal::NUM_SIGNALS => {
                Ok(Signal { number: u32::from(value), name: "" })
            }
            _ => Err(EINVAL),
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
        write!(f, "signal {}: {}", self.number, self.name)
    }
}

/// `SignalActions` contains a `sigaction_t` for each valid signal.
///
/// Actions can be fetched using a `Signal` as an index.
///
/// let signal = SIGSYS;
/// let action = actions[&signal];
pub struct SignalActions {
    actions: [sigaction_t; Signal::NUM_SIGNALS as usize],
}

impl Default for SignalActions {
    /// Returns a collection of `sigaction_t`s that contains default values for each signal.
    fn default() -> SignalActions {
        // TODO: Populate the default signal actions correctly.
        SignalActions { actions: [sigaction_t::default(); Signal::NUM_SIGNALS as usize] }
    }
}

impl Index<&'_ Signal> for SignalActions {
    type Output = sigaction_t;
    fn index(&self, index: &Signal) -> &sigaction_t {
        &self.actions[index.number as usize]
    }
}

impl IndexMut<&'_ Signal> for SignalActions {
    fn index_mut(&mut self, index: &Signal) -> &mut sigaction_t {
        &mut self.actions[index.number as usize]
    }
}
