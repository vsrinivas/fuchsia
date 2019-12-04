// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::encoding::Decodable,
    fidl_fuchsia_fonts::{
        self as fonts, CacheMissPolicy, FallbackGroup, FamilyName, FontFamilyInfo,
        GenericFontFamily, Style2, TypefaceQuery, TypefaceRequest, TypefaceRequestFlags,
        TypefaceResponse, Width,
    },
    fidl_fuchsia_intl as intl,
};

/// Extensions for [`FallbackGroup`](fidl_fuchsia_fonts::FallbackGroup).
pub trait FallbackGroupExt {
    fn to_generic_font_family(&self) -> Option<GenericFontFamily>;
}

impl FallbackGroupExt for FallbackGroup {
    fn to_generic_font_family(&self) -> Option<GenericFontFamily> {
        match self {
            FallbackGroup::None => None,
            FallbackGroup::Serif => Some(GenericFontFamily::Serif),
            FallbackGroup::SansSerif => Some(GenericFontFamily::SansSerif),
            FallbackGroup::Monospace => Some(GenericFontFamily::Monospace),
            FallbackGroup::Cursive => Some(GenericFontFamily::Cursive),
            FallbackGroup::Fantasy => Some(GenericFontFamily::Fantasy),
        }
    }
}

/// Extensions for [`Request`](fidl_fuchsia_fonts::Request).
pub trait RequestExt {
    fn into_typeface_request(self) -> TypefaceRequest;
}

impl RequestExt for fonts::Request {
    fn into_typeface_request(self) -> TypefaceRequest {
        let family: Option<FamilyName> = match self.family {
            Some(family) => Some(FamilyName { name: family }),
            None => None,
        };

        let style: Option<Style2> = Some(Style2 {
            weight: Some(self.weight as u16),
            slant: Some(self.slant),
            width: Width::from_primitive(self.width),
        });

        let languages: Option<Vec<intl::LocaleId>> = self.language.map(|languages| {
            languages.iter().map(|lang_code| intl::LocaleId { id: lang_code.to_string() }).collect()
        });

        let mut flags = TypefaceRequestFlags::empty();
        if (self.flags & fonts::REQUEST_FLAG_NO_FALLBACK) != 0 {
            flags |= TypefaceRequestFlags::ExactFamily;
        }
        if (self.flags & fonts::REQUEST_FLAG_EXACT_MATCH) != 0 {
            flags |= TypefaceRequestFlags::ExactStyle;
        }

        TypefaceRequest {
            query: Some(TypefaceQuery {
                family,
                style,
                languages,
                code_points: match self.character {
                    ch if ch > 0 => Some(vec![ch]),
                    _ => None,
                },
                fallback_family: self.fallback_group.to_generic_font_family(),
            }),
            flags: Some(flags),
            cache_miss_policy: None,
        }
    }
}

/// Extensions for [`TypefaceRequest`](fidl_fuchsia_fonts::TypefaceRequest).
pub trait TypefaceRequestExt {
    /// See [`fidl_fuchsia_fonts::TypefaceRequestFlags::ExactFamily`].
    fn exact_family(&self) -> bool;

    /// See [`fidl_fuchsia_fonts::TypefaceRequestFlags::ExactStyle`].
    fn exact_style(&self) -> bool;

    /// See ['fidl_fuchsia_fonts::CacheMissPolicy`].
    fn cache_miss_policy(&self) -> CacheMissPolicy;
}

impl TypefaceRequestExt for TypefaceRequest {
    fn exact_family(&self) -> bool {
        self.flags.map_or(false, |flags| flags.contains(fonts::TypefaceRequestFlags::ExactFamily))
    }

    fn exact_style(&self) -> bool {
        self.flags.map_or(false, |flags| flags.contains(fonts::TypefaceRequestFlags::ExactStyle))
    }

    fn cache_miss_policy(&self) -> CacheMissPolicy {
        self.cache_miss_policy.unwrap_or(CacheMissPolicy::BlockUntilDownloaded)
    }
}

/// Extensions for [`TypefaceResponse`](fidl_fuchsia_fonts::TypefaceResponse).
pub trait TypefaceResponseExt {
    fn into_font_response(self) -> Option<fonts::Response>;
}

impl TypefaceResponseExt for TypefaceResponse {
    fn into_font_response(self) -> Option<fonts::Response> {
        if self.is_empty() {
            None
        } else {
            Some(fonts::Response {
                buffer: self.buffer.unwrap(),
                buffer_id: self.buffer_id.unwrap(),
                font_index: self.font_index.unwrap(),
            })
        }
    }
}

/// Extensions for [`FontFamilyInfo`](fidl_fuchsia_fonts::FontFamilyInfo).
pub trait FontFamilyInfoExt {
    fn into_family_info(self) -> Option<fonts::FamilyInfo>;
}

impl FontFamilyInfoExt for FontFamilyInfo {
    fn into_family_info(self) -> Option<fonts::FamilyInfo> {
        if self.is_empty() {
            None
        } else {
            Some(fonts::FamilyInfo {
                name: self.name.unwrap().name,
                styles: self
                    .styles
                    .unwrap()
                    .into_iter()
                    .flat_map(|style2| style2.into_style())
                    .collect(),
            })
        }
    }
}

/// Extensions for [`Style2`](fidl_fuchsia_fonts::Style2).
pub trait Style2Ext {
    fn into_style(self) -> Option<fonts::Style>;
}

impl Style2Ext for Style2 {
    fn into_style(self) -> Option<fonts::Style> {
        if self.is_empty() {
            None
        } else {
            Some(fonts::Style {
                weight: self.weight.unwrap() as u32, // Expanded from u16
                width: self.width.unwrap().into_primitive(),
                slant: self.slant.unwrap(),
            })
        }
    }
}

/// Extensions for [`Decodable`](fidl::encoding::Decodable).
pub trait DecodableExt {
    fn is_empty(&self) -> bool;
}

impl<T> DecodableExt for T
where
    T: Decodable + PartialEq + Sized,
{
    /// Test whether a table or other FIDL message is empty.
    fn is_empty(&self) -> bool {
        let empty: T = Decodable::new_empty();
        return *self == empty;
    }
}
