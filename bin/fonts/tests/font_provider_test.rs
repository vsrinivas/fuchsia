// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro, futures_api)]

use failure::{Error, ResultExt, format_err};
use fidl_fuchsia_fonts as fonts;
use fuchsia_app::client::Launcher;
use fuchsia_async::Executor;
use fuchsia_zircon as zx;
use fuchsia_zircon::AsHandleRef;

#[derive(Debug, Eq, PartialEq)]
struct FontInfo {
    vmo_koid: zx::Koid,
    size: u64,
}

async fn get_font_info(
    font_provider: &fonts::FontProviderProxy, name: String,
) -> Result<FontInfo, Error> {
    let font = await!(font_provider.get_font(&mut fonts::FontRequest {
        family: name.clone(),
        weight: 400,
        width: 5,
        slant: fonts::FontSlant::Upright,
    }))?;
    let font = *font.ok_or_else(|| format_err!("Received empty response for {}", name))?;

    assert!(font.data.buffer.size > 0);
    assert!(font.data.buffer.size <= font.data.buffer.vmo.get_size()?);

    let vmo_koid = font.data.buffer.vmo.as_handle_ref().get_koid()?;
    Ok(FontInfo {
        vmo_koid,
        size: font.data.buffer.size,
    })
}

async fn run_tests() -> Result<(), Error> {
    let launcher = Launcher::new().context("Failed to open launcher service")?;
    let app = launcher.launch("fonts".to_string(), None)
                      .context("Failed to launch echo service")?;

    let font_provider = app.connect_to_service(fonts::FontProviderMarker)
        .context("Failed to connect to FontProvider")?;

    let default = await!(get_font_info(&font_provider, "".to_string()))
        .context("Failed to load default font")?;
    let roboto = await!(get_font_info(&font_provider, "Roboto".to_string()))
        .context("Failed to load Roboto")?;
    let roboto_slab = await!(get_font_info(&font_provider, "RobotoSlab".to_string()))
        .context("Failed to load RobotoSlab")?;

    // Roboto should be returned by default.
    assert!(default == roboto);

    // RobotoSlab request should return a different font.
    assert!(default.vmo_koid != roboto_slab.vmo_koid);

    Ok(())
}

fn main() -> Result<(), Error> {
    let mut executor = Executor::new().context("Error creating executor")?;

    executor.run_singlethreaded(run_tests())
}
