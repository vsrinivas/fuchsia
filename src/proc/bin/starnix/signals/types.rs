// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(dead_code)]

use crate::task::{WaitQueue, Waiter};
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
    queue: VecDeque<SignalInfo>,

    /// Wait queue for signalfd. Signaled whenever a signal is added to the queue.
    pub signalfd_wait: WaitQueue,

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

    pub fn enqueue(&mut self, siginfo: SignalInfo) {
        if siginfo.signal.is_real_time() || !self.has_queued(siginfo.signal) {
            self.queue.push_back(siginfo.clone());
            self.signalfd_wait.notify_all();
        }
    }

    /// Finds the next queued signal that is not blocked by the given mask, removes it from the
    /// queue, and returns it.
    pub fn take_next_allowed_by_mask(&mut self, mask: sigset_t) -> Option<SignalInfo> {
        // Find the first non-blocked signal
        let index = self.queue.iter().position(|sig| !sig.signal.is_in_set(mask))?;
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

#[derive(Clone, Debug)]
pub struct SignalInfo {
    pub signal: Signal,
    pub errno: i32,
    pub code: i32,
    pub detail: SignalDetail,
}

impl SignalInfo {
    pub fn default(signal: Signal) -> Self {
        Self::new(signal, SI_KERNEL, SignalDetail::default())
    }

    pub fn new(signal: Signal, code: u32, detail: SignalDetail) -> Self {
        Self { signal, errno: 0, code: code as i32, detail }
    }

    // TODO(tbodt): Add a bound requiring siginfo_t to be FromBytes. This will help ensure the
    // Linux side won't get an invalid siginfo_t.
    pub fn as_siginfo_bytes(&self) -> [u8; std::mem::size_of::<siginfo_t>()] {
        /// Initializes the given fields of a struct or union and returns the bytes of the
        /// resulting object as a byte array.
        ///
        /// `struct_union_into_bytes` is invoked like so:
        ///
        /// ```rust,ignore
        /// union Foo {
        ///     a: u8,
        ///     b: u16,
        /// }
        ///
        /// struct Bar {
        ///     a: Foo,
        ///     b: u8,
        ///     c: u16,
        /// }
        ///
        /// struct_union_into_bytes!(Bar { a.b: 1, b: 2, c: 3 })
        /// ```
        ///
        /// Each named field is initialized with a value whose type must implement
        /// `zerocopy::AsBytes`. Any fields which are not explicitly initialized will be left as
        /// all zeroes.
        macro_rules! struct_union_into_bytes {
            ($ty:ident { $($($field:ident).*: $value:expr,)* }) => {{
                use std::mem::MaybeUninit;

                const BYTES: usize = std::mem::size_of::<$ty>();

                struct AlignedBytes {
                    bytes: [u8; BYTES],
                    _align: MaybeUninit<$ty>,
                }

                let mut bytes = AlignedBytes { bytes: [0; BYTES], _align: MaybeUninit::uninit() };

                $({
                    // Evaluate `$value` once to make sure it has the same type
                    // when passed to `type_check_as_bytes` as when assigned to
                    // the field.
                    let value = $value;
                    if false {
                        fn type_check_as_bytes<T: zerocopy::AsBytes>(_: T) {
                            unreachable!()
                        }
                        type_check_as_bytes(value);
                    } else {
                        // SAFETY: We only treat these zeroed bytes as a `$ty` for the purposes of
                        // overwriting the given field. Thus, it's OK if a sequence of zeroes is
                        // not a valid instance of `$ty` or if the sub-sequence of zeroes is not a
                        // valid instance of the type of the field being overwritten. Note that we
                        // use `std::ptr::write`, not normal field assignment, as the latter would
                        // treat the current field value (all zeroes) as an initialized instance of
                        // the field's type (in order to drop it), which would be unsound.
                        //
                        // Since we know from the preceding `if` branch that the type of `value` is
                        // `AsBytes`, we know that no uninitialized bytes will be written to the
                        // field. That, combined with the fact that the entire `bytes.bytes` is
                        // initialized to zero, ensures that all bytes of `bytes.bytes` are
                        // initialized, so we can safely return `bytes.bytes` as a byte array.
                        unsafe {
                            std::ptr::write(&mut (&mut *(&mut bytes.bytes as *mut [u8; BYTES] as *mut $ty)).$($field).*, value);
                        }
                    }
                })*

                bytes.bytes
            }};
        }

        macro_rules! make_siginfo {
            ($self:ident $(, $sifield:ident, $value:expr)?) => {
                struct_union_into_bytes!(siginfo_t {
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
        }
    }
}

#[derive(Clone, Debug)]
pub enum SignalDetail {
    None,
    SigChld { pid: pid_t, uid: uid_t, status: i32 },
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

    #[test]
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
