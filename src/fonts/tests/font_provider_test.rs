// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await)]
// This is only needed because GN's invocation of the Rust compiler doesn't recognize the test_
// methods as entry points, so it complains about the helper methods being "dead code".
#![cfg(test)]

const FONTS_CMX: &str = "fuchsia-pkg://fuchsia.com/fonts#meta/fonts.cmx";

#[cfg(test)]
mod old_api {
    use crate::FONTS_CMX;
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
        let font = font_provider
            .get_font(&mut fonts::Request {
                family: name.clone(),
                weight: 400,
                width: 5,
                slant: fonts::Slant::Upright,
                character: character as u32,
                language,
                fallback_group: fonts::FallbackGroup::None,
                flags: 0,
            })
            .await?;
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
        get_font_info(font_provider, name, None, '\0').await
    }

    fn start_provider_with_default_fonts() -> Result<(App, fonts::ProviderProxy), Error> {
        let launcher = launcher().context("Failed to open launcher service")?;
        let app = launch(&launcher, FONTS_CMX.to_string(), None)
            .context("Failed to launch fonts::Provider")?;

        let font_provider = app
            .connect_to_service::<fonts::ProviderMarker>()
            .context("Failed to connect to fonts::Provider")?;

        Ok((app, font_provider))
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_basic() -> Result<(), Error> {
        let (_app, font_provider) = start_provider_with_default_fonts()?;

        let default = get_font_info_basic(&font_provider, None)
            .await
            .context("Failed to load default font")?;
        let roboto = get_font_info_basic(&font_provider, Some("Roboto".to_string()))
            .await
            .context("Failed to load Roboto")?;
        let material_icons =
            get_font_info_basic(&font_provider, Some("Material Icons".to_string()))
                .await
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
        let materialicons = get_font_info_basic(&font_provider, Some("MaterialIcons".to_string()))
            .await
            .context("Failed to load MaterialIcons")?;
        let material_icons =
            get_font_info_basic(&font_provider, Some("Material Icons".to_string()))
                .await
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
            FONTS_CMX.to_string(),
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
        let noto_sans_cjk_ja = get_font_info(
            &font_provider,
            Some("NotoSansCJK".to_string()),
            Some(vec!["ja".to_string()]),
            '\0',
        )
        .await
        .context("Failed to load NotoSansCJK font")?;
        let noto_sans_cjk_sc = get_font_info(
            &font_provider,
            Some("NotoSansCJK".to_string()),
            Some(vec!["zh-Hans".to_string()]),
            '\0',
        )
        .await
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

        let noto_sans_cjk_ja = get_font_info(
            &font_provider,
            Some("NotoSansCJK".to_string()),
            Some(vec!["ja".to_string()]),
            '\0',
        )
        .await
        .context("Failed to load NotoSansCJK font")?;

        let noto_sans_cjk_ja_by_char = get_font_info(
            &font_provider,
            Some("Roboto".to_string()),
            Some(vec!["ja".to_string()]),
            'な',
        )
        .await
        .context("Failed to load NotoSansCJK font")?;

        // Same font should be returned in both cases.
        assert_buf_eq!(noto_sans_cjk_ja, noto_sans_cjk_ja_by_char);

        Ok(())
    }

    // Verify that the fallback group of the requested font is taken into account for fallback.
    #[fasync::run_singlethreaded(test)]
    async fn test_fallback_group() -> Result<(), Error> {
        let (_app, font_provider) = start_provider_with_test_fonts()?;

        let noto_serif_cjk_ja = get_font_info(
            &font_provider,
            Some("Noto Serif CJK".to_string()),
            Some(vec!["ja".to_string()]),
            '\0',
        )
        .await
        .context("Failed to load Noto Serif CJK font")?;

        let noto_serif_cjk_ja_by_char = get_font_info(
            &font_provider,
            Some("Roboto Slab".to_string()),
            Some(vec!["ja".to_string()]),
            'な',
        )
        .await
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

        let family_info = font_provider.get_family_info("materialicons").await?;

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
        crate::FONTS_CMX,
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
        let typeface = font_provider
            .get_typeface(fonts::TypefaceRequest {
                query: Some(fonts::TypefaceQuery {
                    family: name.as_ref().map(|name| fonts::FamilyName { name: name.to_string() }),
                    style: Some(fonts::Style2 {
                        weight: Some(fonts::WEIGHT_NORMAL),
                        width: Some(fonts::Width::SemiExpanded),
                        slant: Some(fonts::Slant::Upright),
                    }),
                    code_points: code_points
                        .map(|code_points| code_points.into_iter().map(|ch| ch as u32).collect()),
                    languages: languages.map(|languages| {
                        languages
                            .into_iter()
                            .map(|lang_code| intl::LocaleId { id: lang_code })
                            .collect()
                    }),
                    fallback_family: None,
                }),
                flags: Some(fonts::TypefaceRequestFlags::empty()),
            })
            .await?;

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
        get_typeface_info(font_provider, name, None, None).await
    }

    fn start_provider_with_default_fonts() -> Result<(App, fonts::ProviderProxy), Error> {
        let launcher = launcher().context("Failed to open launcher service")?;
        let app = launch(&launcher, FONTS_CMX.to_string(), None)
            .context("Failed to launch fonts::Provider")?;

        let font_provider = app
            .connect_to_service::<fonts::ProviderMarker>()
            .context("Failed to connect to fonts::Provider")?;

        Ok((app, font_provider))
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_basic() -> Result<(), Error> {
        let (_app, font_provider) = start_provider_with_default_fonts()?;

        let default = get_typeface_info_basic(&font_provider, None)
            .await
            .context("Failed to load default font")?;
        let roboto = get_typeface_info_basic(&font_provider, Some("Roboto".to_string()))
            .await
            .context("Failed to load Roboto")?;
        let material_icons =
            get_typeface_info_basic(&font_provider, Some("Material Icons".to_string()))
                .await
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
            get_typeface_info_basic(&font_provider, Some("MaterialIcons".to_string()))
                .await
                .context("Failed to load MaterialIcons")?;
        let material_icons =
            get_typeface_info_basic(&font_provider, Some("Material Icons".to_string()))
                .await
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
            FONTS_CMX.to_string(),
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
        let noto_sans_cjk_ja = get_typeface_info(
            &font_provider,
            Some("NotoSansCJK".to_string()),
            Some(vec!["ja".to_string()]),
            None,
        )
        .await
        .context("Failed to load NotoSansCJK font")?;
        let noto_sans_cjk_sc = get_typeface_info(
            &font_provider,
            Some("NotoSansCJK".to_string()),
            Some(vec!["zh-Hans".to_string()]),
            None,
        )
        .await
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

        let noto_sans_cjk_ja = get_typeface_info(
            &font_provider,
            Some("NotoSansCJK".to_string()),
            Some(vec!["ja".to_string()]),
            None,
        )
        .await
        .context("Failed to load NotoSansCJK font")?;

        let noto_sans_cjk_ja_by_char = get_typeface_info(
            &font_provider,
            Some("Roboto".to_string()),
            Some(vec!["ja".to_string()]),
            Some(vec!['な', 'ナ']),
        )
        .await
        .context("Failed to load NotoSansCJK font")?;

        // Same font should be returned in both cases.
        assert_buf_eq!(noto_sans_cjk_ja, noto_sans_cjk_ja_by_char);

        Ok(())
    }

    // Verify that the fallback group of the requested font is taken into account for fallback.
    #[fasync::run_singlethreaded(test)]
    async fn test_fallback_group() -> Result<(), Error> {
        let (_app, font_provider) = start_provider_with_test_fonts()?;

        let noto_serif_cjk_ja = get_typeface_info(
            &font_provider,
            Some("Noto Serif CJK".to_string()),
            Some(vec!["ja".to_string()]),
            None,
        )
        .await
        .context("Failed to load Noto Serif CJK font")?;

        let noto_serif_cjk_ja_by_char = get_typeface_info(
            &font_provider,
            Some("Roboto Slab".to_string()),
            Some(vec!["ja".to_string()]),
            Some(vec!['な']),
        )
        .await
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

        let font_family_info = font_provider
            .get_font_family_info(&mut fonts::FamilyName { name: "materialicons".to_string() })
            .await?;

        assert!(!font_family_info.is_empty());

        assert_eq!(font_family_info.name.unwrap().name, "Material Icons");
        assert!(font_family_info.styles.unwrap().len() > 0);

        Ok(())
    }
}

#[cfg(test)]
mod experimental_api {
    use {
        crate::FONTS_CMX,
        failure::{Error, ResultExt},
        fidl::endpoints::create_proxy,
        fidl_fuchsia_fonts as fonts, fidl_fuchsia_fonts_experimental as fonts_exp,
        fidl_fuchsia_intl::LocaleId,
        fuchsia_async as fasync,
        fuchsia_component::client::{launch, launch_with_options, launcher, App, LaunchOptions},
    };

    fn start_provider_with_default_fonts() -> Result<(App, fonts_exp::ProviderProxy), Error> {
        let launcher = launcher().context("Failed to open launcher service")?;
        let app = launch(&launcher, FONTS_CMX.to_string(), None)
            .context("Failed to launch fonts_exp::Provider")?;

        let font_provider = app
            .connect_to_service::<fonts_exp::ProviderMarker>()
            .context("Failed to connect to fonts_exp::Provider")?;

        Ok((app, font_provider))
    }

    fn start_provider_with_test_fonts() -> Result<(App, fonts_exp::ProviderProxy), Error> {
        let mut launch_options = LaunchOptions::new();
        launch_options.add_dir_to_namespace(
            "/test_fonts".to_string(),
            std::fs::File::open("/pkg/data/testdata/test_fonts")?,
        )?;

        let launcher = launcher().context("Failed to open launcher service")?;
        let app = launch_with_options(
            &launcher,
            FONTS_CMX.to_string(),
            Some(vec!["--font-manifest".to_string(), "/test_fonts/manifest.json".to_string()]),
            launch_options,
        )
        .context("Failed to launch fonts::Provider")?;

        let font_provider = app
            .connect_to_service::<fonts_exp::ProviderMarker>()
            .context("Failed to connect to fonts_exp::Provider")?;

        Ok((app, font_provider))
    }

    fn start_provider_with_all_fonts() -> Result<(App, fonts_exp::ProviderProxy), Error> {
        let mut launch_options = LaunchOptions::new();
        launch_options.add_dir_to_namespace(
            "/test_fonts".to_string(),
            std::fs::File::open("/pkg/data/testdata/test_fonts")?,
        )?;

        let launcher = launcher().context("Failed to open launcher service")?;
        let app = launch_with_options(
            &launcher,
            FONTS_CMX.to_string(),
            Some(vec![
                "--no-default-fonts".to_string(),
                "--font-manifest".to_string(),
                "/test_fonts/all_fonts_manifest.json".to_string(),
            ]),
            launch_options,
        )
        .context("Failed to launch fonts::Provider")?;

        let font_provider = app
            .connect_to_service::<fonts_exp::ProviderMarker>()
            .context("Failed to connect to fonts_exp::Provider")?;

        Ok((app, font_provider))
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_typeface_by_id() -> Result<(), Error> {
        let (_app, font_provider) = start_provider_with_default_fonts()?;
        // There will always be a font with index 0 unless manifest loading fails.
        let response = font_provider.get_typeface_by_id(0).await?.unwrap();
        assert_eq!(response.buffer_id, Some(0));
        assert!(response.buffer.is_some());
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_typeface_by_id_not_found() -> Result<(), Error> {
        let (_app, font_provider) = start_provider_with_default_fonts()?;
        let response = font_provider.get_typeface_by_id(std::u32::MAX).await?;
        assert_eq!(response.unwrap_err(), fonts_exp::Error::NotFound);
        Ok(())
    }

    fn roboto_info(id: u32, weight: u16) -> fonts_exp::TypefaceInfo {
        fonts_exp::TypefaceInfo {
            asset_id: Some(id),
            font_index: Some(0),
            family: Some(fonts::FamilyName { name: String::from("Roboto") }),
            style: Some(fonts::Style2 {
                slant: Some(fonts::Slant::Upright),
                weight: Some(weight),
                width: Some(fonts::Width::Normal),
            }),
            languages: Some(Vec::new()),
            generic_family: Some(fonts::GenericFontFamily::SansSerif),
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_typefaces_by_family() -> Result<(), Error> {
        let roboto = roboto_info(1, fonts::WEIGHT_NORMAL);
        let roboto_light = roboto_info(2, 300);
        let roboto_medium = roboto_info(3, 500);

        let (_app, font_provider) = start_provider_with_default_fonts()?;
        let mut family = fonts::FamilyName { name: String::from("Roboto") };

        let response = font_provider.get_typefaces_by_family(&mut family).await?;
        let faces = response.unwrap().results.unwrap();

        assert_eq!(faces.len(), 3);
        assert_eq!(faces[0], roboto);
        assert_eq!(faces[1], roboto_light);
        assert_eq!(faces[2], roboto_medium);
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_typefaces_by_family_alias() -> Result<(), Error> {
        let (_app, font_provider) = start_provider_with_default_fonts()?;
        let mut family = fonts::FamilyName { name: String::from("Material Icons") };
        let mut alias = fonts::FamilyName { name: String::from("MaterialIcons") };

        let by_family = font_provider.get_typefaces_by_family(&mut family).await?;
        let by_alias = font_provider.get_typefaces_by_family(&mut alias).await?;

        let by_family_faces = by_family.unwrap().results.unwrap();
        let by_alias_faces = by_alias.unwrap().results.unwrap();

        assert_eq!(by_family_faces.len(), 1);
        assert_eq!(by_alias_faces.len(), 1);
        assert_eq!(by_family_faces[0], by_alias_faces[0]);
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_typefaces_by_family_not_found() -> Result<(), Error> {
        let (_app, font_provider) = start_provider_with_default_fonts()?;
        let mut family = fonts::FamilyName { name: String::from("NoSuchFont") };
        let response = font_provider.get_typefaces_by_family(&mut family).await?;
        assert_eq!(response.unwrap_err(), fonts_exp::Error::NotFound);
        Ok(())
    }

    fn empty_list_typefaces_request() -> fonts_exp::ListTypefacesRequest {
        fonts_exp::ListTypefacesRequest {
            flags: None,
            family: None,
            slant: None,
            weight: None,
            width: None,
            languages: None,
            code_points: None,
            generic_family: None,
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_list_typefaces_empty_request_gets_all() -> Result<(), Error> {
        let (_app, font_provider) = start_provider_with_test_fonts()?;
        let (client, iterator) = create_proxy::<fonts_exp::ListTypefacesIteratorMarker>()?;

        let request = empty_list_typefaces_request();

        font_provider
            .list_typefaces(request, iterator)
            .await?
            .expect("ListTypefaces request failed");

        let response = client.get_next().await?;
        let results = response.results.unwrap();

        assert!(results.len() >= 12, "{:?}", results);
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_list_typefaces_no_results_after_last_page() -> Result<(), Error> {
        let (_app, font_provider) = start_provider_with_default_fonts()?;
        let (client, iterator) = create_proxy::<fonts_exp::ListTypefacesIteratorMarker>()?;

        let request = empty_list_typefaces_request();

        font_provider
            .list_typefaces(request, iterator.into())
            .await?
            .expect("ListTypefaces request failed");

        let first = client.get_next().await?.results.unwrap();
        let second = client.get_next().await?.results.unwrap();

        assert!(!first.is_empty(), "{:?}", first);
        assert!(second.is_empty(), "{:?}", second);
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_list_typefaces_paginates() -> Result<(), Error> {
        // Load all fonts to ensure results must be paginated
        let (_app, font_provider) = start_provider_with_all_fonts()?;
        let (client, iterator) = create_proxy::<fonts_exp::ListTypefacesIteratorMarker>()?;

        let request = empty_list_typefaces_request();

        font_provider
            .list_typefaces(request, iterator.into())
            .await?
            .expect("ListTypefaces request failed");

        let first = client.get_next().await?.results.unwrap();
        let second = client.get_next().await?.results.unwrap();

        assert!(!first.is_empty(), "{:?}", first);
        assert!(!second.is_empty(), "{:?}", second);

        // Results should be in manifest order
        assert!(first
            .iter()
            .any(|f| f.family == Some(fonts::FamilyName { name: "Material Icons".to_string() })));
        assert!(second
            .iter()
            .any(|f| f.family == Some(fonts::FamilyName { name: "Roboto Mono".to_string() })));

        // Pages should not share elements
        for result in first {
            assert!(!second.contains(&result));
        }
        Ok(())
    }

    fn name_query(name: &str) -> fonts_exp::ListTypefacesRequest {
        fonts_exp::ListTypefacesRequest {
            flags: None,
            family: Some(fonts::FamilyName { name: String::from(name) }),
            slant: None,
            weight: None,
            width: None,
            languages: None,
            code_points: None,
            generic_family: None,
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_list_typefaces_no_results_found() -> Result<(), Error> {
        let (_app, font_provider) = start_provider_with_test_fonts()?;
        let (client, iterator) = create_proxy::<fonts_exp::ListTypefacesIteratorMarker>()?;

        let request = name_query("404FontNotFound");

        font_provider
            .list_typefaces(request, iterator.into())
            .await?
            .expect("ListTypefaces request failed");

        let response = client.get_next().await?;
        let results = response.results.unwrap();

        assert!(results.is_empty(), "{:?}", results);
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_list_typefaces_by_name() -> Result<(), Error> {
        let (_app, font_provider) = start_provider_with_test_fonts()?;
        let (client, iterator) = create_proxy::<fonts_exp::ListTypefacesIteratorMarker>()?;

        let request = name_query("Roboto");

        font_provider
            .list_typefaces(request, iterator.into())
            .await?
            .expect("ListTypefaces request failed");

        let response = client.get_next().await?;
        let results = response.results.unwrap();

        assert_eq!(results.len(), 3, "{:?}", results);
        for result in &results {
            assert_eq!(result.family.as_ref().unwrap().name, "Roboto");
        }
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_list_typefaces_by_alias() -> Result<(), Error> {
        let (_app, font_provider) = start_provider_with_test_fonts()?;
        let (client, iterator) = create_proxy::<fonts_exp::ListTypefacesIteratorMarker>()?;

        let request = name_query("MaterialIcons");

        font_provider
            .list_typefaces(request, iterator.into())
            .await?
            .expect("ListTypefaces request failed");

        let response = client.get_next().await?;
        let results = response.results.unwrap();

        assert_eq!(results.len(), 1, "{:?}", results);
        assert_eq!(results[0].family.as_ref().unwrap().name, "Material Icons");
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_list_typefaces_by_name_ignores_case() -> Result<(), Error> {
        let (_app, font_provider) = start_provider_with_test_fonts()?;
        let (client, iterator) = create_proxy::<fonts_exp::ListTypefacesIteratorMarker>()?;

        let request = name_query("roboto");

        font_provider
            .list_typefaces(request, iterator.into())
            .await?
            .expect("ListTypefaces request failed");

        let response = client.get_next().await?;
        let results = response.results.unwrap();

        assert_eq!(results.len(), 3, "{:?}", results);
        for result in results {
            assert_eq!(result.family.as_ref().unwrap().name, "Roboto");
        }
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_list_typefaces_by_name_substring() -> Result<(), Error> {
        let (_app, font_provider) = start_provider_with_test_fonts()?;
        let (client, iterator) = create_proxy::<fonts_exp::ListTypefacesIteratorMarker>()?;

        let mut request = name_query("Noto");
        request.flags = Some(fonts_exp::ListTypefacesFlags::MatchFamilyNameSubstring);

        font_provider
            .list_typefaces(request, iterator.into())
            .await?
            .expect("ListTypefaces request failed");

        let response = client.get_next().await?;
        let results = response.results.unwrap();

        assert_eq!(results.len(), 8, "{:?}", results);
        for result in results {
            assert!(result.family.as_ref().unwrap().name.contains("Noto"));
        }
        Ok(())
    }

    fn slant_query(lower: fonts::Slant, upper: fonts::Slant) -> fonts_exp::ListTypefacesRequest {
        fonts_exp::ListTypefacesRequest {
            flags: None,
            family: None,
            slant: Some(fonts_exp::SlantRange { lower, upper }),
            weight: None,
            width: None,
            languages: None,
            code_points: None,
            generic_family: None,
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_list_typefaces_by_slant_range_() -> Result<(), Error> {
        let (_app, font_provider) = start_provider_with_all_fonts()?;
        let (client, iterator) = create_proxy::<fonts_exp::ListTypefacesIteratorMarker>()?;

        let request = slant_query(fonts::Slant::Upright, fonts::Slant::Italic);

        font_provider
            .list_typefaces(request, iterator)
            .await?
            .expect("ListTypefaces request failed");

        let response = client.get_next().await?;
        let results = response.results.unwrap();

        assert!(!results.is_empty(), "{:?}", results);
        for result in results {
            let slant = result.style.as_ref().unwrap().slant.unwrap();
            assert!((fonts::Slant::Upright..=fonts::Slant::Italic).contains(&slant));
        }
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_list_typefaces_by_slant_range_is_inclusive() -> Result<(), Error> {
        let (_app, font_provider) = start_provider_with_all_fonts()?;
        let (client, iterator) = create_proxy::<fonts_exp::ListTypefacesIteratorMarker>()?;

        let request = slant_query(fonts::Slant::Italic, fonts::Slant::Italic);

        font_provider
            .list_typefaces(request, iterator)
            .await?
            .expect("ListTypefaces request failed");

        let response = client.get_next().await?;
        let results = response.results.unwrap();

        assert!(!results.is_empty(), "{:?}", results);
        for result in results {
            let slant = result.style.as_ref().unwrap().slant.unwrap();
            assert_eq!(slant, fonts::Slant::Italic);
        }
        Ok(())
    }

    fn weight_query(lower: u16, upper: u16) -> fonts_exp::ListTypefacesRequest {
        fonts_exp::ListTypefacesRequest {
            flags: None,
            family: None,
            slant: None,
            weight: Some(fonts_exp::WeightRange { lower, upper }),
            width: None,
            languages: None,
            code_points: None,
            generic_family: None,
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_list_typefaces_by_weight_range() -> Result<(), Error> {
        let (_app, font_provider) = start_provider_with_test_fonts()?;
        let (client, iterator) = create_proxy::<fonts_exp::ListTypefacesIteratorMarker>()?;

        let request = weight_query(200, 300);

        font_provider
            .list_typefaces(request, iterator)
            .await?
            .expect("ListTypefaces request failed");

        let response = client.get_next().await?;
        let results = response.results.unwrap();

        assert!(!results.is_empty(), "{:?}", results);
        for result in results {
            let weight = result.style.as_ref().unwrap().weight.unwrap();
            assert!((200..=300).contains(&weight));
        }
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_list_typefaces_by_weight_range_is_inclusive() -> Result<(), Error> {
        let (_app, font_provider) = start_provider_with_test_fonts()?;
        let (client, iterator) = create_proxy::<fonts_exp::ListTypefacesIteratorMarker>()?;

        let request = weight_query(300, 300);

        font_provider
            .list_typefaces(request, iterator)
            .await?
            .expect("ListTypefaces request failed");

        let response = client.get_next().await?;
        let results = response.results.unwrap();

        assert!(!results.is_empty(), "{:?}", results);
        for result in results {
            let weight = result.style.as_ref().unwrap().weight.unwrap();
            assert_eq!(weight, 300);
        }
        Ok(())
    }

    fn width_query(lower: fonts::Width, upper: fonts::Width) -> fonts_exp::ListTypefacesRequest {
        fonts_exp::ListTypefacesRequest {
            flags: None,
            family: None,
            slant: None,
            weight: None,
            width: Some(fonts_exp::WidthRange { lower, upper }),
            languages: None,
            code_points: None,
            generic_family: None,
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_list_typefaces_by_width_range() -> Result<(), Error> {
        let (_app, font_provider) = start_provider_with_test_fonts()?;
        let (client, iterator) = create_proxy::<fonts_exp::ListTypefacesIteratorMarker>()?;

        let request = width_query(fonts::Width::Condensed, fonts::Width::Expanded);

        font_provider
            .list_typefaces(request, iterator)
            .await?
            .expect("ListTypefaces request failed");

        let response = client.get_next().await?;
        let results = response.results.unwrap();

        assert!(!results.is_empty(), "{:?}", results);
        for result in results {
            let width = result.style.as_ref().unwrap().width.unwrap();
            assert!((fonts::Width::Condensed..=fonts::Width::Expanded).contains(&width));
        }
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_list_typefaces_by_width_range_is_inclusive() -> Result<(), Error> {
        let (_app, font_provider) = start_provider_with_test_fonts()?;
        let (client, iterator) = create_proxy::<fonts_exp::ListTypefacesIteratorMarker>()?;

        let request = width_query(fonts::Width::Normal, fonts::Width::Normal);

        font_provider
            .list_typefaces(request, iterator)
            .await?
            .expect("ListTypefaces request failed");

        let response = client.get_next().await?;
        let results = response.results.unwrap();

        assert!(!results.is_empty(), "{:?}", results);
        for result in results {
            let width = result.style.as_ref().unwrap().width.unwrap();
            assert_eq!(width, fonts::Width::Normal);
        }
        Ok(())
    }

    fn locale(lang: &str) -> LocaleId {
        LocaleId { id: String::from(lang) }
    }

    fn lang_query(langs: Vec<LocaleId>) -> fonts_exp::ListTypefacesRequest {
        fonts_exp::ListTypefacesRequest {
            flags: None,
            family: None,
            slant: None,
            weight: None,
            width: None,
            languages: Some(langs),
            code_points: None,
            generic_family: None,
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_list_typefaces_by_language() -> Result<(), Error> {
        let (_app, font_provider) = start_provider_with_test_fonts()?;
        let (client, iterator) = create_proxy::<fonts_exp::ListTypefacesIteratorMarker>()?;

        let request = lang_query(vec![locale("ja")]);

        font_provider
            .list_typefaces(request, iterator.into())
            .await?
            .expect("ListTypefaces request failed");

        let response = client.get_next().await?;
        let results = response.results.unwrap();

        assert_eq!(results.len(), 2, "{:?}", results);
        for result in results {
            assert!(result.languages.unwrap().contains(&locale("ja")));
        }
        Ok(())
    }

    fn code_point_query(points: Vec<u32>) -> fonts_exp::ListTypefacesRequest {
        fonts_exp::ListTypefacesRequest {
            flags: None,
            family: None,
            slant: None,
            weight: None,
            width: None,
            languages: None,
            code_points: Some(points),
            generic_family: None,
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_list_typefaces_by_code_point() -> Result<(), Error> {
        let (_app, font_provider) = start_provider_with_test_fonts()?;
        let (client, iterator) = create_proxy::<fonts_exp::ListTypefacesIteratorMarker>()?;

        let request = code_point_query(vec!['な' as u32]);

        font_provider
            .list_typefaces(request, iterator.into())
            .await?
            .expect("ListTypefaces request failed");

        let response = client.get_next().await?;
        let results = response.results.unwrap();

        assert!(!results.is_empty());
        for result in results {
            assert!(result.family.as_ref().unwrap().name.contains("CJK"));
        }
        Ok(())
    }

    fn generic_family_query(
        generic_family: fonts::GenericFontFamily,
    ) -> fonts_exp::ListTypefacesRequest {
        fonts_exp::ListTypefacesRequest {
            flags: None,
            family: None,
            slant: None,
            weight: None,
            width: None,
            languages: None,
            code_points: None,
            generic_family: Some(generic_family),
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_list_typefaces_by_generic_family() -> Result<(), Error> {
        let (_app, font_provider) = start_provider_with_test_fonts()?;
        let (client, iterator) = create_proxy::<fonts_exp::ListTypefacesIteratorMarker>()?;

        let request = generic_family_query(fonts::GenericFontFamily::SansSerif);

        font_provider
            .list_typefaces(request, iterator.into())
            .await?
            .expect("ListTypefaces request failed");

        let response = client.get_next().await?;
        let results = response.results.unwrap();

        assert!(!results.is_empty());
        for result in results {
            assert_eq!(
                result.generic_family.as_ref().unwrap(),
                &fonts::GenericFontFamily::SansSerif
            );
        }
        Ok(())
    }
}
