// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::colors::ColorScheme,
    anyhow::Error,
    carnelian::drawing::DisplayRotation,
    fidl_fuchsia_boot::{ArgumentsProxy, BoolPair},
    std::str::FromStr,
};

pub const MIN_FONT_SIZE: f32 = 15.0;
pub const MAX_FONT_SIZE: f32 = 150.0;

const DEFAULT_KEYMAP: &'static str = "US_QWERTY";
const DEFAULT_FONT_SIZE: f32 = 15.0;
const DEFAULT_SCROLLBACK_ROWS: u32 = 1024;
const DEFAULT_BUFFER_COUNT: usize = 1;

#[derive(Debug, Default)]
pub struct VirtualConsoleArgs {
    pub disable: bool,
    pub keep_log_visible: bool,
    pub keyrepeat: bool,
    pub rounded_corners: bool,
    pub boot_animation: bool,
    pub color_scheme: ColorScheme,
    pub keymap: String,
    pub display_rotation: DisplayRotation,
    pub font_size: f32,
    pub dpi: Vec<u32>,
    pub scrollback_rows: u32,
    pub buffer_count: usize,
}

impl VirtualConsoleArgs {
    pub async fn new_with_proxy(boot_args: ArgumentsProxy) -> Result<VirtualConsoleArgs, Error> {
        let mut bool_keys = [
            BoolPair { key: "virtcon.disable".to_string(), defaultval: false },
            BoolPair { key: "virtcon.keep-log-visible".to_string(), defaultval: false },
            BoolPair { key: "virtcon.keyrepeat".to_string(), defaultval: true },
            BoolPair { key: "virtcon.rounded_corners".to_string(), defaultval: false },
            BoolPair { key: "virtcon.boot_animation".to_string(), defaultval: false },
        ];
        let bool_key_refs: Vec<_> = bool_keys.iter_mut().collect();
        let mut disable = false;
        let mut keep_log_visible = false;
        let mut keyrepeat = true;
        let mut rounded_corners = false;
        let mut boot_animation = false;
        if let Ok(values) = boot_args.get_bools(&mut bool_key_refs.into_iter()).await {
            disable = values[0];
            keep_log_visible = values[1];
            keyrepeat = values[2];
            rounded_corners = values[3];
            boot_animation = values[4];
        }

        let string_keys = vec![
            "virtcon.colorscheme",
            "virtcon.keymap",
            "virtcon.display_rotation",
            "virtcon.font_size",
            "virtcon.dpi",
            "virtcon.scrollback_rows",
            "virtcon.buffer_count",
        ];
        let mut color_scheme = ColorScheme::default();
        let mut keymap = DEFAULT_KEYMAP.to_string();
        let mut display_rotation = DisplayRotation::default();
        let mut font_size = DEFAULT_FONT_SIZE;
        let mut dpi = vec![];
        let mut scrollback_rows = DEFAULT_SCROLLBACK_ROWS;
        let mut buffer_count = DEFAULT_BUFFER_COUNT;
        if let Ok(values) = boot_args.get_strings(&mut string_keys.into_iter()).await {
            if let Some(value) = values[0].as_ref() {
                color_scheme = ColorScheme::from_str(value)?;
            }
            if let Some(value) = values[1].as_ref() {
                keymap = match value.as_str() {
                    "qwerty" => "US_QWERTY",
                    "dvorak" => "US_DVORAK",
                    _ => value,
                }
                .to_string();
            }
            if let Some(value) = values[2].as_ref() {
                display_rotation = DisplayRotation::from_str(value)?;
            }
            if let Some(value) = values[3].as_ref() {
                font_size = value.parse::<f32>()?.clamp(MIN_FONT_SIZE, MAX_FONT_SIZE);
            }
            if let Some(value) = values[4].as_ref() {
                let result: Result<Vec<_>, _> =
                    value.split(",").map(|x| x.parse::<u32>()).collect();
                dpi = result?;
            }
            if let Some(value) = values[5].as_ref() {
                scrollback_rows = value.parse::<u32>()?;
            }
            if let Some(value) = values[6].as_ref() {
                buffer_count = value.parse::<usize>()?;
            }
        }

        Ok(VirtualConsoleArgs {
            disable,
            keep_log_visible,
            keyrepeat,
            rounded_corners,
            boot_animation,
            color_scheme,
            keymap,
            display_rotation,
            font_size,
            dpi,
            scrollback_rows,
            buffer_count,
        })
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::colors::LIGHT_COLOR_SCHEME,
        fidl_fuchsia_boot::{ArgumentsMarker, ArgumentsRequest},
        fuchsia_async as fasync,
        futures::TryStreamExt,
        std::collections::HashMap,
    };

    fn serve_bootargs(env: HashMap<String, String>) -> Result<ArgumentsProxy, Error> {
        let (proxy, mut stream) = fidl::endpoints::create_proxy_and_stream::<ArgumentsMarker>()?;

        fn get_bool_arg(env: &HashMap<String, String>, name: &String, default: bool) -> bool {
            let mut ret = default;
            if let Some(val) = env.get(name) {
                if val == "0" || val == "false" || val == "off" {
                    ret = false;
                } else {
                    ret = true;
                }
            }
            ret
        }

        fasync::Task::local(async move {
            while let Ok(Some(req)) = stream.try_next().await {
                match req {
                    ArgumentsRequest::GetStrings { keys, responder } => {
                        let vec: Vec<_> =
                            keys.into_iter().map(|key| env.get(&key).map(|s| &s[..])).collect();
                        responder.send(&mut vec.into_iter()).unwrap();
                    }
                    ArgumentsRequest::GetBools { keys, responder } => {
                        let mut iter = keys
                            .into_iter()
                            .map(|key| get_bool_arg(&env, &key.key, key.defaultval));
                        responder.send(&mut iter).unwrap();
                    }
                    _ => assert!(false),
                }
            }
        })
        .detach();

        Ok(proxy)
    }

    #[fasync::run_singlethreaded(test)]
    async fn check_disable() -> Result<(), Error> {
        let vars: HashMap<String, String> = [("virtcon.disable", "true")]
            .iter()
            .map(|(a, b)| (a.to_string(), b.to_string()))
            .collect();
        let proxy = serve_bootargs(vars)?;
        let args = VirtualConsoleArgs::new_with_proxy(proxy).await?;
        assert_eq!(args.disable, true);

        let vars: HashMap<String, String> = [("virtcon.disable", "false")]
            .iter()
            .map(|(a, b)| (a.to_string(), b.to_string()))
            .collect();
        let proxy = serve_bootargs(vars)?;
        let args = VirtualConsoleArgs::new_with_proxy(proxy).await?;
        assert_eq!(args.disable, false);

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn check_keep_log_visible() -> Result<(), Error> {
        let vars: HashMap<String, String> = [("virtcon.keep-log-visible", "true")]
            .iter()
            .map(|(a, b)| (a.to_string(), b.to_string()))
            .collect();
        let proxy = serve_bootargs(vars)?;
        let args = VirtualConsoleArgs::new_with_proxy(proxy).await?;
        assert_eq!(args.keep_log_visible, true);

        let vars: HashMap<String, String> = [("virtcon.keep-log-visible", "false")]
            .iter()
            .map(|(a, b)| (a.to_string(), b.to_string()))
            .collect();
        let proxy = serve_bootargs(vars)?;
        let args = VirtualConsoleArgs::new_with_proxy(proxy).await?;
        assert_eq!(args.keep_log_visible, false);

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn check_keyrepeat() -> Result<(), Error> {
        let vars: HashMap<String, String> = [("virtcon.keyrepeat", "true")]
            .iter()
            .map(|(a, b)| (a.to_string(), b.to_string()))
            .collect();
        let proxy = serve_bootargs(vars)?;
        let args = VirtualConsoleArgs::new_with_proxy(proxy).await?;
        assert_eq!(args.keyrepeat, true);

        let vars: HashMap<String, String> = [("virtcon.keyrepeat", "false")]
            .iter()
            .map(|(a, b)| (a.to_string(), b.to_string()))
            .collect();
        let proxy = serve_bootargs(vars)?;
        let args = VirtualConsoleArgs::new_with_proxy(proxy).await?;
        assert_eq!(args.keyrepeat, false);

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn check_rounded_corners() -> Result<(), Error> {
        let vars: HashMap<String, String> = [("virtcon.rounded_corners", "true")]
            .iter()
            .map(|(a, b)| (a.to_string(), b.to_string()))
            .collect();
        let proxy = serve_bootargs(vars)?;
        let args = VirtualConsoleArgs::new_with_proxy(proxy).await?;
        assert_eq!(args.rounded_corners, true);

        let vars: HashMap<String, String> = [("virtcon.rounded_corners", "false")]
            .iter()
            .map(|(a, b)| (a.to_string(), b.to_string()))
            .collect();
        let proxy = serve_bootargs(vars)?;
        let args = VirtualConsoleArgs::new_with_proxy(proxy).await?;
        assert_eq!(args.rounded_corners, false);

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn check_boot_animation() -> Result<(), Error> {
        let vars: HashMap<String, String> = [("virtcon.boot_animation", "true")]
            .iter()
            .map(|(a, b)| (a.to_string(), b.to_string()))
            .collect();
        let proxy = serve_bootargs(vars)?;
        let args = VirtualConsoleArgs::new_with_proxy(proxy).await?;
        assert_eq!(args.boot_animation, true);

        let vars: HashMap<String, String> = [("virtcon.boot_animation", "false")]
            .iter()
            .map(|(a, b)| (a.to_string(), b.to_string()))
            .collect();
        let proxy = serve_bootargs(vars)?;
        let args = VirtualConsoleArgs::new_with_proxy(proxy).await?;
        assert_eq!(args.boot_animation, false);

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn check_color_scheme() -> Result<(), Error> {
        let vars: HashMap<String, String> = [("virtcon.colorscheme", "light")]
            .iter()
            .map(|(a, b)| (a.to_string(), b.to_string()))
            .collect();
        let proxy = serve_bootargs(vars)?;
        let args = VirtualConsoleArgs::new_with_proxy(proxy).await?;
        assert_eq!(args.color_scheme, LIGHT_COLOR_SCHEME);

        let vars: HashMap<String, String> = HashMap::new();
        let proxy = serve_bootargs(vars)?;
        let args = VirtualConsoleArgs::new_with_proxy(proxy).await?;
        assert_eq!(args.color_scheme, ColorScheme::default());

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn check_keymap() -> Result<(), Error> {
        let vars: HashMap<String, String> = [("virtcon.keymap", "US_DVORAK")]
            .iter()
            .map(|(a, b)| (a.to_string(), b.to_string()))
            .collect();
        let proxy = serve_bootargs(vars)?;
        let args = VirtualConsoleArgs::new_with_proxy(proxy).await?;
        assert_eq!(args.keymap, "US_DVORAK");

        let vars: HashMap<String, String> = [("virtcon.keymap", "dvorak")]
            .iter()
            .map(|(a, b)| (a.to_string(), b.to_string()))
            .collect();
        let proxy = serve_bootargs(vars)?;
        let args = VirtualConsoleArgs::new_with_proxy(proxy).await?;
        assert_eq!(args.keymap, "US_DVORAK");

        let vars: HashMap<String, String> = HashMap::new();
        let proxy = serve_bootargs(vars)?;
        let args = VirtualConsoleArgs::new_with_proxy(proxy).await?;
        assert_eq!(args.keymap, "US_QWERTY");

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn check_display_rotation() -> Result<(), Error> {
        let vars: HashMap<String, String> = [("virtcon.display_rotation", "90")]
            .iter()
            .map(|(a, b)| (a.to_string(), b.to_string()))
            .collect();
        let proxy = serve_bootargs(vars)?;
        let args = VirtualConsoleArgs::new_with_proxy(proxy).await?;
        assert_eq!(args.display_rotation, DisplayRotation::Deg90);

        let vars: HashMap<String, String> = [("virtcon.display_rotation", "0")]
            .iter()
            .map(|(a, b)| (a.to_string(), b.to_string()))
            .collect();
        let proxy = serve_bootargs(vars)?;
        let args = VirtualConsoleArgs::new_with_proxy(proxy).await?;
        assert_eq!(args.display_rotation, DisplayRotation::Deg0);

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn check_font_size() -> Result<(), Error> {
        let vars: HashMap<String, String> = [("virtcon.font_size", "32.0")]
            .iter()
            .map(|(a, b)| (a.to_string(), b.to_string()))
            .collect();
        let proxy = serve_bootargs(vars)?;
        let args = VirtualConsoleArgs::new_with_proxy(proxy).await?;
        assert_eq!(args.font_size, 32.0);

        let vars: HashMap<String, String> = [("virtcon.font_size", "1000000.0")]
            .iter()
            .map(|(a, b)| (a.to_string(), b.to_string()))
            .collect();
        let proxy = serve_bootargs(vars)?;
        let args = VirtualConsoleArgs::new_with_proxy(proxy).await?;
        assert_eq!(args.font_size, MAX_FONT_SIZE);

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn check_dpi() -> Result<(), Error> {
        let vars: HashMap<String, String> = [("virtcon.dpi", "160,320,480,640")]
            .iter()
            .map(|(a, b)| (a.to_string(), b.to_string()))
            .collect();
        let proxy = serve_bootargs(vars)?;
        let args = VirtualConsoleArgs::new_with_proxy(proxy).await?;
        assert_eq!(args.dpi, vec![160, 320, 480, 640]);

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn check_scrollback_rows() -> Result<(), Error> {
        let vars: HashMap<String, String> = [("virtcon.scrollback_rows", "10000")]
            .iter()
            .map(|(a, b)| (a.to_string(), b.to_string()))
            .collect();
        let proxy = serve_bootargs(vars)?;
        let args = VirtualConsoleArgs::new_with_proxy(proxy).await?;
        assert_eq!(args.scrollback_rows, 10_000);

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn check_buffer_count() -> Result<(), Error> {
        let vars: HashMap<String, String> = [("virtcon.buffer_count", "2")]
            .iter()
            .map(|(a, b)| (a.to_string(), b.to_string()))
            .collect();
        let proxy = serve_bootargs(vars)?;
        let args = VirtualConsoleArgs::new_with_proxy(proxy).await?;
        assert_eq!(args.buffer_count, 2);

        Ok(())
    }
}
