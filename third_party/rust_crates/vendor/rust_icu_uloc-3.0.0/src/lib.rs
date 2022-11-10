// Copyright 2019 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

use {
    rust_icu_common as common,
    rust_icu_common::buffered_string_method_with_retry,
    rust_icu_sys::versioned_function,
    rust_icu_sys::*,
    rust_icu_uenum::Enumeration,
    std::{
        cmp::Ordering,
        collections::HashMap,
        convert::{From, TryFrom},
        ffi, fmt,
        os::raw,
    },
};

/// Maximum length of locale supported by uloc.h.
/// See `ULOC_FULLNAME_CAPACITY`.
const LOCALE_CAPACITY: usize = 158;

/// [ULocMut] is a mutable companion to [ULoc].
///
/// It has methods that allow one to create a different [ULoc] by adding and
/// removing keywords to the locale identifier.  You can only creates a `ULocMut`
/// by converting from an existing `ULoc` by calling `ULocMut::from`.  And once
/// you are done changing it, you can only convert it back with `ULoc::from`.
///
/// [ULocMut] is not meant to have comprehensive coverage of mutation options.
/// They may be added as necessary.
#[derive(Debug, Clone)]
pub struct ULocMut {
    base: ULoc,
    unicode_keyvalues: HashMap<String, String>,
    other_keyvalues: HashMap<String, String>,
}

impl From<ULoc> for ULocMut {
    /// Turns [ULoc] into [ULocMut], which can be mutated.
    fn from(l: ULoc) -> Self {
        let all_keywords = l.keywords();
        let mut unicode_keyvalues: HashMap<String, String> = HashMap::new();
        let mut other_keyvalues: HashMap<String, String> = HashMap::new();
        for kw in all_keywords {
            // Despite the many unwraps below, none should be triggered, since we know
            // that the keywords come from the list of keywords that already exist.
            let ukw = to_unicode_locale_key(&kw);
            match ukw {
                None => {
                    let v = l.keyword_value(&kw).unwrap().unwrap();
                    other_keyvalues.insert(kw, v);
                }
                Some(u) => {
                    let v = l.unicode_keyword_value(&u).unwrap().unwrap();
                    unicode_keyvalues.insert(u, v);
                }
            }
        }
        // base_name may return an invalid language tag, so convert here.
        let locmut = ULocMut {
            base: l.base_name(),
            unicode_keyvalues,
            other_keyvalues,
        };
        locmut
    }
}

impl From<ULocMut> for ULoc {
    // Creates an [ULoc] from [ULocMut].
    fn from(lm: ULocMut) -> Self {
        // Assemble the unicode extension.
        let mut unicode_extensions_vec = lm
            .unicode_keyvalues
            .iter()
            .map(|(k, v)| format!("{}-{}", k, v))
            .collect::<Vec<String>>();
        unicode_extensions_vec.sort();
        let unicode_extensions: String = unicode_extensions_vec
            .join("-");
        let unicode_extension: String = if unicode_extensions.len() > 0 {
            vec!["u-".to_string(), unicode_extensions]
                .into_iter()
                .collect()
        } else {
            "".to_string()
        };
        // Assemble all other extensions.
        let mut all_extensions: Vec<String> = lm
            .other_keyvalues
            .iter()
            .map(|(k, v)| format!("{}-{}", k, v))
            .collect();
        if unicode_extension.len() > 0 {
            all_extensions.push(unicode_extension);
        }
        // The base language must be in the form of BCP47 language tag to
        // be usable in the code below.
        let base_tag = lm.base.to_language_tag(true)
            .expect("should be known-good");
        let mut everything_vec: Vec<String> = vec![base_tag];
        if !all_extensions.is_empty() {
            all_extensions.sort();
            let extension_string = all_extensions.join("-");
            everything_vec.push(extension_string);
        }
        let everything = everything_vec.join("-").to_lowercase();
        ULoc::for_language_tag(&everything).unwrap()
    }
}

impl ULocMut {
    /// Sets the specified unicode extension keyvalue.  Only valid keys can be set,
    /// inserting an invalid extension key does not change [ULocMut].
    pub fn set_unicode_keyvalue(&mut self, key: &str, value: &str) -> Option<String> {
        if let None = to_unicode_locale_key(key) {
            return None;
        }
        self.unicode_keyvalues
            .insert(key.to_string(), value.to_string())
    }

    /// Removes the specified unicode extension keyvalue.  Only valid keys can
    /// be removed, attempting to remove an invalid extension key does not
    /// change [ULocMut].
    pub fn remove_unicode_keyvalue(&mut self, key: &str) -> Option<String> {
        if let None = to_unicode_locale_key(key) {
            return None;
        }
        self.unicode_keyvalues.remove(key)
    }
}

/// A representation of a Unicode locale.
///
/// For the time being, only basic conversion and methods are in fact implemented.
///
/// To get basic validation when creating a locale, use
/// [`for_language_tag`](ULoc::for_language_tag) with a Unicode BCP-47 locale ID.
#[derive(Debug, Clone, Eq, PartialEq, Hash)]
pub struct ULoc {
    // A locale's representation in C is really just a string.
    repr: String,
}

/// Implement the Display trait to convert the ULoc into string for display.
///
/// The string for display and string serialization happen to be the same for [ULoc].
impl fmt::Display for ULoc {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.repr)
    }
}

impl TryFrom<&str> for ULoc {
    type Error = common::Error;
    /// Creates a new ULoc from a string slice.
    ///
    /// The creation wil fail if the locale is nonexistent.
    fn try_from(s: &str) -> Result<Self, Self::Error> {
        let s = String::from(s);
        ULoc { repr: s }.canonicalize()
    }
}

impl TryFrom<&ffi::CStr> for ULoc {
    type Error = common::Error;

    /// Creates a new `ULoc` from a borrowed C string.
    fn try_from(s: &ffi::CStr) -> Result<Self, Self::Error> {
        let repr = s.to_str()?;
        ULoc {
            repr: String::from(repr),
        }
        .canonicalize()
    }
}

impl ULoc {
    /// Implements `uloc_getLanguage`.
    pub fn language(&self) -> Option<String> {
        self.call_buffered_string_method_to_option(versioned_function!(uloc_getLanguage))
    }

    /// Implements `uloc_getScript`.
    pub fn script(&self) -> Option<String> {
        self.call_buffered_string_method_to_option(versioned_function!(uloc_getScript))
    }

    /// Implements `uloc_getCountry`.
    pub fn country(&self) -> Option<String> {
        self.call_buffered_string_method_to_option(versioned_function!(uloc_getCountry))
    }

    /// Implements `uloc_getVariant`.
    pub fn variant(&self) -> Option<String> {
        self.call_buffered_string_method_to_option(versioned_function!(uloc_getVariant))
    }

    /// Implements `uloc_canonicalize` from ICU4C.
    pub fn canonicalize(&self) -> Result<ULoc, common::Error> {
        self.call_buffered_string_method(versioned_function!(uloc_canonicalize))
            .map(|repr| ULoc { repr })
    }

    /// Implements `uloc_addLikelySubtags` from ICU4C.
    pub fn add_likely_subtags(&self) -> Result<ULoc, common::Error> {
        self.call_buffered_string_method(versioned_function!(uloc_addLikelySubtags))
            .map(|repr| ULoc { repr })
    }

    /// Implements `uloc_minimizeSubtags` from ICU4C.
    pub fn minimize_subtags(&self) -> Result<ULoc, common::Error> {
        self.call_buffered_string_method(versioned_function!(uloc_minimizeSubtags))
            .map(|repr| ULoc { repr })
    }

    /// Implements `uloc_toLanguageTag` from ICU4C.
    pub fn to_language_tag(&self, strict: bool) -> Result<String, common::Error> {
        let locale_id = self.as_c_str();
        // No `UBool` constants available in rust_icu_sys, unfortunately.
        let strict = if strict { 1 } else { 0 };
        buffered_string_method_with_retry(
            |buf, len, error| unsafe {
                versioned_function!(uloc_toLanguageTag)(
                    locale_id.as_ptr(),
                    buf,
                    len,
                    strict,
                    error,
                )
            },
            LOCALE_CAPACITY,
        )
    }

    /// Implements `uloc_openKeywords()` from ICU4C.
    pub fn keywords(&self) -> impl Iterator<Item = String> {
        rust_icu_uenum::uloc_open_keywords(&self.repr)
            .unwrap()
            .map(|result| result.unwrap())
    }

    /// Implements `icu::Locale::getUnicodeKeywords()` from the C++ API.
    pub fn unicode_keywords(&self) -> impl Iterator<Item = String> {
        self.keywords().filter_map(|s| to_unicode_locale_key(&s))
    }

    /// Implements `uloc_getKeywordValue()` from ICU4C.
    pub fn keyword_value(&self, keyword: &str) -> Result<Option<String>, common::Error> {
        let locale_id = self.as_c_str();
        let keyword_name = str_to_cstring(keyword);
        buffered_string_method_with_retry(
            |buf, len, error| unsafe {
                versioned_function!(uloc_getKeywordValue)(
                    locale_id.as_ptr(),
                    keyword_name.as_ptr(),
                    buf,
                    len,
                    error,
                )
            },
            LOCALE_CAPACITY,
        )
        .map(|value| if value.is_empty() { None } else { Some(value) })
    }

    /// Implements `icu::Locale::getUnicodeKeywordValue()` from ICU4C.
    pub fn unicode_keyword_value(
        &self,
        unicode_keyword: &str,
    ) -> Result<Option<String>, common::Error> {
        let legacy_keyword = to_legacy_key(unicode_keyword);
        match legacy_keyword {
            Some(legacy_keyword) => match self.keyword_value(&legacy_keyword) {
                Ok(Some(legacy_value)) => {
                    Ok(to_unicode_locale_type(&legacy_keyword, &legacy_value))
                }
                Ok(None) => Ok(None),
                Err(e) => Err(e),
            },
            None => Ok(None),
        }
    }

    /// Returns the current label of this locale.
    pub fn label(&self) -> &str {
        &self.repr
    }

    /// Returns the current locale name as a C string.
    pub fn as_c_str(&self) -> ffi::CString {
        ffi::CString::new(self.repr.clone()).expect("ULoc contained interior NUL bytes")
    }

    /// Implements `uloc_forLanguageTag` from ICU4C.
    ///
    /// Note that an invalid tag will cause that tag and all others to be
    /// ignored.  For example `en-us` will work but `en_US` will not.
    pub fn for_language_tag(tag: &str) -> Result<ULoc, common::Error> {
        let tag = str_to_cstring(tag);
        let locale_id = buffered_string_method_with_retry(
            |buf, len, error| unsafe {
                versioned_function!(uloc_forLanguageTag)(
                    tag.as_ptr(),
                    buf,
                    len,
                    std::ptr::null_mut(),
                    error
                )
            },
            LOCALE_CAPACITY,
        )?;
        ULoc::try_from(&locale_id[..])
    }

    /// Call a `uloc` method that takes this locale's ID and returns a string.
    fn call_buffered_string_method(
        &self,
        uloc_method: unsafe extern "C" fn(
            *const raw::c_char,
            *mut raw::c_char,
            i32,
            *mut UErrorCode,
        ) -> i32,
    ) -> Result<String, common::Error> {
        let asciiz = self.as_c_str();
        buffered_string_method_with_retry(
            |buf, len, error| unsafe { uloc_method(asciiz.as_ptr(), buf, len, error) },
            LOCALE_CAPACITY,
        )
    }

    /// Call a `uloc` method that takes this locale's ID, panics on any errors, and returns
    /// `Some(result)` if the resulting string is non-empty, or `None` otherwise.
    fn call_buffered_string_method_to_option(
        &self,
        uloc_method: unsafe extern "C" fn(
            *const raw::c_char,
            *mut raw::c_char,
            i32,
            *mut UErrorCode,
        ) -> i32,
    ) -> Option<String> {
        let value: String = self.call_buffered_string_method(uloc_method).unwrap();
        if value.is_empty() {
            None
        } else {
            Some(value)
        }
    }

    /// Implements `uloc_getBaseName` from ICU4C.
    pub fn base_name(self) -> Self {
        let result = self
            .call_buffered_string_method(versioned_function!(uloc_getBaseName))
            .expect("should be able to produce a shorter locale");
        ULoc::try_from(&result[..]).expect("should be able to convert to locale")
    }
}

/// This implementation is based on ULocale.compareTo from ICU4J.
/// See 
/// <https://github.com/unicode-org/icu/blob/%6d%61%73%74%65%72/icu4j/main/classes/core/src/com/ibm/icu/util/ULocale.java>
impl Ord for ULoc {
    fn cmp(&self, other: &Self) -> Ordering {
        /// Compare corresponding keywords from two `ULoc`s. If the keywords match, compare the
        /// keyword values.
        fn compare_keywords(
            this: &ULoc,
            self_keyword: &Option<String>,
            other: &ULoc,
            other_keyword: &Option<String>,
        ) -> Option<Ordering> {
            match (self_keyword, other_keyword) {
                (Some(self_keyword), Some(other_keyword)) => {
                    // Compare the two keywords
                    match self_keyword.cmp(&other_keyword) {
                        Ordering::Equal => {
                            // Compare the two keyword values
                            let self_val = this.keyword_value(&self_keyword[..]).unwrap();
                            let other_val = other.keyword_value(&other_keyword[..]).unwrap();
                            Some(self_val.cmp(&other_val))
                        }
                        unequal_ordering => Some(unequal_ordering),
                    }
                }
                // `other` has run out of keywords
                (Some(_), _) => Some(Ordering::Greater),
                // `this` has run out of keywords
                (_, Some(_)) => Some(Ordering::Less),
                // Both iterators have run out
                (_, _) => None,
            }
        }

        self.language()
            .cmp(&other.language())
            .then_with(|| self.script().cmp(&other.script()))
            .then_with(|| self.country().cmp(&other.country()))
            .then_with(|| self.variant().cmp(&other.variant()))
            .then_with(|| {
                let mut self_keywords = self.keywords();
                let mut other_keywords = other.keywords();

                while let Some(keyword_ordering) =
                    compare_keywords(self, &self_keywords.next(), other, &other_keywords.next())
                {
                    match keyword_ordering {
                        Ordering::Equal => {}
                        unequal_ordering => {
                            return unequal_ordering;
                        }
                    }
                }

                // All keywords and values were identical (or there were none)
                Ordering::Equal
            })
    }
}

impl PartialOrd for ULoc {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

/// Gets the current system default locale.
///
/// Implements `uloc_getDefault` from ICU4C.
pub fn get_default() -> ULoc {
    let loc = unsafe { versioned_function!(uloc_getDefault)() };
    let uloc_cstr = unsafe { ffi::CStr::from_ptr(loc) };
    crate::ULoc::try_from(uloc_cstr).expect("could not convert default locale to ULoc")
}

/// Sets the current default system locale.
///
/// Implements `uloc_setDefault` from ICU4C.
pub fn set_default(loc: &ULoc) -> Result<(), common::Error> {
    let mut status = common::Error::OK_CODE;
    let asciiz = str_to_cstring(&loc.repr);
    unsafe { versioned_function!(uloc_setDefault)(asciiz.as_ptr(), &mut status) };
    common::Error::ok_or_warning(status)
}

/// Implements `uloc_acceptLanguage` from ICU4C.
pub fn accept_language(
    accept_list: impl IntoIterator<Item = impl Into<ULoc>>,
    available_locales: impl IntoIterator<Item = impl Into<ULoc>>,
) -> Result<(Option<ULoc>, UAcceptResult), common::Error> {
    let mut accept_result: UAcceptResult = UAcceptResult::ULOC_ACCEPT_FAILED;
    let mut accept_list_cstrings: Vec<ffi::CString> = vec![];
    // This is mutable only to satisfy the missing `const`s in the ICU4C API.
    let mut accept_list: Vec<*const raw::c_char> = accept_list
        .into_iter()
        .map(|item| {
            let uloc: ULoc = item.into();
            accept_list_cstrings.push(uloc.as_c_str());
            accept_list_cstrings
                .last()
                .expect("non-empty list")
                .as_ptr()
        })
        .collect();

    let available_locales: Vec<ULoc> = available_locales
        .into_iter()
        .map(|item| item.into())
        .collect();
    let available_locales: Vec<&str> = available_locales.iter().map(|uloc| uloc.label()).collect();
    let mut available_locales = Enumeration::try_from(&available_locales[..])?;

    let matched_locale = buffered_string_method_with_retry(
        |buf, len, error| unsafe {
            versioned_function!(uloc_acceptLanguage)(
                buf,
                len,
                &mut accept_result,
                accept_list.as_mut_ptr(),
                accept_list.len() as i32,
                available_locales.repr(),
                error,
            )
        },
        LOCALE_CAPACITY,
    );

    // Having no match is a valid if disappointing result.
    if accept_result == UAcceptResult::ULOC_ACCEPT_FAILED {
        return Ok((None, accept_result));
    }

    matched_locale
        .and_then(|s| ULoc::try_from(s.as_str()))
        .map(|uloc| (Some(uloc), accept_result))
}

/// Implements `uloc_toUnicodeLocaleKey` from ICU4C.
pub fn to_unicode_locale_key(legacy_keyword: &str) -> Option<String> {
    let legacy_keyword = str_to_cstring(legacy_keyword);
    let unicode_keyword: Option<ffi::CString> = unsafe {
        let ptr = versioned_function!(uloc_toUnicodeLocaleKey)(legacy_keyword.as_ptr());
        ptr.as_ref().map(|ptr| ffi::CStr::from_ptr(ptr).to_owned())
    };
    unicode_keyword.map(|cstring| cstring_to_string(&cstring))
}

/// Implements `uloc_toUnicodeLocaleType` from ICU4C.
pub fn to_unicode_locale_type(legacy_keyword: &str, legacy_value: &str) -> Option<String> {
    let legacy_keyword = str_to_cstring(legacy_keyword);
    let legacy_value = str_to_cstring(legacy_value);
    let unicode_value: Option<ffi::CString> = unsafe {
        let ptr = versioned_function!(uloc_toUnicodeLocaleType)(
            legacy_keyword.as_ptr(),
            legacy_value.as_ptr(),
        );
        ptr.as_ref().map(|ptr| ffi::CStr::from_ptr(ptr).to_owned())
    };
    unicode_value.map(|cstring| cstring_to_string(&cstring))
}

/// Implements `uloc_toLegacyKey` from ICU4C.
pub fn to_legacy_key(unicode_keyword: &str) -> Option<String> {
    let unicode_keyword = str_to_cstring(unicode_keyword);
    let legacy_keyword: Option<ffi::CString> = unsafe {
        let ptr = versioned_function!(uloc_toLegacyKey)(unicode_keyword.as_ptr());
        ptr.as_ref().map(|ptr| ffi::CStr::from_ptr(ptr).to_owned())
    };
    legacy_keyword.map(|cstring| cstring_to_string(&cstring))
}

/// Infallibly converts a Rust string to a `CString`. If there's an interior NUL, the string is
/// truncated up to that point.
fn str_to_cstring(input: &str) -> ffi::CString {
    ffi::CString::new(input)
        .unwrap_or_else(|e| ffi::CString::new(&input[0..e.nul_position()]).unwrap())
}

/// Infallibly converts a `CString` to a Rust `String`. We can safely assume that any strings
/// coming from ICU data are valid UTF-8.
fn cstring_to_string(input: &ffi::CString) -> String {
    input.to_string_lossy().to_string()
}

#[cfg(test)]
mod tests {
    use {super::*, anyhow::Error};

    #[test]
    fn test_language() -> Result<(), Error> {
        let loc = ULoc::try_from("es-CO")?;
        assert_eq!(loc.language(), Some("es".to_string()));
        Ok(())
    }

    #[test]
    fn test_language_absent() -> Result<(), Error> {
        let loc = ULoc::for_language_tag("und-CO")?;
        assert_eq!(loc.language(), None);
        Ok(())
    }

    // https://github.com/google/rust_icu/issues/244
    #[test]
    fn test_long_language_tag() -> Result<(), Error> {
        let mut language_tag: String = "und-CO".to_owned();
        let language_tag_rest = (0..500).map(|_| " ").collect::<String>();
        language_tag.push_str(&language_tag_rest);
        let loc = ULoc::for_language_tag(&language_tag)?;
        Ok(())
    }

    #[test]
    fn test_script() -> Result<(), Error> {
        let loc = ULoc::try_from("sr-Cyrl")?;
        assert_eq!(loc.script(), Some("Cyrl".to_string()));
        Ok(())
    }

    #[test]
    fn test_script_absent() -> Result<(), Error> {
        let loc = ULoc::try_from("sr")?;
        assert_eq!(loc.script(), None);
        Ok(())
    }

    #[test]
    fn test_country() -> Result<(), Error> {
        let loc = ULoc::try_from("es-CO")?;
        assert_eq!(loc.country(), Some("CO".to_string()));
        Ok(())
    }

    #[test]
    fn test_country_absent() -> Result<(), Error> {
        let loc = ULoc::try_from("es")?;
        assert_eq!(loc.country(), None);
        Ok(())
    }

    // This test yields a different result in ICU versions prior to 64:
    // "zh-Latn@collation=pinyin".
    #[cfg(features = "icu_version_64_plus")]
    #[test]
    fn test_variant() -> Result<(), Error> {
        let loc = ULoc::try_from("zh-Latn-pinyin")?;
        assert_eq!(
            loc.variant(),
            Some("PINYIN".to_string()),
            "locale was: {:?}",
            loc
        );
        Ok(())
    }

    #[test]
    fn test_variant_absent() -> Result<(), Error> {
        let loc = ULoc::try_from("zh-Latn")?;
        assert_eq!(loc.variant(), None);
        Ok(())
    }

    #[test]
    fn test_default_locale() {
        let loc = ULoc::try_from("fr-fr").expect("get fr_FR locale");
        set_default(&loc).expect("successful set of locale");
        assert_eq!(get_default().label(), loc.label());
        assert_eq!(loc.label(), "fr_FR", "The locale should get canonicalized");
        let loc = ULoc::try_from("en-us").expect("get en_US locale");
        set_default(&loc).expect("successful set of locale");
        assert_eq!(get_default().label(), loc.label());
    }

    #[test]
    fn test_add_likely_subtags() {
        let loc = ULoc::try_from("en-US").expect("get en_US locale");
        let with_likely_subtags = loc.add_likely_subtags().expect("should add likely subtags");
        let expected = ULoc::try_from("en_Latn_US").expect("get en_Latn_US locale");
        assert_eq!(with_likely_subtags.label(), expected.label());
    }

    #[test]
    fn test_minimize_subtags() {
        let loc = ULoc::try_from("sr_Cyrl_RS").expect("get sr_Cyrl_RS locale");
        let minimized_subtags = loc.minimize_subtags().expect("should minimize subtags");
        let expected = ULoc::try_from("sr").expect("get sr locale");
        assert_eq!(minimized_subtags.label(), expected.label());
    }

    #[test]
    fn test_to_language_tag() {
        let loc = ULoc::try_from("sr_Cyrl_RS").expect("get sr_Cyrl_RS locale");
        let language_tag = loc
            .to_language_tag(true)
            .expect("should convert to language tag");
        assert_eq!(language_tag, "sr-Cyrl-RS".to_string());
    }

    #[test]
    fn test_keywords() -> Result<(), Error> {
        let loc = ULoc::for_language_tag("az-Cyrl-AZ-u-ca-hebrew-fw-sunday-nu-deva-tz-usnyc")?;
        let keywords: Vec<String> = loc.keywords().collect();
        assert_eq!(
            keywords,
            vec![
                "calendar".to_string(),
                "fw".to_string(),
                "numbers".to_string(),
                "timezone".to_string()
            ]
        );
        Ok(())
    }

    #[test]
    fn test_keywords_nounicode() -> Result<(), Error> {
        let loc = ULoc::for_language_tag("az-Cyrl-AZ-u-ca-hebrew-t-it-x-whatever")?;
        let keywords: Vec<String> = loc.keywords().collect();
        assert_eq!(
            keywords,
            vec!["calendar".to_string(), "t".to_string(), "x".to_string(),]
        );
        assert_eq!(loc.keyword_value("t")?.unwrap(), "it");
        assert_eq!(loc.keyword_value("x")?.unwrap(), "whatever");
        Ok(())
    }

    #[test]
    fn test_keywords_empty() -> Result<(), Error> {
        let loc = ULoc::for_language_tag("az-Cyrl-AZ")?;
        let keywords: Vec<String> = loc.keywords().collect();
        assert!(keywords.is_empty());
        Ok(())
    }

    #[test]
    fn test_unicode_keywords() -> Result<(), Error> {
        let loc = ULoc::for_language_tag("az-Cyrl-AZ-u-ca-hebrew-fw-sunday-nu-deva-tz-usnyc")?;
        let keywords: Vec<String> = loc.unicode_keywords().collect();
        assert_eq!(
            keywords,
            vec![
                "ca".to_string(),
                "fw".to_string(),
                "nu".to_string(),
                "tz".to_string()
            ]
        );
        Ok(())
    }

    #[test]
    fn test_unicode_keywords_empty() -> Result<(), Error> {
        let loc = ULoc::for_language_tag("az-Cyrl-AZ")?;
        let keywords: Vec<String> = loc.unicode_keywords().collect();
        assert!(keywords.is_empty());
        Ok(())
    }

    #[test]
    fn test_keyword_value() -> Result<(), Error> {
        let loc = ULoc::for_language_tag("az-Cyrl-AZ-u-ca-hebrew-fw-sunday-nu-deva-tz-usnyc")?;
        assert_eq!(loc.keyword_value("calendar")?, Some("hebrew".to_string()));
        assert_eq!(loc.keyword_value("collation")?, None);
        Ok(())
    }

    #[test]
    fn test_unicode_keyword_value() -> Result<(), Error> {
        let loc = ULoc::for_language_tag("az-Cyrl-AZ-u-ca-hebrew-fw-sunday-nu-deva-tz-usnyc")?;
        assert_eq!(loc.unicode_keyword_value("ca")?, Some("hebrew".to_string()));
        assert_eq!(loc.unicode_keyword_value("fw")?, Some("sunday".to_string()));
        assert_eq!(loc.unicode_keyword_value("co")?, None);
        Ok(())
    }

    #[test]
    fn test_order() -> Result<(), Error> {
        assert!(ULoc::for_language_tag("az")? < ULoc::for_language_tag("az-Cyrl")?);
        assert!(ULoc::for_language_tag("az-Cyrl")? < ULoc::for_language_tag("az-Cyrl-AZ")?);
        assert!(
            ULoc::for_language_tag("az-Cyrl-AZ")? < ULoc::for_language_tag("az-Cyrl-AZ-variant")?
        );
        assert!(
            ULoc::for_language_tag("az-Cyrl-AZ-variant")?
                < ULoc::for_language_tag("az-Cyrl-AZ-variant-u-nu-arab")?
        );
        assert!(
            ULoc::for_language_tag("az-u-ca-gregory")? < ULoc::for_language_tag("az-u-fw-fri")?
        );
        assert!(
            ULoc::for_language_tag("az-u-ca-buddhist")?
                < ULoc::for_language_tag("az-u-ca-chinese")?
        );
        assert!(ULoc::for_language_tag("az-u-fw-mon")? < ULoc::for_language_tag("az-u-fw-tue")?);
        assert!(
            ULoc::for_language_tag("az-u-fw-mon")? < ULoc::for_language_tag("az-u-fw-mon-nu-arab")?
        );
        assert!(
            ULoc::for_language_tag("az-u-fw-mon-nu-arab")? > ULoc::for_language_tag("az-u-fw-mon")?
        );

        let loc = ULoc::for_language_tag("az-Cyrl-AZ-variant-u-nu-arab")?;
        assert_eq!(loc.cmp(&loc), Ordering::Equal,);
        Ok(())
    }

    #[test]
    fn test_accept_language_fallback() {
        let accept_list: Result<Vec<_>, _> = vec!["es_MX", "ar_EG", "fr_FR"]
            .into_iter()
            .map(ULoc::try_from)
            .collect();
        let accept_list = accept_list.expect("make accept_list");

        let available_locales: Result<Vec<_>, _> =
            vec!["de_DE", "en_US", "es", "nl_NL", "sr_RS_Cyrl"]
                .into_iter()
                .map(ULoc::try_from)
                .collect();
        let available_locales = available_locales.expect("make available_locales");

        let actual = accept_language(accept_list, available_locales).expect("call accept_language");
        assert_eq!(
            actual,
            (
                ULoc::try_from("es").ok(),
                UAcceptResult::ULOC_ACCEPT_FALLBACK
            )
        );
    }

    // This tests verifies buggy behavior which is fixed since ICU version 67.1
    #[cfg(not(feature = "icu_version_67_plus"))]
    #[test]
    fn test_accept_language_exact_match() {
        let accept_list: Result<Vec<_>, _> = vec!["es_ES", "ar_EG", "fr_FR"]
            .into_iter()
            .map(ULoc::try_from)
            .collect();
        let accept_list = accept_list.expect("make accept_list");

        let available_locales: Result<Vec<_>, _> = vec!["de_DE", "en_US", "es_MX", "ar_EG"]
            .into_iter()
            .map(ULoc::try_from)
            .collect();
        let available_locales = available_locales.expect("make available_locales");

        let actual = accept_language(accept_list, available_locales).expect("call accept_language");
        assert_eq!(
            actual,
            (
                // "es_MX" should be preferred as a fallback over exact match "ar_EG".
                ULoc::try_from("ar_EG").ok(),
                UAcceptResult::ULOC_ACCEPT_VALID
            )
        );
    }

    #[cfg(feature = "icu_version_67_plus")]
    #[test]
    fn test_accept_language_exact_match() {
        let accept_list: Result<Vec<_>, _> = vec!["es_ES", "ar_EG", "fr_FR"]
            .into_iter()
            .map(ULoc::try_from)
            .collect();
        let accept_list = accept_list.expect("make accept_list");

        let available_locales: Result<Vec<_>, _> = vec!["de_DE", "en_US", "es_MX", "ar_EG"]
            .into_iter()
            .map(ULoc::try_from)
            .collect();
        let available_locales = available_locales.expect("make available_locales");

        let actual = accept_language(accept_list, available_locales).expect("call accept_language");
        assert_eq!(
            actual,
            (
                ULoc::try_from("es_MX").ok(),
                UAcceptResult::ULOC_ACCEPT_FALLBACK,
            )
        );
    }

    #[test]
    fn test_accept_language_no_match() {
        let accept_list: Result<Vec<_>, _> = vec!["es_ES", "ar_EG", "fr_FR"]
            .into_iter()
            .map(ULoc::try_from)
            .collect();
        let accept_list = accept_list.expect("make accept_list");

        let available_locales: Result<Vec<_>, _> =
            vec!["el_GR"].into_iter().map(ULoc::try_from).collect();
        let available_locales = available_locales.expect("make available_locales");

        let actual = accept_language(accept_list, available_locales).expect("call accept_language");
        assert_eq!(actual, (None, UAcceptResult::ULOC_ACCEPT_FAILED))
    }

    #[test]
    fn test_to_unicode_locale_key() -> Result<(), Error> {
        let actual = to_unicode_locale_key("calendar");
        assert_eq!(actual, Some("ca".to_string()));
        Ok(())
    }

    #[test]
    fn test_to_unicode_locale_type() -> Result<(), Error> {
        let actual = to_unicode_locale_type("co", "phonebook");
        assert_eq!(actual, Some("phonebk".to_string()));
        Ok(())
    }

    #[test]
    fn test_to_legacy_key() -> Result<(), Error> {
        let actual = to_legacy_key("ca");
        assert_eq!(actual, Some("calendar".to_string()));
        Ok(())
    }

    #[test]
    fn test_str_to_cstring() -> Result<(), Error> {
        assert_eq!(str_to_cstring("abc"), ffi::CString::new("abc")?);
        assert_eq!(str_to_cstring("abc\0def"), ffi::CString::new("abc")?);

        Ok(())
    }

    #[test]
    fn test_base_name() -> Result<(), Error> {
        assert_eq!(
            ULoc::try_from("en-u-tz-uslax-x-foo")?.base_name(),
            ULoc::try_from("en")?
        );
        Ok(())
    }

    #[test]
    fn test_uloc_mut() -> Result<(), Error> {
        let loc = ULoc::for_language_tag("en-t-it-u-tz-uslax-x-foo")?;
        let loc_mut = ULocMut::from(loc);
        let loc = ULoc::from(loc_mut);
        assert_eq!(ULoc::for_language_tag("en-t-it-u-tz-uslax-x-foo")?, loc);
        Ok(())
    }

    #[test]
    fn test_uloc_mut_changes() -> Result<(), Error> {
        let loc = ULoc::for_language_tag("en-t-it-u-tz-uslax-x-foo")?;
        let mut loc_mut = ULocMut::from(loc);
        loc_mut.remove_unicode_keyvalue("tz");
        let loc = ULoc::from(loc_mut);
        assert_eq!(ULoc::for_language_tag("en-t-it-x-foo")?, loc);

        let loc = ULoc::for_language_tag("en-u-tz-uslax")?;
        let mut loc_mut = ULocMut::from(loc);
        loc_mut.remove_unicode_keyvalue("tz");
        let loc = ULoc::from(loc_mut);
        assert_eq!(ULoc::for_language_tag("en")?, loc);
        Ok(())
    }

    #[test]
    fn test_uloc_mut_overrides() -> Result<(), Error> {
        let loc = ULoc::for_language_tag("en-t-it-u-tz-uslax-x-foo")?;
        let mut loc_mut = ULocMut::from(loc);
        loc_mut.set_unicode_keyvalue("tz", "usnyc");
        let loc = ULoc::from(loc_mut);
        assert_eq!(ULoc::for_language_tag("en-t-it-u-tz-usnyc-x-foo")?, loc);

        let loc = ULoc::for_language_tag("en-t-it-u-tz-uslax-x-foo")?;
        let mut loc_mut = ULocMut::from(loc);
        loc_mut.set_unicode_keyvalue("tz", "usnyc");
        loc_mut.set_unicode_keyvalue("nu", "arabic");
        let loc = ULoc::from(loc_mut);
        assert_eq!(ULoc::for_language_tag("en-t-it-u-nu-arabic-tz-usnyc-x-foo")?, loc);
        assert_eq!(ULoc::for_language_tag("en-t-it-u-tz-usnyc-nu-arabic-x-foo")?, loc);
        Ok(())
    }

    #[test]
    fn test_uloc_mut_add_unicode_extension() -> Result<(), Error> {
        let loc = ULoc::for_language_tag("en-t-it-x-foo")?;
        let mut loc_mut = ULocMut::from(loc);
        loc_mut.set_unicode_keyvalue("tz", "usnyc");
        let loc = ULoc::from(loc_mut);
        assert_eq!(ULoc::for_language_tag("en-t-it-u-tz-usnyc-x-foo")?, loc);
        Ok(())
    }

    #[test]
    fn test_round_trip_from_uloc_plain() -> Result<(), Error> {
        let loc = ULoc::for_language_tag("sr")?;
        let loc = ULocMut::from(loc);
        let loc = ULoc::from(loc);
        assert_eq!(ULoc::try_from("sr")?, loc);
        Ok(())
    }

    #[test]
    fn test_round_trip_from_uloc_with_country() -> Result<(), Error> {
        let loc = ULoc::for_language_tag("sr-rs")?;
        let loc = ULoc::from(ULocMut::from(loc));
        assert_eq!(ULoc::try_from("sr-rs")?, loc);
        Ok(())
    }

    #[test]
    fn test_equivalence() {
        let loc = ULoc::try_from("sr@timezone=America/Los_Angeles").unwrap();
        assert_eq!(ULoc::for_language_tag("sr-u-tz-uslax").unwrap(), loc);
    }
}
