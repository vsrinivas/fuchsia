// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::common::*;
use crate::fidl_clone::*;
use fidl_fuchsia_setui::*;
use std::sync::mpsc::Sender;
use std::sync::Mutex;

/// SettingAdapter provides a basic implementation of the Adapter trait,
/// handling callbacks for hanging get interactions and keeping track of the
/// latest values. Users of this implementation can provide a callback for
/// processing mutations.
pub struct SettingAdapter {
    setting_type: SettingType,
    latest_val: Option<SettingData>,
    senders: Mutex<Vec<Sender<SettingData>>>,
    mutation_process: Box<ProcessMutation>,
}

impl SettingAdapter {
    pub fn new(
        setting_type: SettingType,
        mutation_processor: Box<ProcessMutation>,
        default_value: Option<SettingData>,
    ) -> SettingAdapter {
        return SettingAdapter {
            setting_type: setting_type,
            latest_val: default_value,
            senders: Mutex::new(vec![]),
            mutation_process: mutation_processor,
        };
    }
}

impl Adapter for SettingAdapter {
    fn get_type(&self) -> SettingType {
        return self.setting_type;
    }

    fn mutate(&mut self, mutation: &fidl_fuchsia_setui::Mutation) -> MutationResponse {
        let result = (self.mutation_process)(mutation);

        match result {
            Ok(Some(setting)) => {
                self.latest_val = Some(setting.clone());

                if let Ok(mut senders) = self.senders.lock() {
                    while !senders.is_empty() {
                        let sender_option = senders.pop();

                        if let Some(sender) = sender_option {
                            sender.send(setting.clone()).ok();
                        }
                    }
                }
            }
            Ok(None) => {
                self.latest_val = None;
            }
            _ => {
                return MutationResponse { return_code: ReturnCode::Failed };
            }
        }

        return MutationResponse { return_code: ReturnCode::Ok };
    }

    /// Listen
    fn listen(&self, sender: Sender<SettingData>, _last_seen_data: Option<&SettingData>) {
        if let Some(ref value) = self.latest_val {
            sender.send(value.clone()).ok();
        } else {
            if let Ok(mut senders) = self.senders.lock() {
                senders.push(sender);
            }
        }
    }
}
