// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::registry::base::{Command, Notifier, State},
    crate::registry::service_context::ServiceContext,
    crate::switchboard::base::{AccessibilityInfo, SettingRequest, SettingResponse, SettingType},
    failure::format_err,
    fidl::endpoints::create_proxy,
    fidl_fuchsia_accessibility::{SettingsManagerMarker, SettingsManagerStatus},
    fuchsia_async as fasync,
    fuchsia_syslog::fx_log_err,
    futures::stream::StreamExt,
    futures::TryFutureExt,
    std::sync::{Arc, RwLock},
};

/// Controller that handles commands for SettingType::Accessibility.
/// TODO(fxb/35252): store persistently
pub fn spawn_accessibility_controller(
    service_context_handle: Arc<RwLock<ServiceContext>>,
) -> futures::channel::mpsc::UnboundedSender<Command> {
    let (accessibility_handler_tx, mut accessibility_handler_rx) =
        futures::channel::mpsc::unbounded::<Command>();

    // TODO(fxb/35532): switch to parking_lot
    let notifier_lock = Arc::<RwLock<Option<Notifier>>>::new(RwLock::new(None));

    fasync::spawn(
        async move {
            // Locally persist audio description value.
            // TODO(go/fxb/25465): persist value.
            let mut stored_audio_description: bool = false;

            while let Some(command) = accessibility_handler_rx.next().await {
                match command {
                    Command::ChangeState(state) => match state {
                        State::Listen(notifier) => {
                            *notifier_lock.write().unwrap() = Some(notifier);
                        }
                        State::EndListen => {
                            *notifier_lock.write().unwrap() = None;
                        }
                    },
                    Command::HandleRequest(request, responder) => {
                        #[allow(unreachable_patterns)]
                        match request {
                            SettingRequest::SetAudioDescription(audio_description) => {
                                let service_result = service_context_handle
                                    .read()
                                    .unwrap()
                                    .connect::<SettingsManagerMarker>();

                                let accessibility_service = match service_result {
                                    Ok(service) => service,
                                    Err(err) => {
                                        let _ = responder.send(Err(format_err!(
                                            "Error getting accessibility service: {:?}",
                                            err
                                        )));
                                        return Ok(());
                                    }
                                };

                                // Register ourselves as a provider to a11y service to write values.
                                let (provider_proxy, server_end) = create_proxy()?;
                                let register_result =
                                    accessibility_service.register_setting_provider(server_end);
                                match register_result {
                                    Ok(_) => {
                                        let status = provider_proxy
                                            .set_screen_reader_enabled(audio_description.into())
                                            .await?;
                                        match status {
                                            SettingsManagerStatus::Ok => {
                                                stored_audio_description = audio_description;
                                                let _ = responder.send(Ok(None));
                                            }
                                            SettingsManagerStatus::Error => {
                                                let _ = responder.send(Err(format_err!(
                                                    "error setting value in accessibility service"
                                                )));
                                            }
                                        }
                                        if let Some(notifier) =
                                            (*notifier_lock.read().unwrap()).clone()
                                        {
                                            notifier.unbounded_send(SettingType::Accessibility)?;
                                        }
                                    }
                                    Err(err) => {
                                        let _ = responder.send(Err(format_err!(
                                            "Error registering as settings provider: {:?}",
                                            err
                                        )));
                                    }
                                }
                            }
                            SettingRequest::Get => {
                                let _ = responder.send(Ok(Some(SettingResponse::Accessibility(
                                    AccessibilityInfo::AudioDescription(stored_audio_description),
                                ))));
                            }
                            _ => panic!("Unexpected command to accessibility"),
                        }
                    }
                }
            }
            Ok(())
        }
            .unwrap_or_else(|e: failure::Error| {
                fx_log_err!("Error processing accessibility command: {:?}", e)
            }),
    );
    accessibility_handler_tx
}
