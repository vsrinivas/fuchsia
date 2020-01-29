// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::sync::Arc;

use fuchsia_async as fasync;
use fuchsia_syslog::fx_log_err;
use futures::lock::Mutex;
use futures::stream::StreamExt;
use futures::TryFutureExt;
use parking_lot::RwLock;

use anyhow::Error;

use crate::registry::base::{Command, Notifier, State};
use crate::registry::device_storage::{DeviceStorage, DeviceStorageCompatible};
use crate::switchboard::base::{
    NightModeInfo, SettingRequest, SettingRequestResponder, SettingResponse, SettingType,
    SwitchboardError,
};

type NightModeStorage = Arc<Mutex<DeviceStorage<NightModeInfo>>>;

impl DeviceStorageCompatible for NightModeInfo {
    const KEY: &'static str = "night_mode_info";

    fn default_value() -> Self {
        NightModeInfo { night_mode_enabled: None }
    }
}

pub struct NightModeController {
    stored_value: NightModeInfo,
    listen_notifier: Arc<RwLock<Option<Notifier>>>,
    storage: NightModeStorage,
}

/// Controller for processing switchboard messages for the NightMode protocol.
impl NightModeController {
    pub fn spawn(
        storage: NightModeStorage,
    ) -> Result<futures::channel::mpsc::UnboundedSender<Command>, anyhow::Error> {
        let (ctrl_tx, mut ctrl_rx) = futures::channel::mpsc::unbounded::<Command>();

        fasync::spawn(
            async move {
                // Local copy of persisted night mode value.
                let stored_value: NightModeInfo;
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
                fx_log_err!("Error processing night command: {:?}", e);
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
                    SettingRequest::SetNightModeInfo(night_mode_info) => {
                        self.set_night_mode_enabled(night_mode_info.night_mode_enabled, responder)?;
                    }
                    SettingRequest::Get => {
                        self.get(responder);
                    }
                    _ => panic!("Unexpected command to night_mode: {:?}", request),
                }
            }
        };
        Ok(())
    }

    fn get(&self, responder: SettingRequestResponder) {
        match responder.send(Ok(Some(SettingResponse::NightMode(self.stored_value.clone())))) {
            Ok(_) => {}
            Err(err) => fx_log_err!("Error in responder while sending NightMode: {:?}", err),
        }
    }

    fn set_night_mode_enabled(
        &mut self,
        night_mode_enabled: Option<bool>,
        responder: SettingRequestResponder,
    ) -> Result<(), anyhow::Error> {
        let old_value = self.stored_value.clone();

        if old_value.night_mode_enabled == night_mode_enabled {
            // Value unchanged, no need to persist or notify listeners.
            return Ok(());
        }

        // Save the value locally.
        self.stored_value.night_mode_enabled = night_mode_enabled;

        // Attempt to persist the value.
        self.persist_night_mode_info(self.stored_value, responder);

        // Notify listeners of value change.
        if let Some(notifier) = (*self.listen_notifier.read()).clone() {
            notifier.unbounded_send(SettingType::NightMode)?;
        }

        Ok(())
    }

    fn persist_night_mode_info(&self, info: NightModeInfo, responder: SettingRequestResponder) {
        let storage_clone = self.storage.clone();
        // Spin off a separate thread to persist the value.
        fasync::spawn(async move {
            let mut storage_lock = storage_clone.lock().await;
            let write_request = storage_lock.write(&info, false).await;
            let _ = match write_request {
                Ok(_) => responder.send(Ok(None)),
                Err(err) => responder.send(Err(Error::new(SwitchboardError::StorageFailure {
                    setting_type: SettingType::NightMode,
                    storage_error: err,
                }))),
            };
        });
    }
}
