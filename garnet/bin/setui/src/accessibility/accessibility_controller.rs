// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::registry::base::{Command, Notifier, State},
    crate::registry::service_context::ServiceContext,
    crate::switchboard::base::{
        AccessibilityInfo, ColorBlindnessType, SettingRequest, SettingResponse, SettingType,
    },
    fidl::endpoints::create_proxy,
    fidl_fuchsia_accessibility::{ColorCorrection, SettingsManagerMarker, SettingsManagerStatus},
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
            let service_result =
                service_context_handle.read().unwrap().connect::<SettingsManagerMarker>();

            let accessibility_service = match service_result {
                Ok(service) => service,
                Err(err) => {
                    return Err(err);
                }
            };

            // Register ourselves as a provider to a11y service to write values.
            let (provider_proxy, server_end) = create_proxy()?;
            accessibility_service.register_setting_provider(server_end)?;

            // Locally remember accessibility info.
            // TODO(go/fxb/25465): persist value.
            let mut stored_accessibility_info: AccessibilityInfo = AccessibilityInfo {
                audio_description: false,
                color_correction: ColorBlindnessType::None,
            };

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
                                let status = provider_proxy
                                    .set_screen_reader_enabled(audio_description.into())
                                    .await?;
                                match status {
                                    SettingsManagerStatus::Ok => {
                                        stored_accessibility_info.audio_description =
                                            audio_description;
                                        let _ = responder.send(Ok(None));

                                        // Notify listeners of value change.
                                        if let Some(notifier) =
                                            (*notifier_lock.read().unwrap()).clone()
                                        {
                                            notifier.unbounded_send(SettingType::Accessibility)?;
                                        }
                                    }
                                    SettingsManagerStatus::Error => {
                                        let _ = responder.send(Err(failure::err_msg(
                                            "error setting value in accessibility service",
                                        )));
                                    }
                                }
                            }
                            SettingRequest::SetColorCorrection(color_correction) => {
                                let status = provider_proxy
                                    .set_color_correction(color_correction.into())
                                    .await?;
                                match status {
                                    SettingsManagerStatus::Ok => {
                                        stored_accessibility_info.color_correction =
                                            color_correction;
                                        let _ = responder.send(Ok(None));

                                        // Notify listeners of value change.
                                        if let Some(notifier) =
                                            (*notifier_lock.read().unwrap()).clone()
                                        {
                                            notifier.unbounded_send(SettingType::Accessibility)?;
                                        }
                                    }
                                    SettingsManagerStatus::Error => {
                                        let _ = responder.send(Err(failure::err_msg(
                                            "error setting value in accessibility service",
                                        )));
                                    }
                                }
                            }
                            SettingRequest::Get => {
                                let _ = responder.send(Ok(Some(SettingResponse::Accessibility(
                                    stored_accessibility_info.clone(),
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

/// Converts from the SetUI-internal ColorBlindnessType to the accessibility service's
/// ColorCorrection.
impl From<ColorBlindnessType> for ColorCorrection {
    fn from(color_blindness_type: ColorBlindnessType) -> Self {
        match color_blindness_type {
            ColorBlindnessType::None => ColorCorrection::Disabled,
            ColorBlindnessType::Protanomaly => ColorCorrection::CorrectProtanomaly,
            ColorBlindnessType::Deuteranomaly => ColorCorrection::CorrectDeuteranomaly,
            ColorBlindnessType::Tritanomaly => ColorCorrection::CorrectTritanomaly,
        }
    }
}
