// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::registry::base::{Command, Notifier, State};
use crate::registry::service_context::ServiceContext;
use crate::switchboard::base::{
    ConfigurationInterfaceFlags, SettingRequest, SettingRequestResponder, SettingResponse,
    SettingType, SetupInfo,
};
use failure::{format_err, Error};
use fuchsia_async as fasync;
use futures::StreamExt;
use std::sync::{Arc, RwLock};

pub struct SetupController {
    service_context_handle: Arc<RwLock<ServiceContext>>,
    interfaces: Option<ConfigurationInterfaceFlags>,
    listen_notifier: Arc<RwLock<Option<Notifier>>>,
}

impl SetupController {
    pub fn spawn(
        service_context_handle: Arc<RwLock<ServiceContext>>,
    ) -> Result<futures::channel::mpsc::UnboundedSender<Command>, Error> {
        let handle = Arc::new(RwLock::new(Self {
            service_context_handle: service_context_handle,
            interfaces: None,
            listen_notifier: Arc::new(RwLock::new(None)),
        }));

        let (ctrl_tx, mut ctrl_rx) = futures::channel::mpsc::unbounded::<Command>();

        let handle_clone = handle.clone();
        fasync::spawn(async move {
            while let Some(command) = ctrl_rx.next().await {
                handle_clone.write().unwrap().process_command(command);
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
                    *self.listen_notifier.write().unwrap() = Some(notifier);
                }
                State::EndListen => {
                    *self.listen_notifier.write().unwrap() = None;
                }
            },
        }
    }

    fn set_interfaces(
        &mut self,
        interfaces: ConfigurationInterfaceFlags,
        responder: SettingRequestResponder,
    ) {
        self.interfaces = Some(interfaces);
        responder.send(Ok(None)).ok();
        if let Some(notifier) = (*self.listen_notifier.read().unwrap()).clone() {
            notifier.unbounded_send(SettingType::Setup).unwrap();
        }
    }

    fn get(&self, responder: SettingRequestResponder) {
        let mut flags = ConfigurationInterfaceFlags::empty();

        if let Some(enabled_interfaces) = self.interfaces {
            flags = flags | enabled_interfaces;
        }

        responder
            .send(Ok(Some(SettingResponse::Setup(SetupInfo { configuration_interfaces: flags }))))
            .ok();
    }
}
