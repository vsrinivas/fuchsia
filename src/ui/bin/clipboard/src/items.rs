// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{defaults, errors::ClipboardError},
    fidl_fuchsia_ui_clipboard as fclip,
    tracing::warn,
};

/// Internal representation of a single clipboard item, equivalent to
/// [`fidl_fuchsia_ui_clipboard::ClipboardItem`].
#[derive(Debug, Clone, Eq, PartialEq, Hash)]
pub(crate) struct ClipboardItem {
    mime_type_hint: String,
    payload: Payload,
}

impl ClipboardItem {
    /// Creates a new `ClipboardItem` containing the given string.
    ///
    /// If `mime_type_hint` is omitted, the [default](crate::defaults::MIME_TYPE_HINT) is used.
    pub fn new_text_item(text: impl AsRef<str>, mime_type_hint: impl IntoOptionString) -> Self {
        let mime_type_hint = mime_type_hint
            .into_option_string()
            .unwrap_or_else(|| defaults::MIME_TYPE_HINT.to_string());
        Self { mime_type_hint, payload: Payload::Text { text: text.as_ref().to_string() } }
    }

    /// Returns the MIME type hint.
    pub fn mime_type_hint(&self) -> &str {
        &self.mime_type_hint
    }

    /// Returns the size of the payload in bytes.
    pub fn payload_size_bytes(&self) -> usize {
        match &self.payload {
            Payload::Text { text } => text.as_bytes().len(),
        }
    }
}

/// Equivalent to [`fidl_fuchsia_ui_clipboard::ClipboardItemData`].
#[derive(Debug, Clone, Eq, PartialEq, Hash)]
pub(crate) enum Payload {
    Text { text: String },
}

impl From<&Payload> for fclip::ClipboardItemData {
    fn from(src: &Payload) -> Self {
        match src {
            Payload::Text { text } => fclip::ClipboardItemData::Text(text.to_owned()),
        }
    }
}

impl From<Payload> for fclip::ClipboardItemData {
    fn from(src: Payload) -> Self {
        match src {
            Payload::Text { text } => fclip::ClipboardItemData::Text(text.to_owned()),
        }
    }
}

impl TryFrom<fclip::ClipboardItem> for ClipboardItem {
    type Error = ClipboardError;

    fn try_from(src: fclip::ClipboardItem) -> Result<Self, Self::Error> {
        let src_payload = src.payload.as_ref().ok_or_else(|| {
            warn!("Invalid {src:?}");
            ClipboardError::InvalidRequest
        })?;

        let dst = match src_payload {
            fclip::ClipboardItemData::Text(text) => Self::new_text_item(text, src.mime_type_hint),
            _ => {
                warn!("Unsupported {src:?}");
                return Err(ClipboardError::InvalidRequest);
            }
        };
        Ok(dst)
    }
}

impl From<&ClipboardItem> for fclip::ClipboardItem {
    fn from(src: &ClipboardItem) -> Self {
        fclip::ClipboardItem {
            mime_type_hint: Some(src.mime_type_hint.to_owned()),
            payload: Some((&src.payload).into()),
            ..fclip::ClipboardItem::EMPTY
        }
    }
}

/// Handy trait for optional string arguments, because Rust doesn't support
/// `impl Into<Option<impl Into<String>>>`.
pub(crate) trait IntoOptionString {
    fn into_option_string(self) -> Option<String>;
}

impl IntoOptionString for Option<String> {
    fn into_option_string(self) -> Option<String> {
        self.into()
    }
}

impl IntoOptionString for &Option<String> {
    fn into_option_string(self) -> Option<String> {
        self.to_owned()
    }
}

impl<'a> IntoOptionString for Option<&'a str> {
    fn into_option_string(self) -> Option<String> {
        let s: Option<&str> = self.into();
        s.map(|s| s.to_owned())
    }
}

impl<'a> IntoOptionString for &'a str {
    fn into_option_string(self) -> Option<String> {
        Some(self.to_owned())
    }
}

impl IntoOptionString for () {
    fn into_option_string(self) -> Option<String> {
        None
    }
}

#[cfg(test)]
mod tests {
    use {super::*, assert_matches::assert_matches};

    #[fuchsia::test]
    fn new_text_item_with_mime_type() {
        let actual = ClipboardItem::new_text_item("abc", "application/json");
        let expected = ClipboardItem {
            mime_type_hint: "application/json".to_string(),
            payload: Payload::Text { text: "abc".to_string() },
        };
        assert_eq!(actual, expected);
    }

    #[fuchsia::test]
    fn new_text_item_without_mime_type() {
        let actual = ClipboardItem::new_text_item("abc", ());
        let expected = ClipboardItem {
            mime_type_hint: "text/plain;charset=utf-8".to_string(),
            payload: Payload::Text { text: "abc".to_string() },
        };
        assert_eq!(actual, expected);
    }

    #[fuchsia::test]
    fn try_from_missing_payload() {
        let src = fclip::ClipboardItem {
            mime_type_hint: Some("text/json".to_string()),
            payload: None,
            ..fclip::ClipboardItem::EMPTY
        };
        let actual: Result<ClipboardItem, ClipboardError> = src.try_into();
        assert_matches!(actual, Err(ClipboardError::InvalidRequest));
    }
}
