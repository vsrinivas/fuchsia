// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(dead_code)]

use crate::task::Waiter;
use crate::types::*;
use parking_lot::RwLock;
use std::collections::VecDeque;
use std::sync::Arc;

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

/// Per-task signal handling state.
#[derive(Default)]
pub struct SignalState {
    /// The queue of signals for a given task.
    ///
    /// There may be more than one instance of a real-time signal in the queue, but for standard
    /// signals there is only ever one instance of any given signal.
    pub queue: VecDeque<SignalInfo>,

    // See https://man7.org/linux/man-pages/man2/sigaltstack.2.html
    pub alt_stack: Option<sigaltstack_t>,

    /// The signal mask of the task.
    // See https://man7.org/linux/man-pages/man2/rt_sigprocmask.2.html
    pub mask: sigset_t,

    /// The waiter that the task is currently sleeping on, if any.
    pub waiter: Option<Arc<Waiter>>,
}

impl SignalState {
    /// Sets the signal mask of the state, and returns the old signal mask.
    pub fn set_signal_mask(&mut self, signal_mask: u64) -> u64 {
        let old_mask = self.mask;
        self.mask = signal_mask & !(SIGSTOP.mask() | SIGKILL.mask());
        old_mask
    }

    /// Checks whether the given signal is blocked.
    pub fn is_blocked(&self, signal: Signal) -> bool {
        self.mask & signal.mask() != 0
    }

    pub fn enqueue(&mut self, siginfo: SignalInfo) {
        if siginfo.signal.is_real_time() || !self.has_queued(siginfo.signal) {
            self.queue.push_back(siginfo.clone());
        }
    }

    /// Finds the next pending (queued and not blocked) signal, removes it from the queue, and
    /// returns it.
    pub fn take_next_pending(&mut self) -> Option<SignalInfo> {
        // Find the first non-blocked signal
        let index = self.queue.iter().position(|sig| !self.is_blocked(sig.signal))?;
        self.queue.remove(index)
    }

    /// Returns whether any signals are pending (queued and not blocked).
    pub fn is_any_pending(&self) -> bool {
        self.queue.iter().any(|sig| !self.is_blocked(sig.signal))
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

#[derive(Clone)]
pub struct SignalInfo {
    pub signal: Signal,
    pub errno: u32,
    pub code: u32,
    pub detail: SignalDetail,
}

impl SignalInfo {
    pub fn default(signal: Signal) -> Self {
        Self { signal, errno: 0, code: SI_KERNEL, detail: SignalDetail::default() }
    }
}

#[derive(Clone)]
pub enum SignalDetail {
    None,
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

    #[test]
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
}
