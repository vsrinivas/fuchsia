// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::sync::Arc;

use fuchsia_async as fasync;
use fuchsia_syslog::fx_log_err;
use futures::lock::Mutex;
use futures::stream::StreamExt;
use futures::TryFutureExt;
use parking_lot::RwLock;

use crate::registry::base::{Command, Notifier, State};
use crate::registry::device_storage::{DeviceStorage, DeviceStorageCompatible};
use crate::switchboard::base::{
    PrivacyInfo, SettingRequest, SettingRequestResponder, SettingResponse, SettingType,
};

type PrivacyStorage = Arc<Mutex<DeviceStorage<PrivacyInfo>>>;

impl DeviceStorageCompatible for PrivacyInfo {
    const KEY: &'static str = "privacy_info";

    fn default_value() -> Self {
        PrivacyInfo { user_data_sharing_consent: None }
    }
}

pub struct PrivacyController {
    stored_value: PrivacyInfo,
    listen_notifier: Arc<RwLock<Option<Notifier>>>,
    storage: PrivacyStorage,
}

/// Controller for processing switchboard messages for the Privacy protocol.
impl PrivacyController {
    pub fn spawn(
        storage: PrivacyStorage,
    ) -> Result<futures::channel::mpsc::UnboundedSender<Command>, anyhow::Error> {
        let (ctrl_tx, mut ctrl_rx) = futures::channel::mpsc::unbounded::<Command>();

        fasync::spawn(
            async move {
                // Local copy of persisted privacy value.
                let stored_value: PrivacyInfo;
                {
                    let mut storage_lock = storage.lock().await;
                    stored_value = storage_lock.get().await;
                }

                let handle = Arc::new(RwLock::new(Self {
                    stored_value: stored_value,
                    listen_notifier: Arc::new(RwLock::new(None)),
                    storage: storage,
                }));

                while let Some(command) = ctrl_rx.next().await {
                    handle.write().process_command(command)?;
                }
                Ok(())
            }
            .unwrap_or_else(|e: anyhow::Error| {
                fx_log_err!("Error processing privacy command: {:?}", e);
            }),
        );

        return Ok(ctrl_tx);
    }

    fn process_command(&mut self, command: Command) -> Result<(), anyhow::Error> {
        match command {
            Command::ChangeState(state) => match state {
                State::Listen(notifier) => {
                    *self.listen_notifier.write() = Some(notifier);
                }
                State::EndListen => {
                    *self.listen_notifier.write() = None;
                }
            },
            Command::HandleRequest(request, responder) =>
            {
                #[allow(unreachable_patterns)]
                match request {
                    SettingRequest::SetUserDataSharingConsent(user_data_sharing_consent) => {
                        self.set_user_data_sharing_consent(user_data_sharing_consent, responder)?;
                    }
                    SettingRequest::Get => {
                        self.get(responder);
                    }
                    _ => panic!("Unexpected command to privacy"),
                }
            }
        };
        Ok(())
    }

    fn get(&self, responder: SettingRequestResponder) {
        let _ = responder.send(Ok(Some(SettingResponse::Privacy(self.stored_value.clone()))));
    }

    fn set_user_data_sharing_consent(
        &mut self,
        user_data_sharing_consent: Option<bool>,
        responder: SettingRequestResponder,
    ) -> Result<(), anyhow::Error> {
        let old_value = self.stored_value.clone();

        // Save the value locally.
        self.stored_value.user_data_sharing_consent = user_data_sharing_consent;

        if old_value == self.stored_value {
            // Value unchanged, no need to persist or notify listeners.
            return Ok(());
        }

        // Attempt to persist the value.
        self.persist_privacy_info(self.stored_value, responder);

        // Notify listeners of value change.
        if let Some(notifier) = (*self.listen_notifier.read()).clone() {
            notifier.unbounded_send(SettingType::Privacy)?;
        }

        Ok(())
    }

    fn persist_privacy_info(&self, info: PrivacyInfo, responder: SettingRequestResponder) {
        let storage_clone = self.storage.clone();
        // Spin off a separate thread to persist the value.
        fasync::spawn(async move {
            let mut storage_lock = storage_clone.lock().await;
            let write_request = storage_lock.write(&info, false).await;
            let _ = match write_request {
                Ok(_) => responder.send(Ok(None)),
                Err(err) => responder
                    .send(Err(anyhow::format_err!("failed to persist privacy_info: {}", err))),
            };
        });
    }
}
