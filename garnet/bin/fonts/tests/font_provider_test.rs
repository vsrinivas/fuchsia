// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro)]
// This is only needed because GN's invocation of the Rust compiler doesn't recognize the test_
// methods as entry points, so it complains about the helper methods being "dead code".
#![cfg(test)]

#[cfg(test)]
mod old_api {
    use failure::{format_err, Error, ResultExt};
    use fidl_fuchsia_fonts as fonts;
    use fuchsia_async as fasync;
    use fuchsia_component::client::{launch, launch_with_options, launcher, App, LaunchOptions};
    use fuchsia_zircon as zx;
    use fuchsia_zircon::AsHandleRef;

    macro_rules! assert_buf_eq {
        ($font_info_a:ident, $font_info_b:ident) => {
            assert!(
                $font_info_a.buffer_id == $font_info_b.buffer_id,
                "{}.buffer_id == {}.buffer_id\n{0}: {:?}\n{1}: {:?}",
                stringify!($font_info_a),
                stringify!($font_info_b),
                $font_info_a,
                $font_info_b
            )
        };
    }

    #[derive(Debug, Eq, PartialEq)]
    struct FontInfo {
        vmo_koid: zx::Koid,
        buffer_id: u32,
        size: u64,
        index: u32,
    }

    async fn get_font_info(
        font_provider: &fonts::ProviderProxy,
        name: Option<String>,
        language: Option<Vec<String>>,
        character: char,
    ) -> Result<FontInfo, Error> {
        let font = await!(font_provider.get_font(&mut fonts::Request {
            family: name.clone(),
            weight: 400,
            width: 5,
            slant: fonts::Slant::Upright,
            character: character as u32,
            language,
            fallback_group: fonts::FallbackGroup::None,
            flags: 0,
        }))?;
        let font = *font.ok_or_else(|| format_err!("Received empty response for {:?}", name))?;

        assert!(font.buffer.size > 0);
        assert!(font.buffer.size <= font.buffer.vmo.get_size()?);

        let vmo_koid = font.buffer.vmo.as_handle_ref().get_koid()?;
        Ok(FontInfo {
            vmo_koid,
            buffer_id: font.buffer_id,
            size: font.buffer.size,
            index: font.font_index,
        })
    }

    async fn get_font_info_basic(
        font_provider: &fonts::ProviderProxy,
        name: Option<String>,
    ) -> Result<FontInfo, Error> {
        await!(get_font_info(font_provider, name, None, '\0'))
    }

    fn start_provider_with_default_fonts() -> Result<(App, fonts::ProviderProxy), Error> {
        let launcher = launcher().context("Failed to open launcher service")?;
        let app =
            launch(&launcher, "fuchsia-pkg://fuchsia.com/fonts#meta/fonts.cmx".to_string(), None)
                .context("Failed to launch fonts::Provider")?;

        let font_provider = app
            .connect_to_service::<fonts::ProviderMarker>()
            .context("Failed to connect to fonts::Provider")?;

        Ok((app, font_provider))
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_basic() -> Result<(), Error> {
        let (_app, font_provider) = start_provider_with_default_fonts()?;

        let default = await!(get_font_info_basic(&font_provider, None))
            .context("Failed to load default font")?;
        let roboto = await!(get_font_info_basic(&font_provider, Some("Roboto".to_string())))
            .context("Failed to load Roboto")?;
        let material_icons =
            await!(get_font_info_basic(&font_provider, Some("Material Icons".to_string())))
                .context("Failed to load Material Icons")?;

        // Roboto should be returned by default.
        assert_buf_eq!(default, roboto);

        // Material Icons request should return a different font.
        assert_ne!(default.buffer_id, material_icons.buffer_id);

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_aliases() -> Result<(), Error> {
        let (_app, font_provider) = start_provider_with_default_fonts()?;

        // Both requests should return the same font.
        let materialicons =
            await!(get_font_info_basic(&font_provider, Some("MaterialIcons".to_string())))
                .context("Failed to load MaterialIcons")?;
        let material_icons =
            await!(get_font_info_basic(&font_provider, Some("Material Icons".to_string())))
                .context("Failed to load Material Icons")?;

        assert_buf_eq!(materialicons, material_icons);

        Ok(())
    }

    fn start_provider_with_test_fonts() -> Result<(App, fonts::ProviderProxy), Error> {
        let mut launch_options = LaunchOptions::new();
        launch_options.add_dir_to_namespace(
            "/test_fonts".to_string(),
            std::fs::File::open("/pkg/data/testdata/test_fonts")?,
        )?;

        let launcher = launcher().context("Failed to open launcher service")?;
        let app = launch_with_options(
            &launcher,
            "fuchsia-pkg://fuchsia.com/fonts#meta/fonts.cmx".to_string(),
            Some(vec!["--font-manifest".to_string(), "/test_fonts/manifest.json".to_string()]),
            launch_options,
        )
        .context("Failed to launch fonts::Provider")?;

        let font_provider = app
            .connect_to_service::<fonts::ProviderMarker>()
            .context("Failed to connect to fonts::Provider")?;

        Ok((app, font_provider))
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_font_collections() -> Result<(), Error> {
        let (_app, font_provider) = start_provider_with_test_fonts()?;

        // Request Japanese and Simplified Chinese versions of Noto Sans CJK. Both
        // fonts are part of the same TTC file, so font provider is expected to
        // return the same buffer with different font index values.
        let noto_sans_cjk_ja = await!(get_font_info(
            &font_provider,
            Some("NotoSansCJK".to_string()),
            Some(vec!["ja".to_string()]),
            '\0'
        ))
        .context("Failed to load NotoSansCJK font")?;
        let noto_sans_cjk_sc = await!(get_font_info(
            &font_provider,
            Some("NotoSansCJK".to_string()),
            Some(vec!["zh-Hans".to_string()]),
            '\0'
        ))
        .context("Failed to load NotoSansCJK font")?;

        assert_buf_eq!(noto_sans_cjk_ja, noto_sans_cjk_sc);

        assert!(
            noto_sans_cjk_ja.index != noto_sans_cjk_sc.index,
            "noto_sans_cjk_ja.index != noto_sans_cjk_sc.index\n \
             noto_sans_cjk_ja.index: {:?}\n \
             noto_sans_cjk_sc.index: {:?}",
            noto_sans_cjk_ja,
            noto_sans_cjk_sc
        );

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_fallback() -> Result<(), Error> {
        let (_app, font_provider) = start_provider_with_test_fonts()?;

        let noto_sans_cjk_ja = await!(get_font_info(
            &font_provider,
            Some("NotoSansCJK".to_string()),
            Some(vec!["ja".to_string()]),
            '\0'
        ))
        .context("Failed to load NotoSansCJK font")?;

        let noto_sans_cjk_ja_by_char = await!(get_font_info(
            &font_provider,
            Some("Roboto".to_string()),
            Some(vec!["ja".to_string()]),
            'な'
        ))
        .context("Failed to load NotoSansCJK font")?;

        // Same font should be returned in both cases.
        assert_buf_eq!(noto_sans_cjk_ja, noto_sans_cjk_ja_by_char);

        Ok(())
    }

    // Verify that the fallback group of the requested font is taken into account for fallback.
    #[fasync::run_singlethreaded(test)]
    async fn test_fallback_group() -> Result<(), Error> {
        let (_app, font_provider) = start_provider_with_test_fonts()?;

        let noto_serif_cjk_ja = await!(get_font_info(
            &font_provider,
            Some("Noto Serif CJK".to_string()),
            Some(vec!["ja".to_string()]),
            '\0'
        ))
        .context("Failed to load Noto Serif CJK font")?;

        let noto_serif_cjk_ja_by_char = await!(get_font_info(
            &font_provider,
            Some("Roboto Slab".to_string()),
            Some(vec!["ja".to_string()]),
            'な'
        ))
        .context("Failed to load Noto Serif CJK font")?;

        // The query above requested Roboto Slab, so it's expected to return
        // Noto Serif CJK instead of Noto Sans CJK because Roboto Slab and
        // Noto Serif CJK are both in serif fallback group.
        assert_buf_eq!(noto_serif_cjk_ja, noto_serif_cjk_ja_by_char);

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_family_info() -> Result<(), Error> {
        let (_app, font_provider) = start_provider_with_default_fonts()?;

        let family_info = await!(font_provider.get_family_info("materialicons"))?;

        assert!(family_info.is_some());
        let family_info = family_info.unwrap();

        assert_eq!(family_info.name, "Material Icons");
        assert!(family_info.styles.len() > 0);

        Ok(())
    }
}

#[cfg(test)]
mod reviewed_api {
    use {
        failure::{Error, ResultExt},
        fidl_fuchsia_fonts as fonts,
        fidl_fuchsia_fonts_ext::DecodableExt,
        fidl_fuchsia_intl as intl, fuchsia_async as fasync,
        fuchsia_component::client::{launch, launch_with_options, launcher, App, LaunchOptions},
        fuchsia_zircon as zx,
        fuchsia_zircon::AsHandleRef,
    };

    macro_rules! assert_buf_eq {
        ($typeface_info_a:ident, $typeface_info_b:ident) => {
            assert!(
                $typeface_info_a.buffer_id == $typeface_info_b.buffer_id,
                "{}.buffer_id == {}.buffer_id\n{0}: {:?}\n{1}: {:?}",
                stringify!($typeface_info_a),
                stringify!($typeface_info_b),
                $typeface_info_a,
                $typeface_info_b
            )
        };
    }

    #[derive(Debug, Eq, PartialEq)]
    struct TypefaceInfo {
        vmo_koid: zx::Koid,
        buffer_id: u32,
        size: u64,
        index: u32,
    }

    async fn get_typeface_info(
        font_provider: &fonts::ProviderProxy,
        name: Option<String>,
        languages: Option<Vec<String>>,
        code_points: Option<Vec<char>>,
    ) -> Result<TypefaceInfo, Error> {
        let typeface = await!(font_provider.get_typeface(fonts::TypefaceRequest {
            query: Some(fonts::TypefaceQuery {
                family: name.as_ref().map(|name| fonts::FamilyName { name: name.to_string() }),
                style: Some(fonts::Style2 {
                    weight: Some(fonts::WEIGHT_NORMAL),
                    width: Some(fonts::Width::SemiExpanded),
                    slant: Some(fonts::Slant::Upright)
                }),
                code_points: code_points
                    .map(|code_points| code_points.into_iter().map(|ch| ch as u32).collect()),
                languages: languages.map(|languages| languages
                    .into_iter()
                    .map(|lang_code| intl::LocaleId { id: lang_code })
                    .collect()),
                fallback_family: None,
            }),
            flags: Some(fonts::TypefaceRequestFlags::empty()),
        }))?;

        assert!(!typeface.is_empty(), "Received empty response for {:?}", name);
        let buffer = typeface.buffer.unwrap();
        assert!(buffer.size > 0);
        assert!(buffer.size <= buffer.vmo.get_size()?);

        let vmo_koid = buffer.vmo.as_handle_ref().get_koid()?;
        Ok(TypefaceInfo {
            vmo_koid,
            buffer_id: typeface.buffer_id.unwrap(),
            size: buffer.size,
            index: typeface.font_index.unwrap(),
        })
    }

    async fn get_typeface_info_basic(
        font_provider: &fonts::ProviderProxy,
        name: Option<String>,
    ) -> Result<TypefaceInfo, Error> {
        await!(get_typeface_info(font_provider, name, None, None))
    }

    fn start_provider_with_default_fonts() -> Result<(App, fonts::ProviderProxy), Error> {
        let launcher = launcher().context("Failed to open launcher service")?;
        let app =
            launch(&launcher, "fuchsia-pkg://fuchsia.com/fonts#meta/fonts.cmx".to_string(), None)
                .context("Failed to launch fonts::Provider")?;

        let font_provider = app
            .connect_to_service::<fonts::ProviderMarker>()
            .context("Failed to connect to fonts::Provider")?;

        Ok((app, font_provider))
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_basic() -> Result<(), Error> {
        let (_app, font_provider) = start_provider_with_default_fonts()?;

        let default = await!(get_typeface_info_basic(&font_provider, None))
            .context("Failed to load default font")?;
        let roboto = await!(get_typeface_info_basic(&font_provider, Some("Roboto".to_string())))
            .context("Failed to load Roboto")?;
        let material_icons =
            await!(get_typeface_info_basic(&font_provider, Some("Material Icons".to_string())))
                .context("Failed to load Material Icons")?;

        // Roboto should be returned by default.
        assert_buf_eq!(default, roboto);

        // Material Icons request should return a different font.
        assert_ne!(default.vmo_koid, material_icons.vmo_koid);
        assert_ne!(default.buffer_id, material_icons.buffer_id);

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_aliases() -> Result<(), Error> {
        let (_app, font_provider) = start_provider_with_default_fonts()?;

        // Both requests should return the same font.
        let materialicons =
            await!(get_typeface_info_basic(&font_provider, Some("MaterialIcons".to_string())))
                .context("Failed to load MaterialIcons")?;
        let material_icons =
            await!(get_typeface_info_basic(&font_provider, Some("Material Icons".to_string())))
                .context("Failed to load Material Icons")?;

        assert_buf_eq!(materialicons, material_icons);

        Ok(())
    }

    fn start_provider_with_test_fonts() -> Result<(App, fonts::ProviderProxy), Error> {
        let mut launch_options = LaunchOptions::new();
        launch_options.add_dir_to_namespace(
            "/test_fonts".to_string(),
            std::fs::File::open("/pkg/data/testdata/test_fonts")?,
        )?;

        let launcher = launcher().context("Failed to open launcher service")?;
        let app = launch_with_options(
            &launcher,
            "fuchsia-pkg://fuchsia.com/fonts#meta/fonts.cmx".to_string(),
            Some(vec!["--font-manifest".to_string(), "/test_fonts/manifest.json".to_string()]),
            launch_options,
        )
        .context("Failed to launch fonts::Provider")?;

        let font_provider = app
            .connect_to_service::<fonts::ProviderMarker>()
            .context("Failed to connect to fonts::Provider")?;

        Ok((app, font_provider))
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_font_collections() -> Result<(), Error> {
        let (_app, font_provider) = start_provider_with_test_fonts()?;

        // Request Japanese and Simplified Chinese versions of Noto Sans CJK. Both
        // fonts are part of the same TTC file, so font provider is expected to
        // return the same buffer with different font index values.
        let noto_sans_cjk_ja = await!(get_typeface_info(
            &font_provider,
            Some("NotoSansCJK".to_string()),
            Some(vec!["ja".to_string()]),
            None
        ))
        .context("Failed to load NotoSansCJK font")?;
        let noto_sans_cjk_sc = await!(get_typeface_info(
            &font_provider,
            Some("NotoSansCJK".to_string()),
            Some(vec!["zh-Hans".to_string()]),
            None
        ))
        .context("Failed to load NotoSansCJK font")?;

        assert_buf_eq!(noto_sans_cjk_ja, noto_sans_cjk_sc);

        assert!(
            noto_sans_cjk_ja.index != noto_sans_cjk_sc.index,
            "noto_sans_cjk_ja.index != noto_sans_cjk_sc.index\n \
             noto_sans_cjk_ja.index: {:?}\n \
             noto_sans_cjk_sc.index: {:?}",
            noto_sans_cjk_ja,
            noto_sans_cjk_sc
        );
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_fallback() -> Result<(), Error> {
        let (_app, font_provider) = start_provider_with_test_fonts()?;

        let noto_sans_cjk_ja = await!(get_typeface_info(
            &font_provider,
            Some("NotoSansCJK".to_string()),
            Some(vec!["ja".to_string()]),
            None
        ))
        .context("Failed to load NotoSansCJK font")?;

        let noto_sans_cjk_ja_by_char = await!(get_typeface_info(
            &font_provider,
            Some("Roboto".to_string()),
            Some(vec!["ja".to_string()]),
            Some(vec!['な', 'ナ'])
        ))
        .context("Failed to load NotoSansCJK font")?;

        // Same font should be returned in both cases.
        assert_buf_eq!(noto_sans_cjk_ja, noto_sans_cjk_ja_by_char);

        Ok(())
    }

    // Verify that the fallback group of the requested font is taken into account for fallback.
    #[fasync::run_singlethreaded(test)]
    async fn test_fallback_group() -> Result<(), Error> {
        let (_app, font_provider) = start_provider_with_test_fonts()?;

        let noto_serif_cjk_ja = await!(get_typeface_info(
            &font_provider,
            Some("Noto Serif CJK".to_string()),
            Some(vec!["ja".to_string()]),
            None
        ))
        .context("Failed to load Noto Serif CJK font")?;

        let noto_serif_cjk_ja_by_char = await!(get_typeface_info(
            &font_provider,
            Some("Roboto Slab".to_string()),
            Some(vec!["ja".to_string()]),
            Some(vec!['な'])
        ))
        .context("Failed to load Noto Serif CJK font")?;

        // The query above requested Roboto Slab, so it's expected to return
        // Noto Serif CJK instead of Noto Sans CJK because Roboto Slab and
        // Noto Serif CJK are both in serif fallback group.
        assert_buf_eq!(noto_serif_cjk_ja, noto_serif_cjk_ja_by_char);

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_font_family_info() -> Result<(), Error> {
        let (_app, font_provider) = start_provider_with_default_fonts()?;

        let font_family_info = await!(font_provider
            .get_font_family_info(&mut fonts::FamilyName { name: "materialicons".to_string() }))?;

        assert!(!font_family_info.is_empty());

        assert_eq!(font_family_info.name.unwrap().name, "Material Icons");
        assert!(font_family_info.styles.unwrap().len() > 0);

        Ok(())
    }
}
