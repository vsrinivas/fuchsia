// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// These constants are taken from the UX wireframe mockups
// TODO(b/259491503): SPlit constants into app specific and general UI building blocks
#[allow(unused)]
pub mod constants {
    use carnelian::color::Color;
    use euclid::{size2, Size2D, UnknownUnit};

    // Width of left hand column in generic view
    #[derive(Debug, Clone, PartialEq)]
    pub enum ScreenSplit {
        None = 100,
        Wide = 75,
        Even = 50,
    }

    impl ScreenSplit {
        pub fn as_percent(&self) -> f32 {
            self.clone() as u8 as f32 / 100.0
        }
    }

    const UX_SCALING_FACTOR: f32 = 1.0;

    pub const MIN_SPACE: f32 = 1.0;

    pub const BACKGROUND_WHITE: Color = Color { r: 0xff, g: 0xff, b: 0xff, a: 0xff };
    pub const BACKGROUND_GREY: Color = Color { r: 0xf8, g: 0xf9, b: 0xfa, a: 0xff };

    pub const BUTTON_COLOR: Color = Color { r: 0x1a, g: 0x73, b: 0xeb, a: 0xff };
    pub const BUTTON_TEXT_COLOR: Color = BACKGROUND_WHITE;
    pub const BUTTON_BORDER_COLOR: Color = Color { r: 0xda, g: 0xdc, b: 0xe0, a: 0xff };
    pub const BUTTON_DISABLED_COLOR: Color = Color { r: 0xc0, g: 0xc0, b: 0xc0, a: 0xff };
    pub const ACTIVE_BUTTON_COLOR: Color = Color { r: 0x15, g: 0x5c, b: 0xba, a: 0xff };
    pub const ACTIVE_BUTTON_COLOR_SWAPPED: Color = Color { r: 0x00, g: 0x00, b: 0x00, a: 0x33 };
    pub const ADD_NETWORK_BUTTON_COLOR: Color = Color { r: 0x1a, g: 0x73, b: 0xe8, a: 0xff };

    pub const KEY_SPECIAL_COLOR: Color = Color { r: 0xe0, g: 0xff, b: 0xf8, a: 0xff };

    pub const BUTTON_BORDER: f32 = 15.0;
    pub const BUTTON_SPACE: f32 = 15.0;
    pub const BORDER_WIDTH: f32 = 2.0;

    pub const LARGE_BUTTON_FONT_SIZE: f32 = 32.0;
    pub const SMALL_BUTTON_FONT_SIZE: f32 = 24.0;

    // Screen text values
    pub const TITLE_COLOR: Color = Color { r: 0x3c, g: 0x40, b: 0x43, a: 0xff };
    pub const TITLE_FONT_SIZE: f32 = 45.0;
    pub const TITLE_SMALL_FONT_SIZE: f32 = 32.0;
    pub const TEXT_COLOR: Color = Color { r: 0x5f, g: 0x63, b: 0x68, a: 0xff };
    pub const TEXT_FONT_SIZE: f32 = 24.0;
    pub const PERMISSION_FONT_SIZE: f32 = 20.0;
    pub const THIN_LINE_COLOR: Color = Color { r: 0xda, g: 0xdc, b: 0xe0, a: 0xff };

    pub const TEXT_FIELD_FONT_SIZE: f32 = 28.0;
    pub const TEXT_FIELD_TITLE_SIZE: f32 = 20.0;

    // The panel on the right with the colourful G
    pub const G_LOGO_SIZE: f32 = 50.0;
    pub const G_TITLE_FONT_SIZE: f32 = 16.0;
    pub const G_TEXT_FONT_SIZE: f32 = 14.0;

    // All the icons reside in a RIVE file and are accessed by name within that file
    // https://rive.app/
    // The sizes are taken from UX documents
    pub const LOGO_PATH: &str = "/pkg/data/logo.riv";
    pub const ICONS_PATH: &str = "/pkg/data/ota_icons.riv";
    pub const IMAGE_DEVICE_INSTALL: &str = "device_install.svg";
    pub const IMAGE_DEFAULT_SIZE: Size2D<f32, UnknownUnit> = size2(428.0, 428.0);
    pub const IMAGE_DEVICE_UPDATING: &str = "device_updating.svg";
    pub const IMAGE_DEVICE_UPDATING_SIZE: Size2D<f32, UnknownUnit> = size2(296.21, 170.66);
    pub const IMAGE_G_LOGO: &str = "G_logo.svg";
    pub const ICON_ADD: &str = "icon_add.svg";
    pub const ICON_ADD_SIZE: Size2D<f32, UnknownUnit> = size2(32.0, 32.0);
    pub const ICON_ARROW_BACK: &str = "icon_arrow_back.svg";
    pub const ICON_ARROW_BACK_DISABLED: &str = "icon_arrow_back_disabled.svg";
    pub const ICON_ARROW_FORWARD: &str = "icon_arrow_forward.svg";
    pub const ICON_ARROW_FORWARD_DISABLED: &str = "icon_arrow_forward_disabled.svg";
    pub const ICON_ARROW_SIZE: Size2D<f32, UnknownUnit> = size2(24.0, 24.0);
    pub const ICON_FACTORY_RESET: &str = "icon_factory_reset.svg";
    pub const ICON_LOCK: &str = "icon_lock.svg";
    pub const ICON_LOCK_SIZE: Size2D<f32, UnknownUnit> = size2(24.0, 24.0);
    pub const ICON_PASSWORD_VISIBLE: &str = "icon_password_visibility.svg";
    pub const ICON_PASSWORD_INVISIBLE: &str = "icon_password_visibility_off.svg";
    pub const ICON_PASSWORD_VISIBLE_SIZE: Size2D<f32, UnknownUnit> = size2(30.0, 30.0);
    pub const ICON_POWER: &str = "icon_power.svg";
    pub const ICON_REINSTALL_SOFTWARE: &str = "icon_reinstall_software.svg";
    pub const ICON_WIFI_FULL_SIGNAL: &str = "icon_wifi_full_signal.svg";
    pub const ICON_WIFI_MID_SIGNAL: &str = "icon_wifi_mid_signal_1.svg";
    pub const ICON_WIFI_NO_SIGNAL: &str = "icon_wifi_no_signal.svg";
    pub const ICON_WIFI_WEAK_SIGNAL: &str = "icon_wifi_weak_signal_1.svg";
    pub const ICON_WIFI_SIZE: Size2D<f32, UnknownUnit> = size2(24.0, 24.0);
    pub const IMAGE_OTA_FAILED: &str = "OTA_failed.svg";
    pub const IMAGE_OTA_FAILED_SIZE: Size2D<f32, UnknownUnit> = size2(10.0, 10.0);
    pub const IMAGE_PHONE_HOME_APP: &str = "phone_Home_app.svg";
    pub const IMAGE_PHONE_HOME_APP_SIZE: Size2D<f32, UnknownUnit> = size2(167.66, 348.46);
    pub const IMAGE_RESET_FAILED: &str = "reset_failed.svg";
    pub const IMAGE_DEVICE_CONNECT: &str = "device_connect.svg";
    pub const IMAGE_SWITCH_OFF: &str = "Switch_off.svg";
    pub const IMAGE_SWITCH_ON: &str = "Switch_on.svg";
    pub const IMAGE_SWITCH_SIZE: Size2D<f32, UnknownUnit> = size2(72.0, 72.0);

    pub const QR_PATH: &str = "/pkg/data/qr_codes.riv";
    pub const QR_CODE: &str = "qr_fuchsia.svg";
    pub const QR_CODE_SIZE: Size2D<f32, UnknownUnit> = size2(80.0, 80.0);

    // Spacing for most screens
    pub const TOP_SPACE: f32 = 100.0;
    pub const LEFT_MARGIN_SPACE: f32 = 60.0;
    pub const AFTER_TITLE_SPACE: f32 = 20.0;
    pub const AFTER_TEXT_SPACE: f32 = 60.0;
    pub const AFTER_MAIN_BUTTON_SPACE: f32 = 130.0;

    pub const OPTIONAL_REPORT_TEXT: &'static str =
        "Send optional crash reports \nto help improve the experience";
}
