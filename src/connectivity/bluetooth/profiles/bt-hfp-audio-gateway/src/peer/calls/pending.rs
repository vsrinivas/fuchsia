// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//! Pending call updates
use super::{CallIdx, CallState};
use std::collections::hash_map::{Entry, HashMap};
use std::collections::VecDeque;
use std::iter::FromIterator;

/// A Transaction represents a group of requested call state changes that should all complete before
/// the HF is notified that any of the changes in the group have completed.
#[derive(Default, Debug)]
pub struct Transaction {
    states: HashMap<CallIdx, VecDeque<CallState>>,
}

impl From<(CallIdx, CallState)> for Transaction {
    fn from((idx, state): (CallIdx, CallState)) -> Self {
        let states = HashMap::from_iter([(idx, VecDeque::from_iter([state]))]);
        Transaction { states }
    }
}

impl Transaction {
    /// Push a new call state onto the list of pending states in this transaction.
    pub fn pending_call(&mut self, idx: CallIdx, state: CallState) {
        self.states.entry(idx).or_default().push_back(state);
    }

    /// Return true if complete or unexpected and false if partially complete
    fn record(&mut self, call_idx: CallIdx, state: CallState) -> bool {
        match self.states.entry(call_idx) {
            Entry::Vacant(_) => return true,
            Entry::Occupied(mut entry) => {
                let next = entry.get_mut().pop_front().expect("entries not empty");
                if entry.get().is_empty() {
                    let _ = entry.remove();
                }
                if next != state {
                    return true;
                }
            }
        }

        // Check if the transaction is complete.
        self.states.is_empty()
    }

    /// Returns `true` if the Transaction contains no pending calls.
    fn is_empty(&self) -> bool {
        self.states.is_empty()
    }
}

/// A record of outstanding call state changes that the HFP component has requested.
/// Used to keep track of whether indicators should be reported to the HF or not.
#[derive(Default)]
pub struct CallChanges {
    /// Transactions are stored in order from oldest to newest.
    transactions: VecDeque<Transaction>,
    /// This value is true if the set of complete changes requires an indicators report.
    pub should_report: bool,
}

impl CallChanges {
    /// Push a `txn` of multiple pending call changes that should change and be reported as
    /// a group before any subsequent changes.
    ///
    /// Empty Transactions are not added to `CallChanges` because they are not meaningful in any
    /// way.
    pub fn pending_txn(&mut self, txn: Transaction) {
        if !txn.is_empty() {
            self.transactions.push_back(txn);
        }
    }

    /// Push a single pending call change that should change and be
    /// reported before any subsequent changes.
    pub fn pending_call(&mut self, idx: CallIdx, state: CallState) {
        self.transactions.push_back(Transaction::from((idx, state)));
    }

    /// Record that a call state change has been made by the Call Manager.
    // This looks for a matching expected state change in the oldest transaction.
    // It removes the oldest transaction if there is an unexpected state change
    // or all changes in the transaction have been completed.
    pub fn record(&mut self, idx: CallIdx, state: CallState) {
        match self.transactions.front_mut().map(|t| t.record(idx, state)) {
            None => self.should_report = true,
            Some(true) => {
                let _ = self.transactions.pop_front();
                self.should_report = true;
            }
            Some(false) => {}
        }
    }

    /// Return `true` if the call changes require a report, and
    /// clear the report flag.
    pub fn report_now(&mut self) -> bool {
        std::mem::replace(&mut self.should_report, false)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn transaction_is_empty() {
        let mut txn = Transaction::default();
        assert!(txn.is_empty());
        txn.pending_call(1, CallState::OngoingActive);
        assert!(!txn.is_empty());
    }

    #[test]
    fn single_event() {
        let mut pending = CallChanges::default();
        pending.pending_call(1, CallState::OngoingActive);
        assert!(!pending.report_now());
        pending.record(1, CallState::OngoingActive);
        assert!(pending.report_now());
        // check that the report flag is cleared
        assert!(!pending.report_now());
    }

    #[test]
    fn multiple_events() {
        let mut pending = CallChanges::default();
        // Create a transaction and add events to it
        let mut txn = Transaction::default();
        txn.pending_call(1, CallState::OngoingActive);
        txn.pending_call(2, CallState::OngoingActive);
        pending.pending_txn(txn);
        assert!(!pending.report_now());
        pending.record(1, CallState::OngoingActive);
        // not complete until all pending calls are recorded
        assert!(!pending.report_now());
        pending.record(2, CallState::OngoingActive);
        assert!(pending.report_now());
    }

    #[fuchsia::test]
    fn unexpected_record_with_empty_changes_collection() {
        let mut pending = CallChanges::default();
        pending.record(2, CallState::OngoingActive);
        assert!(pending.report_now());
    }

    #[fuchsia::test]
    fn unexpected_records_in_transaction() {
        let mut pending = CallChanges::default();

        // Setup 2 transactions with multiple pending calls each.
        let mut t1 = Transaction::default();
        t1.pending_call(1, CallState::OngoingActive);
        t1.pending_call(2, CallState::OngoingHeld);
        pending.pending_txn(t1);

        let mut t2 = Transaction::default();
        t2.pending_call(1, CallState::Terminated);
        t2.pending_call(2, CallState::OngoingActive);
        t2.pending_call(3, CallState::OngoingHeld);
        pending.pending_txn(t2);

        // Record a call state that is not present in t1 and check
        // that it causes report_now to return true.
        // This call state _is_ present in t2 but that isn't
        // searched because all updates in t1 must be recorded before t2
        pending.record(2, CallState::OngoingActive);
        assert!(pending.report_now());

        // Record a call state that is present in t2 and check
        // that it doesn't cause report_now to return true.
        // This must be the case because t1 was cleared by the unexpected
        // call update so t2 is searched.
        pending.record(2, CallState::OngoingActive);
        assert!(!pending.report_now());

        // Record a call index that is not present in t2 and check
        // that it causes report_now to return true.
        pending.record(4, CallState::Terminated);
        assert!(pending.report_now());
        assert!(!pending.report_now());
    }
}
