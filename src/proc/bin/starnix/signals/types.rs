// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(dead_code)]

use crate::errno;
use crate::error;
use crate::types::*;
use std::collections::HashMap;
use std::convert::TryFrom;
use std::fmt;
use std::hash::{Hash, Hasher};
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
    /// The signal number, guaranteed to be a value between 1..=NUM_SIGNALS.
    pub number: u32,

    /// The name of the signal, if one was supplied when the signal was constructed.
    name: &'static str,
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
        let value = u32::try_from(value.0).map_err(|_| errno!(EINVAL))?;
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
            _ => error!(EINVAL),
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

/// A SignalAction represents the action to take when signal is delivered.
///
/// See https://man7.org/linux/man-pages/man7/signal.7.html.
#[derive(PartialEq, Debug)]
pub enum SignalAction {
    /// Execution is resumed if the process is currently stopped.
    Cont,
    /// The process is terminated after generating a core dump file.
    Core,
    /// The handler in `sigaction_t` is invoked.
    Custom(sigaction_t),
    /// The signal is ignored.
    Ignore,
    /// The process is suspended.
    Stop,
    /// The process is terminated.
    Term,
}

/// `SignalActions` contains a `sigaction_t` for each valid signal.
///
/// The current action can be retrieved, and signal actions can be updated as follows:
///   - A signal action can be ignored by calling `ignore`.
///   - A signal action can be set to a custom handler via `set_handler`.
///   - A signal action can be restored to its default via `reset`.
#[derive(Debug)]
pub struct SignalActions {
    actions: HashMap<Signal, SignalAction>,
}

impl Default for SignalActions {
    /// Returns a collection of `sigaction_t`s that contains default values for each signal.
    fn default() -> SignalActions {
        let actions = (SIGHUP..Signal::NUM_SIGNALS).map(|sig_num| {
            let unchecked = UncheckedSignal::from(sig_num);
            // This unwrap is safe since all values between SIGHUP and SIGRTMIN are valid signals.
            let signal = Signal::try_from(unchecked).unwrap();
            let default_action = default_signal_action(&signal);
            (signal, default_action)
        });

        SignalActions { actions: actions.collect() }
    }
}

impl SignalActions {
    /// Returns the `SignalAction` that is currently set for `signal`.
    pub fn get(&self, signal: &Signal) -> &SignalAction {
        // This is safe, since the actions always contain a value for each signal.
        self.actions.get(signal).unwrap()
    }

    /// Resets the action for `signal` to the default for that signal.
    pub fn reset(&mut self, signal: &Signal) {
        *self.actions.get_mut(signal).unwrap() = default_signal_action(signal);
    }

    /// Sets the action of `signal` to be `SignalAction::Ignore`.
    pub fn ignore(&mut self, signal: &Signal) {
        *self.actions.get_mut(signal).unwrap() = SignalAction::Ignore;
    }

    /// Sets the action of `signal` to be the provided `handler`.
    pub fn set_handler(&mut self, signal: &Signal, handler: sigaction_t) {
        *self.actions.get_mut(signal).unwrap() = SignalAction::Custom(handler);
    }
}

// Returns the default action for `signal`.
//
// See https://man7.org/linux/man-pages/man7/signal.7.html.
pub fn default_signal_action(signal: &Signal) -> SignalAction {
    match signal.number {
        SIGHUP => SignalAction::Term,
        SIGINT => SignalAction::Term,
        SIGQUIT => SignalAction::Core,
        SIGILL => SignalAction::Core,
        SIGABRT => SignalAction::Core,
        SIGFPE => SignalAction::Core,
        SIGKILL => SignalAction::Term,
        SIGSEGV => SignalAction::Core,
        SIGPIPE => SignalAction::Term,
        SIGALRM => SignalAction::Term,
        SIGTERM => SignalAction::Term,
        SIGUSR1 => SignalAction::Term,
        SIGUSR2 => SignalAction::Term,
        SIGCHLD => SignalAction::Ignore,
        SIGCONT => SignalAction::Cont,
        SIGSTOP => SignalAction::Stop,
        SIGTSTP => SignalAction::Stop,
        SIGTTIN => SignalAction::Stop,
        SIGTTOU => SignalAction::Stop,
        SIGBUS => SignalAction::Core,
        SIGPROF => SignalAction::Term,
        SIGSYS => SignalAction::Core,
        SIGTRAP => SignalAction::Core,
        SIGURG => SignalAction::Ignore,
        SIGVTALRM => SignalAction::Term,
        SIGXCPU => SignalAction::Core,
        SIGXFSZ => SignalAction::Core,
        SIGSTKFLT => SignalAction::Term,
        SIGIO => SignalAction::Term,
        SIGPWR => SignalAction::Term,
        SIGWINCH => SignalAction::Ignore,
        SIGRTMIN..=Signal::NUM_SIGNALS => SignalAction::Ignore,
        _ => panic!("Getting default value for invalid signal"),
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
mod tests {
    use super::*;

    #[test]
    fn test_defaults() {
        assert_eq!(default_signal_action(&Signal::SIGABRT), SignalAction::Core);
        assert_eq!(default_signal_action(&Signal::SIGPWR), SignalAction::Term);
        assert_eq!(default_signal_action(&Signal::SIGSEGV), SignalAction::Core);
        assert_eq!(default_signal_action(&Signal::SIGCONT), SignalAction::Cont);
        assert_eq!(default_signal_action(&Signal::SIGPOLL), SignalAction::Term);
        assert_eq!(default_signal_action(&Signal::SIGTTIN), SignalAction::Stop);
        assert_eq!(default_signal_action(&Signal::SIGCHLD), SignalAction::Ignore);
        assert_eq!(default_signal_action(&Signal::SIGURG), SignalAction::Ignore);
    }
}
