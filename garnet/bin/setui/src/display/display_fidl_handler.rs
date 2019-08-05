// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::switchboard::base::*,
    crate::switchboard::switchboard_impl::SwitchboardImpl,
    fidl_fuchsia_settings::*,
    fuchsia_async as fasync,
    futures::prelude::*,
    std::sync::{Arc, RwLock},
};

pub fn spawn_display_fidl_handler(
    switchboard_handle: Arc<RwLock<SwitchboardImpl>>,
    mut stream: DisplayRequestStream,
) {
    println!("Started from the bottom");
    let switchboard_lock = switchboard_handle.clone();

    // Start listening to changes to display when connected.
    {
        let mut switchboard = switchboard_lock.write().unwrap();
        let (listen_tx, _listen_rx) = futures::channel::mpsc::unbounded::<SettingType>();
        switchboard.listen(SettingType::Display, listen_tx).unwrap();
    }

    fasync::spawn(async move {
        while let Ok(Some(req)) = await!(stream.try_next()) {
            // Support future expansion of FIDL
            #[allow(unreachable_patterns)]
            match req {
                DisplayRequest::Set { settings, responder } => {
                    let mut request = None;
                    if let Some(brightness_value) = settings.brightness_value {
                        request = Some(SettingRequest::SetBrightness(brightness_value));
                    } else if let Some(enable_auto_brightness) = settings.auto_brightness {
                        request = Some(SettingRequest::SetAutoBrightness(enable_auto_brightness));
                    }

                    if let Some(request) = request {
                        let (response_tx, _response_rx) =
                            futures::channel::oneshot::channel::<SettingResponseResult>();
                        let mut switchboard = switchboard_lock.write().unwrap();
                        let result =
                            switchboard.request(SettingType::Display, request, response_tx);

                        match result {
                            Ok(_) => responder.send(&mut Ok(())).unwrap(),
                            Err(_err) => responder.send(&mut Err(Error::Unsupported)).unwrap(),
                        }
                    } else {
                        responder.send(&mut Err(Error::Unsupported)).unwrap();
                    }
                }
                DisplayRequest::Watch { responder } => {
                    // TODO(ejia): support hanging get
                    let (response_tx, response_rx) =
                        futures::channel::oneshot::channel::<SettingResponseResult>();

                    {
                        let mut switchboard = switchboard_lock.write().unwrap();

                        switchboard
                            .request(SettingType::Display, SettingRequest::Get, response_tx)
                            .unwrap();
                    }
                    if let Ok(Some(SettingResponse::Brightness(info))) =
                        await!(response_rx).unwrap()
                    {
                        let mut display_settings = fidl_fuchsia_settings::DisplaySettings::empty();

                        match info {
                            BrightnessInfo::ManualBrightness(value) => {
                                display_settings.brightness_value = Some(value);
                                display_settings.auto_brightness = Some(false);
                            }
                            BrightnessInfo::AutoBrightness => {
                                display_settings.auto_brightness = Some(true);
                            }
                        }

                        responder.send(&mut Ok(display_settings)).unwrap();
                    } else {
                        panic!("incorrect value sent to display");
                    }
                }
                _ => {}
            }
        }
    });
}
