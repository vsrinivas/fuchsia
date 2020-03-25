// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

use fidl_fuchsia_logger::{LogListenerProxy, LogMessage};
use std::collections::HashSet;

pub(super) struct ListenerWrapper {
    pub listener: LogListenerProxy,
    pub min_severity: Option<i32>,
    pub pid: Option<u64>,
    pub tid: Option<u64>,
    pub tags: HashSet<String>,
}

#[derive(PartialEq)]
pub(super) enum ListenerStatus {
    Fine,
    Stale,
}

impl ListenerWrapper {
    pub fn filter(&self, log_message: &mut LogMessage) -> bool {
        if self.pid.map(|pid| pid != log_message.pid).unwrap_or(false)
            || self.tid.map(|tid| tid != log_message.tid).unwrap_or(false)
            || self.min_severity.map(|min_sev| min_sev > log_message.severity).unwrap_or(false)
        {
            return false;
        }

        if !self.tags.is_empty() {
            if !log_message.tags.iter().any(|tag| self.tags.contains(tag)) {
                return false;
            }
        }
        return true;
    }

    /// This fn assumes that logs have already been filtered.
    pub fn send_filtered_logs(&self, log_messages: &mut Vec<&mut LogMessage>) -> ListenerStatus {
        if let Err(e) = self.listener.log_many(&mut log_messages.iter_mut().map(|x| &mut **x)) {
            if e.is_closed() {
                ListenerStatus::Stale
            } else {
                eprintln!("Error calling listener: {:?}", e);
                ListenerStatus::Fine
            }
        } else {
            ListenerStatus::Fine
        }
    }

    pub fn send_log(&self, log_message: &mut LogMessage) -> ListenerStatus {
        if !self.filter(log_message) {
            return ListenerStatus::Fine;
        }
        if let Err(e) = self.listener.log(log_message) {
            if e.is_closed() {
                ListenerStatus::Stale
            } else {
                eprintln!("Error calling listener: {:?}", e);
                ListenerStatus::Fine
            }
        } else {
            ListenerStatus::Fine
        }
    }
}
