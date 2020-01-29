use std::fmt;
use std::ops::{Add, Mul};

use serde::de::Visitor;
use serde::{Deserialize, Deserializer};
use serde_derive::Deserialize;

use crate::config::{failure_default, Delta};

/// Font size stored as integer
#[derive(Debug, Copy, Clone, Hash, PartialEq, Eq, PartialOrd, Ord)]
pub struct Size(i16);

impl Size {
    /// Create a new `Size` from a f32 size in points
    pub fn new(size: f32) -> Size {
        Size((size * Size::factor()) as i16)
    }

    /// Scale factor between font "Size" type and point size
    #[inline]
    pub fn factor() -> f32 {
        2.0
    }

    /// Get the f32 size in points
    pub fn as_f32_pts(self) -> f32 {
        f32::from(self.0) / Size::factor()
    }
}

impl<T: Into<Size>> Add<T> for Size {
    type Output = Size;

    fn add(self, other: T) -> Size {
        Size(self.0.saturating_add(other.into().0))
    }
}

impl<T: Into<Size>> Mul<T> for Size {
    type Output = Size;

    fn mul(self, other: T) -> Size {
        Size(self.0 * other.into().0)
    }
}

impl From<f32> for Size {
    fn from(float: f32) -> Size {
        Size::new(float)
    }
}

/// Font config
///
/// Defaults are provided at the level of this struct per platform, but not per
/// field in this struct. It might be nice in the future to have defaults for
/// each value independently. Alternatively, maybe erroring when the user
/// doesn't provide complete config is Ok.
#[serde(default)]
#[derive(Debug, Deserialize, Clone, PartialEq, Eq)]
pub struct Font {
    /// Normal font face
    #[serde(deserialize_with = "failure_default")]
    normal: FontDescription,

    /// Bold font face
    #[serde(deserialize_with = "failure_default")]
    bold: SecondaryFontDescription,

    /// Italic font face
    #[serde(deserialize_with = "failure_default")]
    italic: SecondaryFontDescription,

    /// Bold italic font face
    #[serde(deserialize_with = "failure_default")]
    bold_italic: SecondaryFontDescription,

    /// Font size in points
    #[serde(deserialize_with = "DeserializeSize::deserialize")]
    pub size: Size,

    /// Extra spacing per character
    #[serde(deserialize_with = "failure_default")]
    pub offset: Delta<i8>,

    /// Glyph offset within character cell
    #[serde(deserialize_with = "failure_default")]
    pub glyph_offset: Delta<i8>,
}

impl Default for Font {
    fn default() -> Font {
        Font {
            size: default_font_size(),
            normal: Default::default(),
            bold: Default::default(),
            italic: Default::default(),
            bold_italic: Default::default(),
            glyph_offset: Default::default(),
            offset: Default::default(),
        }
    }
}

impl Font {
    /// Get a font clone with a size modification
    pub fn with_size(self, size: Size) -> Font {
        Font { size, ..self }
    }

    // Get normal font description
    pub fn normal(&self) -> &FontDescription {
        &self.normal
    }

    // Get bold font description
    pub fn bold(&self) -> FontDescription {
        self.bold.desc(&self.normal)
    }

    // Get italic font description
    pub fn italic(&self) -> FontDescription {
        self.italic.desc(&self.normal)
    }

    // Get bold italic font description
    pub fn bold_italic(&self) -> FontDescription {
        self.bold_italic.desc(&self.normal)
    }
}

fn default_font_size() -> Size {
    Size::new(11.)
}

/// Description of the normal font
#[serde(default)]
#[derive(Debug, Deserialize, Clone, PartialEq, Eq)]
pub struct FontDescription {
    #[serde(deserialize_with = "failure_default")]
    pub family: String,
    #[serde(deserialize_with = "failure_default")]
    pub style: Option<String>,
}

impl Default for FontDescription {
    fn default() -> FontDescription {
        FontDescription {
            #[cfg(not(any(target_os = "macos", windows)))]
            family: "monospace".into(),
            #[cfg(target_os = "macos")]
            family: "Menlo".into(),
            #[cfg(windows)]
            family: "Consolas".into(),
            style: None,
        }
    }
}

/// Description of the italic and bold font
#[serde(default)]
#[derive(Debug, Default, Deserialize, Clone, PartialEq, Eq)]
pub struct SecondaryFontDescription {
    #[serde(deserialize_with = "failure_default")]
    family: Option<String>,
    #[serde(deserialize_with = "failure_default")]
    style: Option<String>,
}

impl SecondaryFontDescription {
    pub fn desc(&self, fallback: &FontDescription) -> FontDescription {
        FontDescription {
            family: self.family.clone().unwrap_or_else(|| fallback.family.clone()),
            style: self.style.clone(),
        }
    }
}

trait DeserializeSize: Sized {
    fn deserialize<'a, D>(_: D) -> ::std::result::Result<Self, D::Error>
    where
        D: serde::de::Deserializer<'a>;
}

impl DeserializeSize for Size {
    fn deserialize<'a, D>(deserializer: D) -> ::std::result::Result<Self, D::Error>
    where
        D: serde::de::Deserializer<'a>,
    {
        use std::marker::PhantomData;

        struct NumVisitor<__D> {
            _marker: PhantomData<__D>,
        }

        impl<'a, __D> Visitor<'a> for NumVisitor<__D>
        where
            __D: serde::de::Deserializer<'a>,
        {
            type Value = f64;

            fn expecting(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
                f.write_str("f64 or u64")
            }

            fn visit_f64<E>(self, value: f64) -> ::std::result::Result<Self::Value, E>
            where
                E: ::serde::de::Error,
            {
                Ok(value)
            }

            fn visit_u64<E>(self, value: u64) -> ::std::result::Result<Self::Value, E>
            where
                E: ::serde::de::Error,
            {
                Ok(value as f64)
            }
        }

        let value = serde_json::Value::deserialize(deserializer)?;
        let size = value
            .deserialize_any(NumVisitor::<D> { _marker: PhantomData })
            .map(|v| Size::new(v as _));

        // Use default font size as fallback
        match size {
            Ok(size) => Ok(size),
            Err(_err) => {
                let size = default_font_size();

                Ok(size)
            }
        }
    }
}
