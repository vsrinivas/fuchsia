// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::registry::base::{Command, Notifier, State};
use crate::registry::device_storage::{DeviceStorage, DeviceStorageCompatible};
use crate::switchboard::base::{
    ConfigurationInterfaceFlags, SettingRequest, SettingRequestResponder, SettingResponse,
    SettingType, SetupInfo,
};
use failure::{format_err, Error};
use fuchsia_async as fasync;
use futures::lock::Mutex;
use futures::StreamExt;
use parking_lot::RwLock;
use std::sync::Arc;

impl DeviceStorageCompatible for SetupInfo {
    const DEFAULT_VALUE: Self =
        SetupInfo { configuration_interfaces: ConfigurationInterfaceFlags::DEFAULT };
    const KEY: &'static str = "setup_info";
}

pub struct SetupController {
    info: SetupInfo,
    listen_notifier: Arc<RwLock<Option<Notifier>>>,
    storage: Arc<Mutex<DeviceStorage<SetupInfo>>>,
}

impl SetupController {
    pub fn spawn(
        storage: Arc<Mutex<DeviceStorage<SetupInfo>>>,
    ) -> Result<futures::channel::mpsc::UnboundedSender<Command>, Error> {
        let (ctrl_tx, mut ctrl_rx) = futures::channel::mpsc::unbounded::<Command>();

        fasync::spawn(async move {
            let stored_value: SetupInfo;
            {
                let mut storage_lock = storage.lock().await;
                stored_value = storage_lock.get().await;
            }

            let handle = Arc::new(RwLock::new(Self {
                info: stored_value,
                listen_notifier: Arc::new(RwLock::new(None)),
                storage: storage,
            }));

            while let Some(command) = ctrl_rx.next().await {
                handle.write().process_command(command);
            }
        });

        return Ok(ctrl_tx);
    }

    fn process_command(&mut self, command: Command) {
        match command {
            Command::HandleRequest(request, responder) => match request {
                SettingRequest::SetConfigurationInterfaces(interfaces) => {
                    self.set_interfaces(interfaces, responder);
                }
                SettingRequest::Get => {
                    self.get(responder);
                }
                _ => {
                    responder.send(Err(format_err!("unimplemented"))).ok();
                }
            },
            Command::ChangeState(state) => match state {
                State::Listen(notifier) => {
                    *self.listen_notifier.write() = Some(notifier);
                }
                State::EndListen => {
                    *self.listen_notifier.write() = None;
                }
            },
        }
    }

    fn set_interfaces(
        &mut self,
        interfaces: ConfigurationInterfaceFlags,
        responder: SettingRequestResponder,
    ) {
        self.info.configuration_interfaces = interfaces;
        responder.send(Ok(None)).ok();
        if let Some(notifier) = (*self.listen_notifier.read()).clone() {
            notifier.unbounded_send(SettingType::Setup).unwrap();
        }

        let storage_clone = self.storage.clone();
        let info = self.info;
        fasync::spawn(async move {
            let mut storage_lock = storage_clone.lock().await;
            storage_lock.write(info, false).await.unwrap();
        });
    }

    fn get(&self, responder: SettingRequestResponder) {
        responder.send(Ok(Some(SettingResponse::Setup(self.info)))).ok();
    }
}
