// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(dead_code)]

use crate::lock::RwLock;
use crate::task::{WaitQueue, WaiterRef};
use crate::types::*;
use std::collections::VecDeque;
use std::sync::Arc;
use zerocopy::{AsBytes, FromBytes};

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
        Arc::new(SignalActions { actions: RwLock::new(*self.actions.read()) })
    }

    pub fn reset_for_exec(&self) {
        for action in self.actions.write().iter_mut() {
            if action.sa_handler != SIG_DFL && action.sa_handler != SIG_IGN {
                action.sa_handler = SIG_DFL;
            }
        }
    }
}

/// Per-task signal handling state.
#[derive(Default)]
pub struct SignalState {
    // See https://man7.org/linux/man-pages/man2/sigaltstack.2.html
    pub alt_stack: Option<sigaltstack_t>,

    /// Wait queue for signalfd and sigtimedwait. Signaled whenever a signal is added to the queue.
    pub signal_wait: WaitQueue,

    /// The waiter that the task is currently sleeping on, if any.
    pub waiter: WaiterRef,

    /// The signal mask of the task.
    // See https://man7.org/linux/man-pages/man2/rt_sigprocmask.2.html
    mask: sigset_t,

    /// The queue of signals for a given task.
    ///
    /// There may be more than one instance of a real-time signal in the queue, but for standard
    /// signals there is only ever one instance of any given signal.
    queue: VecDeque<SignalInfo>,
}

impl SignalState {
    /// Sets the signal mask of the state, and returns the old signal mask.
    pub fn set_mask(&mut self, signal_mask: u64) -> u64 {
        let old_mask = self.mask;
        self.mask = signal_mask & !UNBLOCKABLE_SIGNALS;
        old_mask
    }

    pub fn mask(&self) -> u64 {
        self.mask
    }

    pub fn enqueue(&mut self, siginfo: SignalInfo) {
        if siginfo.signal.is_real_time() || !self.has_queued(siginfo.signal) {
            self.queue.push_back(siginfo);
            self.signal_wait.notify_all();
        }
    }

    /// Finds the next queued signal where the given function returns true, removes it from the
    /// queue, and returns it.
    pub fn take_next_where<F>(&mut self, predicate: F) -> Option<SignalInfo>
    where
        F: Fn(&SignalInfo) -> bool,
    {
        // Find the first non-blocked signal
        let index = self.queue.iter().position(predicate)?;
        self.queue.remove(index)
    }

    /// Returns whether any signals are pending (queued and not blocked).
    pub fn is_any_pending(&self) -> bool {
        self.is_any_allowed_by_mask(self.mask)
    }

    /// Returns whether any signals are queued and not blocked by the given mask.
    pub fn is_any_allowed_by_mask(&self, mask: sigset_t) -> bool {
        self.queue.iter().any(|sig| !sig.signal.is_in_set(mask))
    }

    /// Iterates over queued signals with the given number.
    fn iter_queued_by_number(&self, signal: Signal) -> impl Iterator<Item = &SignalInfo> {
        self.queue.iter().filter(move |info| info.signal == signal)
    }

    /// Tests whether a signal with the given number is in the queue.
    pub fn has_queued(&self, signal: Signal) -> bool {
        self.iter_queued_by_number(signal).next().is_some()
    }

    #[cfg(test)]
    pub fn queued_count(&self, signal: Signal) -> usize {
        self.iter_queued_by_number(signal).count()
    }
}

#[derive(Clone, Debug, Default, Eq, PartialEq, AsBytes, FromBytes)]
#[repr(C)]
pub struct SignalInfoHeader {
    pub signo: u32,
    pub errno: i32,
    pub code: i32,
    pub _pad: i32,
}

pub const SI_HEADER_SIZE: usize = std::mem::size_of::<SignalInfoHeader>();

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct SignalInfo {
    pub signal: Signal,
    pub errno: i32,
    pub code: i32,
    pub detail: SignalDetail,
    pub force: bool,
}

impl SignalInfo {
    pub fn default(signal: Signal) -> Self {
        Self::new(signal, SI_KERNEL, SignalDetail::default())
    }

    pub fn new(signal: Signal, code: u32, detail: SignalDetail) -> Self {
        Self { signal, errno: 0, code: code as i32, detail, force: false }
    }

    // TODO(tbodt): Add a bound requiring siginfo_t to be FromBytes. This will help ensure the
    // Linux side won't get an invalid siginfo_t.
    pub fn as_siginfo_bytes(&self) -> [u8; std::mem::size_of::<siginfo_t>()] {
        macro_rules! make_siginfo {
            ($self:ident $(, $sifield:ident, $value:expr)?) => {
                struct_with_union_into_bytes!(siginfo_t {
                    __bindgen_anon_1.__bindgen_anon_1.si_signo: $self.signal.number() as i32,
                    __bindgen_anon_1.__bindgen_anon_1.si_errno: $self.errno,
                    __bindgen_anon_1.__bindgen_anon_1.si_code: $self.code,
                    $(
                        __bindgen_anon_1.__bindgen_anon_1._sifields.$sifield: $value,
                    )?
                })
            };
        }

        match self.detail {
            SignalDetail::None => make_siginfo!(self),
            SignalDetail::SigChld { pid, uid, status } => make_siginfo!(
                self,
                _sigchld,
                __sifields__bindgen_ty_4 {
                    _pid: pid,
                    _uid: uid,
                    _status: status,
                    ..Default::default()
                }
            ),
            SignalDetail::Raw { data } => {
                let header = SignalInfoHeader {
                    signo: self.signal.number(),
                    errno: self.errno,
                    code: self.code,
                    _pad: 0,
                };
                let mut array: [u8; SI_MAX_SIZE as usize] = [0; SI_MAX_SIZE as usize];
                header.write_to(&mut array[..SI_HEADER_SIZE]);
                array[SI_HEADER_SIZE..SI_MAX_SIZE as usize].copy_from_slice(&data);
                array
            }
        }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum SignalDetail {
    None,
    SigChld { pid: pid_t, uid: uid_t, status: i32 },
    Raw { data: [u8; SI_MAX_SIZE as usize - SI_HEADER_SIZE] },
}

impl Default for SignalDetail {
    fn default() -> Self {
        Self::None
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use std::convert::TryFrom;

    #[::fuchsia::test]
    fn test_signal() {
        assert!(Signal::try_from(UncheckedSignal::from(0)).is_err());
        assert!(Signal::try_from(UncheckedSignal::from(1)).is_ok());
        assert!(Signal::try_from(UncheckedSignal::from(Signal::NUM_SIGNALS)).is_ok());
        assert!(Signal::try_from(UncheckedSignal::from(Signal::NUM_SIGNALS + 1)).is_err());
        assert!(!SIGCHLD.is_real_time());
        assert!(Signal::try_from(UncheckedSignal::from(uapi::SIGRTMIN + 12))
            .unwrap()
            .is_real_time());
        assert_eq!(format!("{}", SIGPWR), "signal 30: SIGPWR");
        assert_eq!(
            format!("{}", Signal::try_from(UncheckedSignal::from(uapi::SIGRTMIN + 10)).unwrap()),
            "signal 42: SIGRTMIN+10"
        );
    }

    #[::fuchsia::test]
    fn test_siginfo_bytes() {
        let mut sigchld_bytes =
            vec![17, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 123, 0, 0, 0, 200, 1, 0, 0, 2];
        sigchld_bytes.resize(std::mem::size_of::<siginfo_t>(), 0);
        assert_eq!(
            &SignalInfo::new(
                SIGCHLD,
                CLD_EXITED,
                SignalDetail::SigChld { pid: 123, uid: 456, status: 2 }
            )
            .as_siginfo_bytes(),
            sigchld_bytes.as_slice()
        );
    }
}
