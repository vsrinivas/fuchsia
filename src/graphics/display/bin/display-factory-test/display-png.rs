// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{ensure, Context as AnyhowContext, Error},
    argh::FromArgs,
    carnelian::{
        color::Color,
        make_app_assistant,
        render::{Composition, Context, CopyRegion, Image, PreClear, PreCopy, RenderExt},
        App, AppAssistant, IntSize, RenderOptions, ViewAssistant, ViewAssistantContext,
        ViewAssistantPtr, ViewKey,
    },
    euclid::default::{Point2D, Rect},
    fuchsia_async as fasync,
    fuchsia_zircon::{AsHandleRef, Duration, Event, Signals},
    std::{fs::File, process},
};

const WHITE_COLOR: Color = Color { r: 255, g: 255, b: 255, a: 255 };

/// Display Png.
#[derive(Debug, FromArgs)]
#[argh(name = "display_png")]
struct Args {
    /// PNG file to load
    #[argh(option)]
    file: Option<String>,

    /// path to file containing gamma values. The file format is
    /// a header line plus three lines of 256 three-digit hexadecimal
    /// value groups.
    #[argh(option)]
    gamma_file: Option<String>,

    /// integer value with which to divide the gamma values found in gamma
    /// values file. Does nothing if no gamma file is specified.
    #[argh(option, default = "1023")]
    gamma_divisor: i64,

    /// background color (default is white)
    #[argh(option, from_str_fn(parse_color))]
    background: Option<Color>,

    /// an optional x,y position for the image (default is center)
    #[argh(option, from_str_fn(parse_point))]
    position: Option<Point2D<f32>>,

    /// seconds of delay before application exits (default is 1 second)
    #[argh(option, default = "1")]
    timeout: i64,
}

fn parse_color(value: &str) -> Result<Color, String> {
    Color::from_hash_code(value).map_err(|err| err.to_string())
}

fn parse_point(value: &str) -> Result<Point2D<f32>, String> {
    let mut coords = vec![];
    for value in value.split(",") {
        coords.push(value.parse::<f32>().map_err(|err| err.to_string())?);
    }
    if coords.len() == 2 {
        Ok(Point2D::new(coords[0], coords[1]))
    } else {
        Err("bad position".to_string())
    }
}

struct PngSourceInfo {
    png_reader: Option<png::Reader<File>>,
    png_size: IntSize,
}

#[derive(Clone, Debug)]
struct GammaValues {
    red: Vec<f32>,
    green: Vec<f32>,
    blue: Vec<f32>,
}

const VALUE_GROUP_LENGTH: usize = 3;
const VALUE_COUNT: usize = 256;
const VALUE_LINE_LENGTH: usize = VALUE_COUNT * VALUE_GROUP_LENGTH;

impl GammaValues {
    fn parse_values_line(line: &str, divisor: f32) -> Result<Vec<f32>, Error> {
        ensure!(
            line.len() == VALUE_LINE_LENGTH,
            "Expected value line to be length {} but saw {}",
            VALUE_LINE_LENGTH,
            line.len()
        );
        let values: Vec<f32> = (0..VALUE_LINE_LENGTH)
            .step_by(3)
            .filter_map(|index| {
                usize::from_str_radix(&line[index..index + 3], 16)
                    .ok()
                    .and_then(|value| Some(value as f32 / divisor))
            })
            .collect();
        ensure!(
            values.len() == VALUE_COUNT,
            "Expected {} values, saw {}",
            VALUE_COUNT,
            values.len()
        );
        Ok(values)
    }

    fn parse(source_text: &str, divisor: f32) -> Result<Self, Error> {
        let parts: Vec<&str> = source_text.split("\n").collect();
        ensure!(parts.len() >= 4, "Expected four lines, got {}", parts.len());
        let version_line_parts: Vec<&str> = parts[0].split(" ").collect();
        ensure!(
            version_line_parts == vec!["Gamma", "Calibration", "1.0"],
            "Expected version on first line but saw '{}'",
            parts[0]
        );
        let version = version_line_parts[2].parse::<f32>().context("parsing version number")?;
        ensure!(version == 1.0, "expected version 1.0, got {}", version);
        let red = Self::parse_values_line(parts[1], divisor)?;
        let green = Self::parse_values_line(parts[2], divisor)?;
        let blue = Self::parse_values_line(parts[3], divisor)?;
        Ok(Self { red, green, blue })
    }

    fn parse_file(path: &str, divisor: f32) -> Result<Self, Error> {
        let source_text = std::fs::read_to_string(path)
            .context(format!("Error trying to parse gamma values file: {}", path))?;
        Self::parse(&source_text, divisor)
    }
}

#[derive(Default)]
struct DisplayPngAppAssistant {
    png_source: Option<PngSourceInfo>,
    gamma_values: Option<GammaValues>,
    background: Option<Color>,
    position: Option<Point2D<f32>>,
}

impl DisplayPngAppAssistant {}

impl AppAssistant for DisplayPngAppAssistant {
    fn setup(&mut self) -> Result<(), Error> {
        let args: Args = argh::from_env();
        self.background = args.background;
        self.position = args.position;

        if let Some(filename) = args.file {
            let file = File::open(format!("{}", filename))
                .context(format!("failed to open file {}", filename))?;
            let decoder = png::Decoder::new(file);
            let (info, png_reader) =
                decoder.read_info().context(format!("failed to open image file {}", filename))?;
            ensure!(
                info.width > 0 && info.height > 0,
                "Invalid image size for png file {}x{}",
                info.width,
                info.height
            );

            self.png_source = Some(PngSourceInfo {
                png_reader: Some(png_reader),
                png_size: IntSize::new(info.width as i32, info.height as i32),
            });
        }

        if let Some(gamma_file) = args.gamma_file {
            ensure!(args.gamma_divisor != 0, "gamma_divisor value must be non-zero");
            self.gamma_values =
                Some(GammaValues::parse_file(&gamma_file, args.gamma_divisor as f32)?);
        }

        Ok(())
    }

    fn create_view_assistant(&mut self, _: ViewKey) -> Result<ViewAssistantPtr, Error> {
        let png_source = self.png_source.take();
        Ok(Box::new(DisplayPngViewAssistant::new(
            png_source,
            self.gamma_values.clone(),
            self.background.take().unwrap_or(WHITE_COLOR),
            self.position.take(),
        )))
    }

    fn get_render_options(&self) -> RenderOptions {
        RenderOptions { ..RenderOptions::default() }
    }
}

const GAMMA_TABLE_ID: u64 = 100;

struct DisplayPngViewAssistant {
    png_source: Option<PngSourceInfo>,
    gamma_values: Option<GammaValues>,
    background: Color,
    png: Option<Image>,
    composition: Composition,
    position: Option<Point2D<f32>>,
}

impl DisplayPngViewAssistant {
    pub fn new(
        png_source: Option<PngSourceInfo>,
        gamma_values: Option<GammaValues>,
        background: Color,
        position: Option<Point2D<f32>>,
    ) -> Self {
        let background = Color { r: background.r, g: background.g, b: background.b, a: 255 };
        let composition = Composition::new(background);
        Self { png_source, gamma_values, background, png: None, composition, position }
    }
}

impl ViewAssistant for DisplayPngViewAssistant {
    fn setup(&mut self, context: &ViewAssistantContext) -> Result<(), Error> {
        let args: Args = argh::from_env();
        if let Some(gamma_values) = self.gamma_values.as_ref() {
            if let Some(frame_buffer) = context.frame_buffer.as_ref() {
                let frame_buffer = frame_buffer.borrow_mut();
                let mut r: [f32; 256] = [0.0; 256];
                r.copy_from_slice(&gamma_values.red);
                let mut g: [f32; 256] = [0.0; 256];
                g.copy_from_slice(&gamma_values.green);
                let mut b: [f32; 256] = [0.0; 256];
                b.copy_from_slice(&gamma_values.blue);
                frame_buffer.controller.import_gamma_table(
                    GAMMA_TABLE_ID,
                    &mut r,
                    &mut g,
                    &mut b,
                )?;
                let config = frame_buffer.get_config();
                frame_buffer
                    .controller
                    .set_display_gamma_table(config.display_id, GAMMA_TABLE_ID)?;
            }
        }
        let timer = fasync::Timer::new(fasync::Time::after(Duration::from_seconds(args.timeout)));
        fasync::Task::local(async move {
            timer.await;
            process::exit(1);
        })
        .detach();
        Ok(())
    }

    fn render(
        &mut self,
        render_context: &mut Context,
        ready_event: Event,
        context: &ViewAssistantContext,
    ) -> Result<(), Error> {
        let pre_copy = if let Some(png_source) = self.png_source.as_mut() {
            let png_size = png_source.png_size;
            // Create image from PNG.
            let png_image = self.png.take().unwrap_or_else(|| {
                let mut png_reader = png_source.png_reader.take().expect("png_reader");
                render_context
                    .new_image_from_png(&mut png_reader)
                    .expect(&format!("failed to decode file"))
            });

            // Center image if position has not been specified.
            let position = self.position.take().unwrap_or_else(|| {
                let x = (context.size.width - png_size.width as f32) / 2.0;
                let y = (context.size.height - png_size.height as f32) / 2.0;
                Point2D::new(x, y)
            });

            // Determine visible rect.
            let dst_rect = Rect::new(position.to_i32(), png_size.to_i32());
            let output_rect = Rect::new(Point2D::zero(), context.size.to_i32());

            // Clear |image| to background color and copy |png_image| to |image|.
            let ext = RenderExt {
                pre_clear: Some(PreClear { color: self.background }),
                pre_copy: dst_rect.intersection(&output_rect).map(|visible_rect| PreCopy {
                    image: png_image,
                    copy_region: CopyRegion {
                        src_offset: (visible_rect.origin - dst_rect.origin).to_point().to_u32(),
                        dst_offset: visible_rect.origin.to_u32(),
                        extent: visible_rect.size.to_u32(),
                    },
                }),
                ..Default::default()
            };
            let image = render_context.get_current_image(context);
            render_context.render(&self.composition, Some(Rect::zero()), image, &ext);

            self.png.replace(png_image);
            self.position.replace(position);

            dst_rect.intersection(&output_rect).map(|visible_rect| PreCopy {
                image: png_image,
                copy_region: CopyRegion {
                    src_offset: (visible_rect.origin - dst_rect.origin).to_point().to_u32(),
                    dst_offset: visible_rect.origin.to_u32(),
                    extent: visible_rect.size.to_u32(),
                },
            })
        } else {
            None
        };

        // Clear |image| to background color and copy |png_image| to |image|.
        let ext = RenderExt {
            pre_clear: Some(PreClear { color: self.background }),
            pre_copy: pre_copy,
            ..Default::default()
        };
        let image = render_context.get_current_image(context);
        render_context.render(&self.composition, Some(Rect::zero()), image, &ext);

        ready_event.as_handle_ref().signal(Signals::NONE, Signals::EVENT_SIGNALED)?;
        Ok(())
    }
}

#[cfg(test)]
mod test {
    use super::*;

    const LEGAL_SAMPLE_DATA: &'static str = r#"Gamma Calibration 1.0
00000300700a00e01101401801b01f02202602902d03003403703b03e04204504904c05005305705b05e06206506906d07007407807b07f08308608a08e09209509909d0a10a50a80ac0b00b40b80bc0bf0c30c70cb0ce0d20d60da0de0e10e50e90ec0f00f40f70fb0ff10210610910d11111411811b11f12312612a12d13113413813c13f14314614a14e15115515815c16016316716b16e17217617917d18018418818b18f19319619a19e1a11a51a91ac1b01b41b71bb1bf1c21c61ca1cd1d11d51d81dc1e01e31e71eb1ee1f21f61f91fd20120420820c20f21321721a21e22222522922d23123423823c23f24324724b24e25225625a25d26126526926c27027427827b27f28328728b28e29229629a29e2a12a52a92ad2b12b52b82bc2c02c42c82cc2d02d32d72db2df2e32e72eb2ef2f32f72fa2fe30230630a30e31231631a31e32232632932d33133533933d34134534934c35035435835c36036336736b36f37337737a37e38238638a38e39139539939d3a13a53a83ac3b03b4
00000400700b00f01201601a01d02102502902c03003403803b03f04304704b04e05205605a05e06206506906d07107507907d08108508908d09109509909d0a10a50a90ad0b10b50b90bd0c10c50c90cd0d20d60da0de0e20e60ea0ee0f20f60fa0fe10210610a10d11111511911d12112512912c13013413813c14014414714b14f15315715b15f16216616a16e17217617a17e18218618918d19119519919d1a11a51a91ad1b11b51b91bd1c11c51c81cc1d01d41d81dc1e01e41e81ec1f01f41f81fc20020420820b20f21321721b21f22322722b22f23323723b23f24224624a24e25225625a25e26226626a26e27227627a27e28228628a28e29329729b29f2a32a72ab2af2b32b72bb2bf2c42c82cc2d02d42d82dc2e02e52e92ed2f12f52f92fe30230630a30e31331731b31f32332832c33033433933d34134534a34e35235635b35f36336736b37037437837c38038438938d39139539939d3a23a63aa3ae3b23b63ba3be3c33c73cb3cf3d33d73db3df3e33e83ec3f03f43f83fc
00000300700a00e01101501801c01f02202602902d03003403803b03f04204604904d05005405805b05f06206606a06d07107507807c08008408708b08f09309609a09e0a20a60a90ad0b10b50b90bd0c00c40c80cc0d00d40d70db0df0e30e60ea0ee0f10f50f90fc10010410710b10f11211611911d12112412812c12f13313613a13e14114514814c15015315715b15e16216516916d17017417817b17f18318718a18e19219519919d1a01a41a81ab1af1b31b61ba1be1c21c51c91cd1d01d41d81db1df1e31e61ea1ee1f11f51f91fd20020420820b20f21321621a21e22122522922d23023423823b23f24324724a24e25225625925d26126526926c27027427827c27f28328728b28f29229629a29e2a22a62a92ad2b12b52b92bd2c12c52c82cc2d02d42d82dc2e02e42e82ec2f02f32f72fb2ff30330730b30f31331731b31f32332732b32f33333633a33e34234634a34e35235635935d36136536936d37137537837c38038438838c39039339739b39f3a33a73aa3ae3b23b63ba"#;

    #[test]
    fn legal_calibration_data() {
        let gamma_values = GammaValues::parse(LEGAL_SAMPLE_DATA, 1023.0).expect("parse");
        assert_eq!(gamma_values.red.len(), 256);
        assert_eq!(gamma_values.red[0], 0.0);
        assert_eq!(gamma_values.green.len(), 256);
        assert_eq!((gamma_values.green[1] * 1023.0) as usize, 4);
        assert_eq!(gamma_values.blue.len(), 256);
        assert_eq!((gamma_values.blue[255] * 1023.0) as usize, 0x3ba);
    }

    const TRUNCATED_SAMPLE_DATA: &'static str =
        "Gamma Calibration 1.0\n00000300700a00e01101401801b01f02202602902d0300340\n\n\n";

    #[test]
    fn empty_calibration_data() {
        let gamma_values = GammaValues::parse(TRUNCATED_SAMPLE_DATA, 1023.0);
        assert!(gamma_values.is_err());
    }

    const BAD_VERSION_SAMPLE_DATA: &'static str = "Gamma Calibration 2.0\n\n\n\n";

    #[test]
    fn bad_version_calibration_data() {
        let gamma_values = GammaValues::parse(BAD_VERSION_SAMPLE_DATA, 1023.0);
        assert!(gamma_values.is_err());
    }
}

fn main() -> Result<(), Error> {
    App::run(make_app_assistant::<DisplayPngAppAssistant>())
}
