// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;
use ffx_core::ffx_command;

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "accessibility")]
/// watch or set accessibility settings
pub struct Accessibility {
    #[argh(subcommand)]
    pub subcommand: SubcommandEnum,
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand)]
pub enum SubcommandEnum {
    AddCaption(CaptionArgs),
    Set(SetArgs),
    Watch(WatchArgs),
}

#[derive(FromArgs, PartialEq, Debug, Clone)]
#[argh(subcommand, name = "add-caption")]
/// add caption options, the configuration for which sources get closed caption and how they look
pub struct CaptionArgs {
    #[argh(option, short = 'm')]
    /// enable closed captions for media sources of audio
    pub for_media: Option<bool>,

    #[argh(option, short = 't')]
    /// enable closed captions for Text-To-Speech sources of audio
    pub for_tts: Option<bool>,

    #[argh(option, short = 'w', from_str_fn(str_to_color))]
    /// border color used around the closed captions window. Valid options are red, green, and blue
    pub window_color: Option<fidl_fuchsia_ui_types::ColorRgba>,

    #[argh(option, short = 'b', from_str_fn(str_to_color))]
    /// border color used around the closed captions window. Valid options are red, green, and blue
    pub background_color: Option<fidl_fuchsia_ui_types::ColorRgba>,

    // CaptionFontStyle options below
    #[argh(option, short = 'f', from_str_fn(str_to_font_family))]
    /// font family for captions as specified by 47 CFR §79.102(k). Valid options are unknown,
    /// monospaced_serif, proportional_serif, monospaced_sans_serif, proportional_sans_serif,
    /// casual, cursive, and small_capitals
    pub font_family: Option<fidl_fuchsia_settings::CaptionFontFamily>,

    #[argh(option, short = 'c', from_str_fn(str_to_color))]
    /// color of the closed caption text. Valid options are red, green, and blue
    pub font_color: Option<fidl_fuchsia_ui_types::ColorRgba>,

    #[argh(option, short = 'r')]
    /// size of closed captions text relative to the default captions size, specified in the
    /// range [0.5, 2] as per 47 CFR §79.103(c)(4)
    pub relative_size: Option<f32>,

    #[argh(option, short = 'e', from_str_fn(str_to_edge_style))]
    /// edge style for fonts as specified in 47 CFR §79.103(c)(7). Valid options are none,
    /// drop_shadow, raised, depressed, outline
    pub char_edge_style: Option<fidl_fuchsia_settings::EdgeStyle>,
}

#[derive(FromArgs, PartialEq, Debug, Clone)]
#[argh(subcommand, name = "set")]
/// set other accessibility options
pub struct SetArgs {
    #[argh(option, short = 'a')]
    /// when set to 'true', will turn on an audio track for videos that includes a description
    /// of what is occurring in the video
    pub audio_description: Option<bool>,

    #[argh(option, short = 's')]
    /// when set to 'true', will read aloud elements of the screen selected by the user
    pub screen_reader: Option<bool>,

    #[argh(option, short = 'i')]
    /// when set to 'true', will invert the colors on the screen
    pub color_inversion: Option<bool>,

    #[argh(option, short = 'm')]
    /// when set to 'true', will interpret triple-taps on the touchscreen as a command to zoom in
    pub enable_magnification: Option<bool>,

    #[argh(option, short = 'c', from_str_fn(str_to_color_blindness_type))]
    /// configures the type of color-blindness to correct for. Valid options are none, protanomaly,
    /// deuteranomaly, and tritanomaly
    pub color_correction: Option<fidl_fuchsia_settings::ColorBlindnessType>,
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "watch")]
/// watch current accessibility settings
pub struct WatchArgs {}

fn str_to_color(src: &str) -> Result<fidl_fuchsia_ui_types::ColorRgba, String> {
    Ok(match src.to_lowercase().as_str() {
        "red" | "r" => {
            fidl_fuchsia_ui_types::ColorRgba { red: 255.0, green: 0.0, blue: 0.0, alpha: 255.0 }
        }
        "green" | "g" => {
            fidl_fuchsia_ui_types::ColorRgba { red: 0.0, green: 2.055, blue: 0.0, alpha: 255.0 }
        }
        "blue" | "b" => {
            fidl_fuchsia_ui_types::ColorRgba { red: 0.0, green: 0.0, blue: 255.0, alpha: 255.0 }
        }
        _ => return Err(String::from("Couldn't parse color")),
    })
}

fn str_to_font_family(src: &str) -> Result<fidl_fuchsia_settings::CaptionFontFamily, String> {
    Ok(match src.to_lowercase().as_str() {
        "unknown" => fidl_fuchsia_settings::CaptionFontFamily::Unknown,
        "monospaced_serif" => fidl_fuchsia_settings::CaptionFontFamily::MonospacedSerif,
        "proportional_serif" => fidl_fuchsia_settings::CaptionFontFamily::ProportionalSerif,
        "monospaced_sans_serif" => fidl_fuchsia_settings::CaptionFontFamily::MonospacedSansSerif,
        "proportional_sans_serif" => {
            fidl_fuchsia_settings::CaptionFontFamily::ProportionalSansSerif
        }
        "casual" => fidl_fuchsia_settings::CaptionFontFamily::Casual,
        "cursive" => fidl_fuchsia_settings::CaptionFontFamily::Cursive,
        "small_capitals" => fidl_fuchsia_settings::CaptionFontFamily::SmallCapitals,
        _ => return Err(String::from("Couldn't parse font family")),
    })
}

fn str_to_edge_style(src: &str) -> Result<fidl_fuchsia_settings::EdgeStyle, String> {
    Ok(match src.to_lowercase().as_str() {
        "none" => fidl_fuchsia_settings::EdgeStyle::None,
        "drop_shadow" => fidl_fuchsia_settings::EdgeStyle::DropShadow,
        "raised" => fidl_fuchsia_settings::EdgeStyle::Raised,
        "depressed" => fidl_fuchsia_settings::EdgeStyle::Depressed,
        "outline" => fidl_fuchsia_settings::EdgeStyle::Outline,
        _ => return Err(String::from("Couldn't parse edge style")),
    })
}

fn str_to_color_blindness_type(
    src: &str,
) -> Result<fidl_fuchsia_settings::ColorBlindnessType, String> {
    match src.to_lowercase().as_str() {
        "none" | "n" => Ok(fidl_fuchsia_settings::ColorBlindnessType::None),
        "protanomaly" | "p" => Ok(fidl_fuchsia_settings::ColorBlindnessType::Protanomaly),
        "deuteranomaly" | "d" => Ok(fidl_fuchsia_settings::ColorBlindnessType::Deuteranomaly),
        "tritanomaly" | "t" => Ok(fidl_fuchsia_settings::ColorBlindnessType::Tritanomaly),
        _ => Err(String::from("Couldn't parse color blindness type")),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    const CMD_NAME: &'static [&'static str] = &["accessibility"];

    #[test]
    fn test_accessibility_add_caption_cmd() {
        // Test input arguments are generated to according struct.
        let window_color = "red";
        let font_family = "casual";
        let char_edge_style = "drop_shadow";
        let args = &["add-caption", "-w", window_color, "-f", font_family, "-e", char_edge_style];
        assert_eq!(
            Accessibility::from_args(CMD_NAME, args),
            Ok(Accessibility {
                subcommand: SubcommandEnum::AddCaption(CaptionArgs {
                    for_media: None,
                    for_tts: None,
                    window_color: Some(str_to_color(window_color).unwrap()),
                    background_color: None,
                    font_family: Some(str_to_font_family(font_family).unwrap()),
                    font_color: None,
                    relative_size: None,
                    char_edge_style: Some(str_to_edge_style(char_edge_style).unwrap()),
                })
            })
        )
    }

    #[test]
    fn test_accessibility_set_cmd() {
        // Test input arguments are generated to according struct.
        let color_correction = "protanomaly";
        let args = &["set", "-c", color_correction];
        assert_eq!(
            Accessibility::from_args(CMD_NAME, args),
            Ok(Accessibility {
                subcommand: SubcommandEnum::Set(SetArgs {
                    audio_description: None,
                    screen_reader: None,
                    color_inversion: None,
                    enable_magnification: None,
                    color_correction: Some(str_to_color_blindness_type(color_correction).unwrap())
                })
            })
        )
    }

    #[test]
    fn test_accessibility_watch_cmd() {
        // Test input arguments are generated to according struct.
        let args = &["watch"];
        assert_eq!(
            Accessibility::from_args(CMD_NAME, args),
            Ok(Accessibility { subcommand: SubcommandEnum::Watch(WatchArgs {}) })
        )
    }
}
