// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::registry::base::{Command, Notifier, State},
    crate::switchboard::base::*,
    fuchsia_async as fasync,
    futures::StreamExt,
    std::sync::{Arc, RwLock},
};

pub fn spawn_system_controller() -> futures::channel::mpsc::UnboundedSender<Command> {
    let (system_handler_tx, mut system_handler_rx) = futures::channel::mpsc::unbounded::<Command>();

    // TODO(go/fxb/25465): Replace this stored value with persisted storage once it's available.
    let mut stored_login_override_mode = fidl_fuchsia_settings::LoginOverride::None;

    // TODO(go/fxb/35532): Replace with parking_lot::RwLock.
    let notifier_lock = Arc::<RwLock<Option<Notifier>>>::new(RwLock::new(None));

    fasync::spawn(async move {
        while let Some(command) = system_handler_rx.next().await {
            match command {
                Command::ChangeState(state) => match state {
                    State::Listen(notifier) => {
                        *notifier_lock.write().unwrap() = Some(notifier);
                    }
                    State::EndListen => {
                        *notifier_lock.write().unwrap() = None;
                    }
                },
                Command::HandleRequest(request, responder) =>
                {
                    #[allow(unreachable_patterns)]
                    match request {
                        SettingRequest::SetLoginOverrideMode(mode) => {
                            stored_login_override_mode =
                                fidl_fuchsia_settings::LoginOverride::from(mode);
                            responder.send(Ok(None)).unwrap();

                            {
                                if let Some(notifier) = (*notifier_lock.read().unwrap()).clone() {
                                    notifier
                                        .unbounded_send(SettingType::System)
                                        .expect("failed to send system setting notification");
                                }
                            }
                        }
                        SettingRequest::Get => {
                            responder
                                .send(Ok(Some(SettingResponse::System(SystemInfo {
                                    login_override_mode: SystemLoginOverrideMode::from(
                                        stored_login_override_mode,
                                    ),
                                }))))
                                .unwrap();
                        }
                        _ => panic!("Unexpected command to system"),
                    }
                }
            }
        }
    });
    system_handler_tx
}
