// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::anyhow, async_utils::hanging_get::server as hanging_get,
    fidl_fuchsia_ui_composition as ui_comp, fidl_fuchsia_ui_pointerinjector as ui_pointerinjector,
    fidl_fuchsia_ui_pointerinjector_configuration as ui_pointerinjector_config,
};

/// Minimal spec that can be used to generate a fuchsia.ui.pointerinjector.Viewport,
/// for the purposes of implementing fuchsia.ui.pointerinjector.configuration.Setup API.
/// The info here is not sufficient to fully populate a fucushai.ui.pointerinjector.Viewport;
/// more fields may need to be added to support additional use cases.
#[derive(Copy, Clone)]
pub struct InjectorViewportSpec {
    pub width: f32,
    pub height: f32,
}

/// Conversion from InjectorViewportSpec -> fuchsia.ui.pointerinjector.Viewport.
impl std::convert::From<InjectorViewportSpec> for ui_pointerinjector::Viewport {
    fn from(spec: InjectorViewportSpec) -> Self {
        ui_pointerinjector::Viewport {
            // TODO(fxbug.dev/87640): need non-full-screen injection, eg. for A11y magnification.
            extents: Some([[0.0, 0.0], [spec.width, spec.height]]),
            // TODO(fxbug.dev/87640): need non-full-screen injection, eg. for A11y magnification.
            viewport_to_context_transform: Some([1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0]),
            ..ui_pointerinjector::Viewport::EMPTY
        }
    }
}

/// Conversion from fuchsia.ui.composition.LayoutInfo -> InjectorViewportSpec
impl std::convert::TryFrom<ui_comp::LayoutInfo> for InjectorViewportSpec {
    type Error = anyhow::Error;

    fn try_from(layout_info: ui_comp::LayoutInfo) -> Result<Self, Self::Error> {
        let pixel_scale =
            layout_info.pixel_scale.ok_or(anyhow!("LayoutInfo must have pixel_scale"))?;
        let logical_size =
            layout_info.logical_size.ok_or(anyhow!("LayoutInfo must have logical_size"))?;

        Ok(InjectorViewportSpec {
            width: (logical_size.width * pixel_scale.width) as f32,
            height: (logical_size.height * pixel_scale.height) as f32,
        })
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
