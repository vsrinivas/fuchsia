// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file

pub use {
    crate::util,
    crate::FONTS_EPHEMERAL_CM,
    anyhow::{Context as _, Error},
    fidl::endpoints::create_proxy,
    fidl_fuchsia_fonts as fonts, fidl_fuchsia_fonts_experimental as fonts_exp,
    fidl_fuchsia_intl::LocaleId,
    fuchsia_async as fasync,
    futures::lock::Mutex,
};

pub async fn get_provider(fonts_cm: &'static str) -> Result<fonts_exp::ProviderProxy, Error> {
    util::get_provider::<fonts_exp::ProviderMarker>(fonts_cm).await
}

pub fn roboto_info(id: u32, weight: u16) -> fonts_exp::TypefaceInfo {
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

pub fn empty_list_typefaces_request() -> fonts_exp::ListTypefacesRequest {
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

pub fn name_query(name: &str) -> fonts_exp::ListTypefacesRequest {
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

pub fn slant_query(lower: fonts::Slant, upper: fonts::Slant) -> fonts_exp::ListTypefacesRequest {
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

pub fn weight_query(lower: u16, upper: u16) -> fonts_exp::ListTypefacesRequest {
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

pub fn width_query(lower: fonts::Width, upper: fonts::Width) -> fonts_exp::ListTypefacesRequest {
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

pub fn locale(lang: &str) -> LocaleId {
    LocaleId { id: String::from(lang) }
}

pub fn lang_query(langs: Vec<LocaleId>) -> fonts_exp::ListTypefacesRequest {
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

pub fn code_point_query(points: Vec<u32>) -> fonts_exp::ListTypefacesRequest {
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

pub fn generic_family_query(
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
