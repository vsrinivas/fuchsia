// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_ui_composition::{ScreenshotFormat, ScreenshotMarker, ScreenshotTakeRequest},
    fuchsia_component::client::connect_to_protocol,
    fuchsia_zircon as zx,
    png::HasParameters,
    std::convert::TryInto,
    std::fs,
    std::io::BufWriter,
};

const SCREENSHOT_FILE: &'static str = "/custom_artifacts/screenshot.png";

/// Captures screenshot and saves it as PNG file to |SCREENSHOT_FILE|
/// panics if screenshot is blank.
pub async fn take_screenshot() {
    let screenshot =
        connect_to_protocol::<ScreenshotMarker>().expect("failed to connect to Screenshot");
    let data = screenshot
        .take(ScreenshotTakeRequest {
            format: Some(ScreenshotFormat::BgraRaw),
            ..ScreenshotTakeRequest::EMPTY
        })
        .await
        .expect("cannot take screenshot using scenic");
    let image_size = data.size.expect("no data size returned from screenshot");
    let raw_data_size = image_size.width * image_size.height * 4;
    let screenshot_file = fs::File::create(SCREENSHOT_FILE)
        .expect(&format!("cannot create file {}", SCREENSHOT_FILE));
    let ref mut w = BufWriter::new(screenshot_file);

    let mut encoder = png::Encoder::new(w, image_size.width, image_size.height);
    encoder.set(png::BitDepth::Eight);
    encoder.set(png::ColorType::RGBA);
    let mut writer = encoder.write_header().unwrap();

    let mut image_data = vec![0u8; raw_data_size.try_into().unwrap()];
    let image_vmo = data.vmo.expect("failed to obtain vmo handle for screenshot");
    let vmo_is_readable = image_vmo.read(&mut image_data, 0).is_ok();

    if !vmo_is_readable {
        let image_vmo_mapping = mapped_vmo::Mapping::create_from_vmo(
            &image_vmo,
            raw_data_size.try_into().unwrap(),
            zx::VmarFlags::PERM_READ,
        )
        .expect("failed to map VMO");
        image_vmo_mapping.read_at(0, &mut image_data);
    }
    if is_image_blank(&image_data) {
        panic!("Captured screenshot is blank.");
    }
    // NOTE: Remove once fxb/108647 is added.
    bgra_to_rbga(&mut image_data);
    writer.write_image_data(&image_data).expect("failed to write image data as PNG");
}

/// Performs inplace BGRA -> RGBA.
fn bgra_to_rbga(img_data: &mut Vec<u8>) {
    let bytes_per_pixel = 4;
    let mut blue_pos = 0;
    let mut red_pos = 2;
    let img_data_size = img_data.len();

    while blue_pos < img_data_size && red_pos < img_data_size {
        let blue = img_data[blue_pos];
        img_data[blue_pos] = img_data[red_pos];
        img_data[red_pos] = blue;
        blue_pos = blue_pos + bytes_per_pixel;
        red_pos = red_pos + bytes_per_pixel;
    }
}

fn is_image_blank(img_data: &Vec<u8>) -> bool {
    let img_data_iter = img_data.iter();
    for data in img_data_iter {
        if data > &0 {
            return false;
        }
    }
    return true;
}
