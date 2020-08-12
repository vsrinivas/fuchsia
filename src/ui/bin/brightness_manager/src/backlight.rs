// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context as _, Error};

use async_trait::async_trait;
use fidl_fuchsia_hardware_backlight::{
    DeviceMarker as BacklightMarker, DeviceProxy as BacklightProxy, State as BacklightCommand,
};
use fuchsia_syslog::fx_log_info;

const AUTO_MINIMUM_BRIGHTNESS: f64 = 0.004;

fn open_backlight() -> Result<BacklightProxy, Error> {
    fx_log_info!("Opening backlight");
    let (proxy, server) = fidl::endpoints::create_proxy::<BacklightMarker>()
        .context("Failed to create backlight proxy")?;
    // TODO(kpt): Don't hardcode this path b/138666351
    fdio::service_connect("/dev/class/backlight/000", server.into_channel())
        .context("Failed to connect built-in service")?;
    Ok(proxy)
}

pub struct Backlight {
    proxy: BacklightProxy,
}

impl Backlight {
    pub async fn new() -> Result<Backlight, Error> {
        let proxy = open_backlight()?;

        Ok(Backlight { proxy })
    }

    pub async fn get_max_absolute_brightness(&self) -> Result<f64, Error> {
        let connection_result = self.proxy.get_max_absolute_brightness().await;
        let connection = connection_result
            .map_err(|e| anyhow::format_err!("Didn't connect correctly, got err {}", e))?;
        let max_brightness: f64 = connection.map_err(|e| {
            anyhow::format_err!("Didn't get the max_brightness back, got err {}", e)
        })?;
        Ok(max_brightness)
    }

    async fn get(&self) -> Result<f64, Error> {
        let result = self.proxy.get_state_normalized().await?;
        let backlight_info =
            result.map_err(|e| anyhow::format_err!("Failed to get state: {:?}", e))?;
        assert!(backlight_info.brightness >= 0.0);
        assert!(backlight_info.brightness <= 1.0);
        Ok(if backlight_info.backlight_on { backlight_info.brightness } else { 0.0 })
    }

    fn set(&mut self, value: f64) -> Result<(), Error> {
        // TODO(fxb/36302): Handle error here as well, similar to get_brightness above. Might involve
        let regulated_value = num_traits::clamp(value, AUTO_MINIMUM_BRIGHTNESS, 1.0);
        let _result = self.proxy.set_state_normalized(&mut BacklightCommand {
            backlight_on: value > 0.0,
            brightness: regulated_value,
        });
        Ok(())
    }
}

#[async_trait]
pub trait BacklightControl: Send {
    async fn get_brightness(&self) -> Result<f64, Error>;
    fn set_brightness(&mut self, value: f64) -> Result<(), Error>;
    async fn get_max_absolute_brightness(&self) -> Result<f64, Error>;
}

#[async_trait]
impl BacklightControl for Backlight {
    async fn get_brightness(&self) -> Result<f64, Error> {
        self.get().await
    }
    fn set_brightness(&mut self, value: f64) -> Result<(), Error> {
        self.set(value)
    }
    async fn get_max_absolute_brightness(&self) -> Result<f64, Error> {
        self.get_max_absolute_brightness().await
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl_fuchsia_hardware_backlight::DeviceRequestStream as BacklightRequestStream;
    use fuchsia_async as fasync;
    use futures::prelude::future;
    use futures_util::stream::StreamExt;

    fn mock_backlight() -> (Backlight, BacklightRequestStream) {
        let (proxy, backlight_stream) =
            fidl::endpoints::create_proxy_and_stream::<BacklightMarker>().unwrap();
        (Backlight { proxy }, backlight_stream)
    }

    async fn mock_device_set(mut reqs: BacklightRequestStream) -> BacklightCommand {
        match reqs.next().await.unwrap() {
            Ok(fidl_fuchsia_hardware_backlight::DeviceRequest::SetStateNormalized {
                state: command,
                ..
            }) => {
                return command;
            }
            request => panic!("Unexpected request: {:?}", request),
        }
    }

    async fn mock_device_get(
        mut reqs: BacklightRequestStream,
        backlight_command: BacklightCommand,
    ) {
        match reqs.next().await.unwrap() {
            Ok(fidl_fuchsia_hardware_backlight::DeviceRequest::GetStateNormalized {
                responder,
            }) => {
                fx_log_info!("====== got GetStateNormalized");
                let response = backlight_command;
                let _ = responder.send(&mut Ok(response));
            }
            Ok(fidl_fuchsia_hardware_backlight::DeviceRequest::GetMaxAbsoluteBrightness {
                responder,
            }) => {
                fx_log_info!("====== GetMaxAbsoluteBrightness");
                if let Err(e) = responder.send(&mut Ok(250.0)) {
                    panic!("Failed to reply to GetMaxAbsoluteBrightness: {}", e);
                }
            }
            request => panic!("====== Unexpected request: {:?}", request),
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_brightness_returns_zero_if_screen_off() {
        // Setup
        let (mock, backlight_stream) = mock_backlight();
        let backlight_fut = mock_device_get(
            backlight_stream,
            BacklightCommand { backlight_on: false, brightness: 0.04 },
        );

        // Act
        let mock_fut = mock.get();
        let (brightness, _) = future::join(mock_fut, backlight_fut).await;

        // Assert
        assert_eq!(brightness.unwrap(), 0.0);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_brightness_returns_non_zero_if_screen_on() {
        // Setup
        let (mock, backlight_stream) = mock_backlight();
        let backlight_fut = mock_device_get(
            backlight_stream,
            BacklightCommand { backlight_on: true, brightness: 0.04 },
        );

        // Act
        let mock_fut = mock.get();
        let (brightness, _) = future::join(mock_fut, backlight_fut).await;

        // Assert
        assert_eq!(brightness.unwrap(), 0.04);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_zero_brightness_turns_screen_off() {
        // Setup
        let (mut mock, backlight_stream) = mock_backlight();
        let backlight_fut = mock_device_set(backlight_stream);

        // Act
        mock.set(0.0).expect("set failed");
        let backlight_command = backlight_fut.await;

        // Assert
        assert_eq!(backlight_command.backlight_on, false);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_negative_brightness_turns_screen_off() {
        // Setup
        let (mut mock, backlight_stream) = mock_backlight();
        let backlight_fut = mock_device_set(backlight_stream);

        // Act
        mock.set(-0.01).expect("set failed");
        let backlight_command = backlight_fut.await;

        // Assert
        assert_eq!(backlight_command.backlight_on, false);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_brightness_turns_screen_on() {
        // Setup
        let (mut mock, backlight_stream) = mock_backlight();
        let backlight_fut = mock_device_set(backlight_stream);

        // Act
        mock.set(0.55).expect("set failed");
        let backlight_command = backlight_fut.await;

        // Assert
        assert_eq!(backlight_command.backlight_on, true);
        assert_eq!(backlight_command.brightness, 0.55);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_max_absolute_brightness() {
        // Setup
        let (mock, backlight_stream) = mock_backlight();
        let backlight_fut = mock_device_get(
            backlight_stream,
            BacklightCommand { backlight_on: false, brightness: 0.04 },
        );

        // Act
        let mock_fut = mock.get_max_absolute_brightness();
        let (max_brightness, _) = future::join(mock_fut, backlight_fut).await;

        // Assert
        assert_eq!(max_brightness.unwrap(), 250.0);
    }
}
