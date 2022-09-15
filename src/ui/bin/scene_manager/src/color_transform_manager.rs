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
    fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::lock::Mutex,
    futures::stream::TryStreamExt,
    std::sync::Arc,
    tracing::{error, info, warn},
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
    current_color_transform: Option<ColorTransformMatrix>,
    current_minimum_rgb: Option<u8>,

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
            current_color_transform: None,
            current_minimum_rgb: None,
            state: ColorTransformState {
                color_inversion_enabled: false,
                color_correction_mode: ColorCorrectionMode::Disabled,
            },
            color_converter,
        }))
    }

    /// Sets the minimum value all pixel channels RGB can be from [0, 255] inclusive.
    /// Debounces duplicate requests.
    async fn set_minimum_rgb(&mut self, minimum_rgb: u8) {
        if self.current_minimum_rgb == Some(minimum_rgb) {
            return;
        }
        self.current_minimum_rgb = Some(minimum_rgb);

        let res = self.color_converter.set_minimum_rgb(minimum_rgb).await;
        match res {
            Ok(true) => {}
            Ok(false) => {
                error!("Error setting display minimum RGB");
            }
            Err(e) => {
                error!("Error calling SetMinimumRgb: {}", e);
            }
        }
    }

    /// Sets the color transform matrix in Scenic.
    /// Debounces duplicate requests.
    async fn set_scenic_color_conversion(&mut self, transform: ColorTransformMatrix) {
        if self.current_color_transform == Some(transform) {
            return;
        }
        self.current_color_transform = Some(transform);

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
                error!("Error setting color conversion");
            }
            Err(e) => {
                error!("Error calling SetValues: {}", e);
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
                                warn!("Ignoring SetColorTransformConfiguration - missing matrix, pre_offset, or post_offset");
                            }
                        };

                        match responder.send() {
                            Ok(_) => {}
                            Err(e) => {
                                error!("Error responding to SetMinimumRgb(): {}", e);
                            }
                        }
                    }
                    Ok(None) => {
                        return;
                    }
                    Err(e) => {
                        error!("Error obtaining ColorTransformHandlerRequest: {}", e);
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
                            info!("Ignoring SetColorAdjustment because color correction is currently active.");
                            continue;
                        }
                        if let Some(matrix) = color_adjustment.matrix {
                            manager.set_scenic_color_conversion(ColorTransformMatrix {
                                matrix,
                                pre_offset: ZERO_OFFSET,
                                post_offset: ZERO_OFFSET,
                            }).await;
                        } else {
                            warn!("Ignoring SetColorAdjustment - `matrix` is empty.");
                        }
                    }
                    Ok(None) => {
                        return;
                    }
                    Err(e) => {
                        error!("Error obtaining ColorAdjustmentHandlerRequest: {}", e);
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
                                error!("Error responding to SetMinimumRgb(): {}", e);
                            }
                        }
                    }
                    Ok(None) => {
                        return;
                    }
                    Err(e) => {
                        error!("Error obtaining ColorAdjustmentHandlerRequest: {}", e);
                        return;
                    }
                };
            }
        })
        .detach()
    }
}

#[cfg(test)]
mod tests {
    use anyhow::Result;
    use fasync::Task;
    use fidl::endpoints::create_proxy_and_stream;
    use fidl_fuchsia_accessibility::{ColorTransformHandlerMarker, ColorTransformHandlerProxy};
    use fidl_fuchsia_ui_brightness::{
        ColorAdjustmentHandlerMarker, ColorAdjustmentHandlerProxy, ColorAdjustmentTable,
    };
    use fidl_fuchsia_ui_display_color::{
        ConversionProperties, ConverterRequest, ConverterRequestStream,
    };
    use fidl_fuchsia_ui_policy::{DisplayBacklightMarker, DisplayBacklightProxy};
    use std::collections::VecDeque;
    use std::future;

    use super::*;

    #[derive(Clone, Default)]
    struct MockConverter {
        state: Arc<Mutex<MockConverterState>>,
    }

    #[derive(Default)]
    struct MockConverterState {
        requests: VecDeque<MockConverterRequest>,
    }

    enum MockConverterRequest {
        SetMinimumRgb(u8),
        SetValues(ConversionProperties),
    }

    impl MockConverter {
        async fn run(&self, reqs: ConverterRequestStream) -> Result<()> {
            reqs.try_for_each(|req| async {
                let mut state = self.state.lock().await;
                match req {
                    ConverterRequest::SetMinimumRgb { minimum_rgb, responder } => {
                        state.requests.push_back(MockConverterRequest::SetMinimumRgb(minimum_rgb));
                        responder.send(true)?;
                    }
                    ConverterRequest::SetValues { properties, responder } => {
                        state.requests.push_back(MockConverterRequest::SetValues(properties));
                        responder.send(zx::sys::ZX_OK)?;
                    }
                }
                Ok(())
            })
            .await?;
            Ok(())
        }

        /// Assert that no new requests have gone out.
        #[track_caller]
        async fn expect_no_requests(&self) {
            assert!(self.state.lock().await.requests.is_empty());
        }

        /// Waits for all background tasks to complete, then assert that no new
        /// requests have gone out.
        #[track_caller]
        fn expect_no_requests_sync(&self, exec: &mut fasync::TestExecutor) {
            let _ = exec.run_until_stalled(&mut future::pending::<()>());

            let lock = self.state.try_lock().unwrap_or_else(|| panic!("Failed to get lock"));
            assert!(lock.requests.is_empty());
        }

        /// Assert that a SetMinimumRgb request went out.
        #[track_caller]
        async fn expect_minimum_rgb(&mut self) -> u8 {
            match self.state.lock().await.requests.pop_front().expect("No more requests") {
                MockConverterRequest::SetValues(_) => {
                    panic!("Expected a SetMinimumRgb request, got a SetValues request");
                }
                MockConverterRequest::SetMinimumRgb(val) => val,
            }
        }

        /// Assert that a SetValues request went out.
        #[track_caller]
        async fn expect_set_values(&mut self) -> ConversionProperties {
            match self.state.lock().await.requests.pop_front().expect("No more requests") {
                MockConverterRequest::SetMinimumRgb(_) => {
                    panic!("Expected a SetValues request, got a SetMinimumRgb request");
                }
                MockConverterRequest::SetValues(properties) => properties,
            }
        }

        /// Waits for all background tasks to complete, then assert that a
        /// SetValues request went out.
        #[track_caller]
        fn expect_set_values_sync(
            &mut self,
            exec: &mut fasync::TestExecutor,
        ) -> ConversionProperties {
            let _ = exec.run_until_stalled(&mut future::pending::<()>());

            let mut lock = self.state.try_lock().unwrap_or_else(|| panic!("Failed to get lock"));
            match lock.requests.pop_front().expect("No more requests") {
                MockConverterRequest::SetMinimumRgb(_) => {
                    panic!("Expected a SetValues request, got a SetMinimumRgb request");
                }
                MockConverterRequest::SetValues(properties) => properties,
            }
        }
    }

    fn init() -> (Arc<Mutex<ColorTransformManager>>, MockConverter) {
        let (client, server) = create_proxy_and_stream::<fidl_color::ConverterMarker>().unwrap();

        let converter = MockConverter::default();
        // Run the converter in the background.
        {
            let converter = converter.clone();
            Task::local(async move {
                converter.run(server).await.unwrap();
            })
            .detach();
        }

        (ColorTransformManager::new(client), converter)
    }

    fn create_display_backlight_stream(
        manager: Arc<Mutex<ColorTransformManager>>,
    ) -> DisplayBacklightProxy {
        let (client, server) = create_proxy_and_stream::<DisplayBacklightMarker>().unwrap();
        super::ColorTransformManager::handle_display_backlight_request_stream(manager, server);
        client
    }

    fn create_color_adjustment_stream(
        manager: Arc<Mutex<ColorTransformManager>>,
    ) -> ColorAdjustmentHandlerProxy {
        let (client, server) = create_proxy_and_stream::<ColorAdjustmentHandlerMarker>().unwrap();
        super::ColorTransformManager::handle_color_adjustment_request_stream(manager, server);
        client
    }

    fn create_color_transform_stream(
        manager: Arc<Mutex<ColorTransformManager>>,
    ) -> ColorTransformHandlerProxy {
        let (client, server) = create_proxy_and_stream::<ColorTransformHandlerMarker>().unwrap();
        super::ColorTransformManager::handle_color_transform_request_stream(manager, server);
        client
    }

    const COLOR_TRANSFORM_CONFIGURATION: ColorTransformConfiguration =
        ColorTransformConfiguration {
            color_inversion_enabled: Some(true),
            color_correction: Some(ColorCorrectionMode::CorrectProtanomaly),
            color_adjustment_matrix: Some([1.; 9]),
            color_adjustment_pre_offset: Some([2.; 3]),
            color_adjustment_post_offset: Some([3.; 3]),
            ..ColorTransformConfiguration::EMPTY
        };

    #[fuchsia::test]
    async fn test_backlight() -> Result<()> {
        let (manager, mut converter) = init();
        let backlight_proxy = create_display_backlight_stream(manager);

        backlight_proxy.set_minimum_rgb(20).await?;
        assert_eq!(converter.expect_minimum_rgb().await, 20);
        backlight_proxy.set_minimum_rgb(30).await?;
        assert_eq!(converter.expect_minimum_rgb().await, 30);

        converter.expect_no_requests().await;
        Ok(())
    }

    #[fuchsia::test]
    async fn test_backlight_debounces() -> Result<()> {
        let (manager, mut converter) = init();
        let backlight_proxy = create_display_backlight_stream(manager);

        backlight_proxy.set_minimum_rgb(20).await?;
        converter.expect_minimum_rgb().await;

        // Setting the same value again is debounced.
        backlight_proxy.set_minimum_rgb(20).await?;
        converter.expect_no_requests().await;

        // Setting a different value isn't.
        backlight_proxy.set_minimum_rgb(30).await?;
        converter.expect_minimum_rgb().await;

        converter.expect_no_requests().await;
        Ok(())
    }

    #[fuchsia::test]
    async fn test_color_transform_manager() -> Result<()> {
        let (manager, mut converter) = init();
        let color_transform_proxy = create_color_transform_stream(Arc::clone(&manager));

        color_transform_proxy
            .set_color_transform_configuration(COLOR_TRANSFORM_CONFIGURATION)
            .await?;
        let properties = converter.expect_set_values().await;
        assert_eq!(properties.coefficients, Some([1.; 9]));
        assert_eq!(properties.preoffsets, Some([2.; 3]));
        assert_eq!(properties.postoffsets, Some([3.; 3]));

        converter.expect_no_requests().await;
        Ok(())
    }

    #[fuchsia::test]
    async fn test_color_transform_manager_debounces() -> Result<()> {
        let (manager, mut converter) = init();
        let color_transform_proxy = create_color_transform_stream(Arc::clone(&manager));

        color_transform_proxy
            .set_color_transform_configuration(COLOR_TRANSFORM_CONFIGURATION)
            .await?;
        converter.expect_set_values().await;

        // Setting the same value again is debounced.
        color_transform_proxy
            .set_color_transform_configuration(COLOR_TRANSFORM_CONFIGURATION)
            .await?;
        converter.expect_no_requests().await;

        // Setting a different value isn't.
        color_transform_proxy
            .set_color_transform_configuration(ColorTransformConfiguration {
                color_adjustment_matrix: Some([0.; 9]),
                ..COLOR_TRANSFORM_CONFIGURATION
            })
            .await?;
        converter.expect_set_values().await;

        converter.expect_no_requests().await;
        Ok(())
    }

    #[fuchsia::test]
    async fn test_color_transform_manager_rejects_bad_requests() -> Result<()> {
        let (manager, converter) = init();
        let color_transform_proxy = create_color_transform_stream(Arc::clone(&manager));

        color_transform_proxy
            .set_color_transform_configuration(ColorTransformConfiguration {
                color_adjustment_matrix: None,
                ..COLOR_TRANSFORM_CONFIGURATION
            })
            .await?;
        converter.expect_no_requests().await;

        color_transform_proxy
            .set_color_transform_configuration(ColorTransformConfiguration {
                color_adjustment_pre_offset: None,
                ..COLOR_TRANSFORM_CONFIGURATION
            })
            .await?;
        converter.expect_no_requests().await;

        color_transform_proxy
            .set_color_transform_configuration(ColorTransformConfiguration {
                color_adjustment_post_offset: None,
                ..COLOR_TRANSFORM_CONFIGURATION
            })
            .await?;
        converter.expect_no_requests().await;

        Ok(())
    }

    #[test]
    fn test_color_adjustment_manager() -> Result<()> {
        let mut exec = fasync::TestExecutor::new().unwrap();
        let (manager, mut converter) = init();
        let color_adjustment_proxy = create_color_adjustment_stream(Arc::clone(&manager));

        color_adjustment_proxy.set_color_adjustment(ColorAdjustmentTable {
            matrix: Some([1.; 9]),
            ..ColorAdjustmentTable::EMPTY
        })?;

        let properties = converter.expect_set_values_sync(&mut exec);
        assert_eq!(properties.coefficients, Some([1.; 9]));
        assert_eq!(properties.preoffsets, Some([0.; 3]));
        assert_eq!(properties.postoffsets, Some([0.; 3]));

        converter.expect_no_requests_sync(&mut exec);
        Ok(())
    }

    #[test]
    fn test_color_adjustment_manager_rejects_bad_requests() -> Result<()> {
        let mut exec = fasync::TestExecutor::new().unwrap();
        let (manager, converter) = init();
        let color_adjustment_proxy = create_color_adjustment_stream(Arc::clone(&manager));

        color_adjustment_proxy.set_color_adjustment(ColorAdjustmentTable::EMPTY)?;

        converter.expect_no_requests_sync(&mut exec);
        Ok(())
    }

    #[test]
    fn test_color_adjustment_manager_noop_when_a11y_active() -> Result<()> {
        let mut exec = fasync::TestExecutor::new().unwrap();
        let (manager, mut converter) = init();
        let color_transform_proxy = create_color_transform_stream(Arc::clone(&manager));
        let color_adjustment_proxy = create_color_adjustment_stream(Arc::clone(&manager));

        let _ =
            color_transform_proxy.set_color_transform_configuration(COLOR_TRANSFORM_CONFIGURATION);
        converter.expect_set_values_sync(&mut exec);

        // SetColorAdjustment is a no-op because a11y settings are active.
        color_adjustment_proxy.set_color_adjustment(ColorAdjustmentTable {
            matrix: Some([4.; 9]),
            ..ColorAdjustmentTable::EMPTY
        })?;
        converter.expect_no_requests_sync(&mut exec);

        // Deactivate a11y settings.
        let _ =
            color_transform_proxy.set_color_transform_configuration(ColorTransformConfiguration {
                color_inversion_enabled: Some(false),
                color_correction: Some(ColorCorrectionMode::Disabled),
                color_adjustment_matrix: Some([0.; 9]),
                color_adjustment_pre_offset: Some([0.; 3]),
                color_adjustment_post_offset: Some([0.; 3]),
                ..ColorTransformConfiguration::EMPTY
            });
        converter.expect_set_values_sync(&mut exec);

        // Now SetColorAdjustment has an effect.
        color_adjustment_proxy.set_color_adjustment(ColorAdjustmentTable {
            matrix: Some([5.; 9]),
            ..ColorAdjustmentTable::EMPTY
        })?;
        converter.expect_set_values_sync(&mut exec);

        converter.expect_no_requests_sync(&mut exec);
        Ok(())
    }
}
