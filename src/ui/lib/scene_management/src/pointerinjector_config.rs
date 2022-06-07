// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    async_utils::hanging_get::server as hanging_get,
    fidl_fuchsia_ui_pointerinjector as ui_pointerinjector,
    fidl_fuchsia_ui_pointerinjector_configuration as ui_pointerinjector_config,
};

/// Minimal spec that can be used to generate a fuchsia.ui.pointerinjector.Viewport,
/// for the purposes of implementing fuchsia.ui.pointerinjector.configuration.Setup API.
#[derive(Copy, Clone)]
pub struct InjectorViewportSpec {
    pub width: f32,
    pub height: f32,
    pub scale: f32,
    pub x_offset: f32,
    pub y_offset: f32,
}

/// Conversion from InjectorViewportSpec -> fuchsia.ui.pointerinjector.Viewport.
impl std::convert::From<InjectorViewportSpec> for ui_pointerinjector::Viewport {
    fn from(spec: InjectorViewportSpec) -> Self {
        ui_pointerinjector::Viewport {
            extents: Some([[0.0, 0.0], [spec.width, spec.height]]),
            viewport_to_context_transform: Some([
                // Same transform as: https://cs.opensource.google/fuchsia/fuchsia/+/main:src/ui/input/lib/injector/injector.cc;drc=af2ffe6ce432b6e6f050a7c2d62e9e5fc2b3e3f2;l=315
                spec.scale,
                0.,
                0.,
                0.,
                spec.scale,
                0.,
                spec.x_offset,
                spec.y_offset,
                1.,
            ]),
            ..ui_pointerinjector::Viewport::EMPTY
        }
    }
}

/// Used to implement fuchsia.ui.pointerinjector.configuration.Setup.WatchViewport().
pub type InjectorViewportChangeFn = Box<
    dyn Fn(&InjectorViewportSpec, ui_pointerinjector_config::SetupWatchViewportResponder) -> bool
        + Send
        + Sync,
>;

/// Used to implement fuchsia.ui.pointerinjector.configuration.Setup.WatchViewport().
pub type InjectorViewportHangingGet = hanging_get::HangingGet<
    InjectorViewportSpec,
    ui_pointerinjector_config::SetupWatchViewportResponder,
    InjectorViewportChangeFn,
>;

/// Used to implement fuchsia.ui.pointerinjector.configuration.Setup.WatchViewport().
pub type InjectorViewportPublisher = hanging_get::Publisher<
    InjectorViewportSpec,
    ui_pointerinjector_config::SetupWatchViewportResponder,
    InjectorViewportChangeFn,
>;

/// Used to implement fuchsia.ui.pointerinjector.configuration.Setup.WatchViewport().
pub type InjectorViewportSubscriber = hanging_get::Subscriber<
    InjectorViewportSpec,
    ui_pointerinjector_config::SetupWatchViewportResponder,
    InjectorViewportChangeFn,
>;
