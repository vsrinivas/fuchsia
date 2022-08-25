// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_accessibility::{
        ColorCorrectionMode, ColorTransformConfiguration, ColorTransformHandlerRequest,
        ColorTransformHandlerRequestStream,
    },
    fidl_fuchsia_ui_brightness::{
        ColorAdjustmentHandlerRequest, ColorAdjustmentHandlerRequestStream,
    },
    fidl_fuchsia_ui_display_color as fidl_color,
    fidl_fuchsia_ui_policy::{DisplayBacklightRequest, DisplayBacklightRequestStream},
    fuchsia_async as fasync,
    fuchsia_syslog::{fx_log_err, fx_log_info},
    fuchsia_zircon as zx,
    futures::lock::Mutex,
    futures::TryStreamExt,
    std::sync::Arc,
};

const ZERO_OFFSET: [f32; 3] = [0., 0., 0.];

/// ColorTransformManager serves these protocols:
/// - fuchsia.ui.policy.DisplayBacklight
/// - fuchsia.accessibility.ColorTransformHandler
/// - fuchsia.ui.brightness.ColorAdjustmentHandler (Note: it ignores these
///   requests if a11y color correction is currently applied.)
///
/// It makes outgoing calls to fuchsia.ui.display.color.Converter.
pub struct ColorTransformManager {
    state: ColorTransformState,
    prev_color_transform: Option<ColorTransformMatrix>,

    // Used to set color correction on displays, as well as brightness.
    color_converter: fidl_color::ConverterProxy,
}

#[derive(PartialEq, Clone, Copy)]
struct ColorTransformMatrix {
    matrix: [f32; 9],
    pre_offset: [f32; 3],
    post_offset: [f32; 3],
}

struct ColorTransformState {
    color_inversion_enabled: bool,
    color_correction_mode: ColorCorrectionMode,
}

impl ColorTransformState {
    fn is_active(&self) -> bool {
        self.color_inversion_enabled || self.color_correction_mode != ColorCorrectionMode::Disabled
    }

    fn update(&mut self, configuration: ColorTransformConfiguration) {
        if let Some(color_inversion_enabled) = configuration.color_inversion_enabled {
            self.color_inversion_enabled = color_inversion_enabled
        }

        if let Some(color_correction_mode) = configuration.color_correction {
            self.color_correction_mode = color_correction_mode
        }
    }
}

impl ColorTransformManager {
    pub fn new(color_converter: fidl_color::ConverterProxy) -> Arc<Mutex<Self>> {
        Arc::new(Mutex::new(Self {
            prev_color_transform: None,
            state: ColorTransformState {
                color_inversion_enabled: false,
                color_correction_mode: ColorCorrectionMode::Disabled,
            },
            color_converter,
        }))
    }

    /// Sets the minimum value all pixel channels RGB can be from [0, 255] inclusive.
    async fn set_minimum_rgb(&self, minimum_rgb: u8) {
        let res = self.color_converter.set_minimum_rgb(minimum_rgb).await;
        match res {
            Ok(true) => {}
            Ok(false) => {
                fx_log_err!("Error setting display minimum RGB");
            }
            Err(e) => {
                fx_log_err!("Error calling SetMinimumRgb: {}", e);
            }
        }
    }

    async fn set_scenic_color_conversion(&mut self, transform: ColorTransformMatrix) {
        if self.prev_color_transform == Some(transform) {
            return;
        }
        self.prev_color_transform = Some(transform);

        let res = self
            .color_converter
            .set_values(fidl_color::ConversionProperties {
                coefficients: Some(transform.matrix),
                preoffsets: Some(transform.pre_offset),
                postoffsets: Some(transform.post_offset),
                ..fidl_color::ConversionProperties::EMPTY
            })
            .await;
        match res {
            Ok(zx::sys::ZX_OK) => {}
            Ok(_) => {
                fx_log_err!("Error setting color conversion");
            }
            Err(e) => {
                fx_log_err!("Error calling SetValues: {}", e);
            }
        }
    }

    pub fn handle_color_transform_request_stream(
        manager: Arc<Mutex<Self>>,
        mut request_stream: ColorTransformHandlerRequestStream,
    ) {
        fasync::Task::local(async move {
            loop {
                let request = request_stream.try_next().await;
                match request {
                    Ok(Some(ColorTransformHandlerRequest::SetColorTransformConfiguration{configuration, responder})) => {
                        match (configuration.color_adjustment_matrix, configuration.color_adjustment_pre_offset, configuration.color_adjustment_post_offset) {
                            (Some(matrix), Some(pre_offset), Some(post_offset)) => {
                                let transform = ColorTransformMatrix{matrix, pre_offset, post_offset};
                                let mut manager = manager.lock().await;
                                manager.state.update(configuration);
                                manager.set_scenic_color_conversion(transform).await;
                            }
                            _ => {
                                fx_log_err!("ColorTransformConfiguration missing matrix, pre_offset, or post_offset");
                            }
                        };

                        match responder.send() {
                            Ok(_) => {}
                            Err(e) => {
                                fx_log_err!("Error responding to SetMinimumRgb(): {}", e);
                            }
                        }
                    }
                    Ok(None) => {
                        return;
                    }
                    Err(e) => {
                        fx_log_err!("Error obtaining ColorTransformHandlerRequest: {}", e);
                        return;
                    }
                }
            }
        })
        .detach()
    }

    pub fn handle_color_adjustment_request_stream(
        manager: Arc<Mutex<Self>>,
        mut request_stream: ColorAdjustmentHandlerRequestStream,
    ) {
        fasync::Task::local(async move {
            loop {
                let request = request_stream.try_next().await;
                match request {
                    Ok(Some(ColorAdjustmentHandlerRequest::SetColorAdjustment{ color_adjustment, control_handle: _})) => {
                        let mut manager = manager.lock().await;
                        if manager.state.is_active() {
                            fx_log_info!("Ignoring SetColorAdjustment because color correction is currently active.");
                            continue;
                        }
                        if let Some(matrix) = color_adjustment.matrix {
                            manager.set_scenic_color_conversion(ColorTransformMatrix {
                                matrix,
                                pre_offset: ZERO_OFFSET,
                                post_offset: ZERO_OFFSET,
                            }).await;
                        } else {
                            fx_log_info!("Ignoring SetColorAdjustment because `matrix` is empty.");
                        }
                    }
                    Ok(None) => {
                        return;
                    }
                    Err(e) => {
                        fx_log_err!("Error obtaining ColorAdjustmentHandlerRequest: {}", e);
                        return;
                    }
                }
            }
        })
        .detach()
    }

    pub fn handle_display_backlight_request_stream(
        manager: Arc<Mutex<Self>>,
        mut request_stream: DisplayBacklightRequestStream,
    ) {
        fasync::Task::local(async move {
            loop {
                let request = request_stream.try_next().await;
                match request {
                    Ok(Some(DisplayBacklightRequest::SetMinimumRgb { minimum_rgb, responder })) => {
                        manager.lock().await.set_minimum_rgb(minimum_rgb).await;
                        match responder.send() {
                            Ok(_) => {}
                            Err(e) => {
                                fx_log_err!("Error responding to SetMinimumRgb(): {}", e);
                            }
                        }
                    }
                    Ok(None) => {
                        return;
                    }
                    Err(e) => {
                        fx_log_err!("Error obtaining ColorAdjustmentHandlerRequest: {}", e);
                        return;
                    }
                };
            }
        })
        .detach()
    }
}
