// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Result},
    fuchsia_syslog::fx_log_err,
    intl_model as model, libc, rust_icu_common as ucommon, rust_icu_sys as usys,
    rust_icu_uloc as uloc,
    std::collections::BTreeMap,
    std::convert::From,
    std::convert::TryFrom,
    std::ffi,
    std::fs,
    std::io,
    std::mem,
    std::str,
};

/// The directory where localized resources are kept.
pub(crate) const ASSETS_DIR: &str = "/pkg/data/assets/locales";

#[repr(i8)]
#[derive(Debug, PartialEq)]
pub enum LookupStatus {
    // No error.
    OK = 0,
    /// The value requested is not available.
    Unavailable = 1,
    /// The argument passed in by the user is not valid.
    ArgumentError = 2,
    /// Some internal error happened. Consult logs for details.
    Internal = 111,
}

/// The C API supported by the Lookup module.
///
/// The C API is used for FFI interfacing with other languages that support
/// C ABI FFI.
trait CApi {
    /// Looks a message up by its unique `message_id`.  A nonzero status is
    /// returned if the message is not found.
    fn string(&self, message_id: u64) -> Result<&ffi::CStr, LookupStatus>;
}

impl From<str::Utf8Error> for LookupStatus {
    fn from(e: str::Utf8Error) -> Self {
        fx_log_err!("intl: utf-8: {:?}", e);
        LookupStatus::Unavailable
    }
}

impl From<anyhow::Error> for LookupStatus {
    fn from(e: anyhow::Error) -> Self {
        fx_log_err!("intl: general: {:?}", e);
        LookupStatus::Internal
    }
}

impl From<ucommon::Error> for LookupStatus {
    fn from(e: ucommon::Error) -> Self {
        fx_log_err!("intl: icu: {:?}", e);
        LookupStatus::Internal
    }
}

/// Instantiates a fake Lookup instance, which is useful for tests that don't
/// want to make a full end-to-end localization setup.  
///
/// The fake is simplistic and it is the intention that it provides you with
/// some default fake behaviors.  The behaviors are as follows at the moment,
/// and more could be added if needed.
///
/// - If `locale_ids` contains the string `en-US`, the constructor function
///   in the FFI layer will return [LookupStatus::Unavailable].
/// - If the message ID pased to `Lookup::String()` is exactly 1, the fake
///   returns `Hello {person}!`, so that you can test 1-parameter formatting.
/// - Otherwise, for an even mesage ID it returns "Hello world!", or for
///   an odd message ID returns [LookupStatus::Unavailable].
///
/// The implementation of the fake itself is done in rust behind a FFI ABI,
/// see the package //src/lib/intl/lookup/rust for details.
pub struct FakeLookup {
    hello: ffi::CString,
    hello_person: ffi::CString,
}

impl FakeLookup {
    /// Create a new `FakeLookup`.
    pub fn new() -> FakeLookup {
        let hello =
            ffi::CString::new("Hello world!").expect("CString from known value should never fail");
        let hello_person = ffi::CString::new("Hello {person}!")
            .expect("CString from known value should never fail");
        FakeLookup { hello, hello_person }
    }
}

impl CApi for FakeLookup {
    /// A fake implementation of `string` for testing.
    ///
    /// Returns "Hello world" if passed an even `message_id`, and `LookupStatus::UNAVAILABLE` when
    /// passed an odd message_id. Used to test the FFI.
    fn string(&self, message_id: u64) -> Result<&ffi::CStr, LookupStatus> {
        if message_id == 1 {
            return Ok(self.hello_person.as_c_str());
        }
        match message_id % 2 == 0 {
            true => Ok(self.hello.as_c_str()),
            false => Err(LookupStatus::Unavailable),
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn intl_lookup_new_fake_for_test(
    len: libc::size_t,
    array: *mut *const libc::c_char,
    status: *mut i8,
) -> *const FakeLookup {
    *status = LookupStatus::OK as i8;
    let rsize = len as usize;
    let input: Vec<*const libc::c_char> = Vec::from_raw_parts(array, rsize, rsize);
    // Do not drop the vector we don't own.
    let input = mem::ManuallyDrop::new(input);

    for raw in input.iter() {
        let cstr = ffi::CStr::from_ptr(*raw).to_str().expect("not a valid UTF-8");
        if cstr == "en-US" {
            *status = LookupStatus::Unavailable as i8;
            return std::ptr::null::<FakeLookup>();
        }
    }
    Box::into_raw(Box::new(FakeLookup::new()))
}
#[no_mangle]
pub unsafe extern "C" fn intl_lookup_delete_fake_for_test(this: *mut FakeLookup) {
    generic_delete(this);
}

#[no_mangle]
pub unsafe extern "C" fn intl_lookup_new(
    len: libc::size_t,
    array: *mut *const libc::c_char,
    status: *mut i8,
) -> *const Lookup {
    *status = LookupStatus::OK as i8;
    let rsize = len as usize;
    let input: Vec<*const libc::c_char> = Vec::from_raw_parts(array, rsize, rsize);
    // Do not drop the vector we don't own.
    let input = mem::ManuallyDrop::new(input);

    let mut locales = vec![];
    for raw in input.iter() {
        let cstr = ffi::CStr::from_ptr(*raw).to_str();
        match cstr {
            Err(e) => {
                fx_log_err!("intl::intl_lookup_new::c_str: {:?}", &e);
                let ls: LookupStatus = e.into();
                *status = ls as i8;
                return std::ptr::null::<Lookup>();
            }
            Ok(s) => {
                locales.push(s);
            }
        }
    }
    let data = icu_data::Loader::new().expect("icu data loaded");
    let lookup_or = Lookup::new(data, &locales[..]);
    match lookup_or {
        Ok(lookup) => Box::into_raw(Box::new(lookup)),
        Err(e) => {
            fx_log_err!("intl::intl_lookup_new: {:?}", &e);
            let ls: LookupStatus = e.into();
            *status = ls as i8;
            std::ptr::null::<Lookup>()
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn intl_lookup_delete(instance: *mut Lookup) {
    generic_delete(instance);
}

#[no_mangle]
pub unsafe extern "C" fn intl_lookup_string_fake_for_test(
    this: *const FakeLookup,
    id: u64,
    status: *mut i8,
) -> *const libc::c_char {
    generic_string(this, id, status)
}

unsafe fn generic_string<T: CApi>(this: *const T, id: u64, status: *mut i8) -> *const libc::c_char {
    *status = LookupStatus::OK as i8;
    match this.as_ref().unwrap().string(id) {
        Err(e) => {
            *status = e as i8;
            std::ptr::null()
        }
        Ok(s) => s.as_ptr() as *const libc::c_char,
    }
}

unsafe fn generic_delete<T>(instance: *mut T) {
    let _ = Box::from_raw(instance);
}

#[no_mangle]
pub unsafe extern "C" fn intl_lookup_string(
    this: *const Lookup,
    id: u64,
    status: *mut i8,
) -> *const libc::c_char {
    *status = LookupStatus::OK as i8;
    match this.as_ref().unwrap().string(id) {
        Err(e) => {
            *status = e as i8;
            std::ptr::null()
        }
        Ok(s) => s.as_ptr() as *const libc::c_char,
    }
}

// Contains the message catalog ready for external consumption.  Specifically,
// provides C-style messages
pub struct Catalog {
    locale_to_message: BTreeMap<String, BTreeMap<u64, ffi::CString>>,
}

impl Catalog {
    fn new() -> Catalog {
        let locale_to_message = BTreeMap::new();
        Catalog { locale_to_message }
    }

    fn add(&mut self, model: model::Model) -> Result<()> {
        let locale_id = model.locale();
        let mut messages: BTreeMap<u64, ffi::CString> = BTreeMap::new();
        for (id, msg) in model.messages() {
            let c_msg = ffi::CString::new(msg.clone())
                .with_context(|| format!("interior NUL in  {:?}", msg))?;
            messages.insert(*id, c_msg);
        }
        self.locale_to_message.insert(locale_id.to_string(), messages);
        Ok(())
    }

    fn get(&self, locale: &str, id: u64) -> Option<&ffi::CStr> {
        self.locale_to_message
            .get(locale)
            .map(|messages| messages.get(&id))
            .flatten()
            .map(|cstring| cstring.as_c_str())
    }
}

/// Implements localized string lookup.
///
/// Requires that the ICU data loader is configured and that the ICU data are registered in the
/// program's package.  See the [rust
/// documentation](https://fuchsia.dev/fuchsia-src/development/internationalization/icu_data#rust_example)
/// for detailed instructions.
///
/// ```ignore
/// use intl_lookup::Lookup;
/// let icu_data = icu_data::Loader::new().expect("icu data loaded");
/// let l = Lookup::new(icu_data, &vec!["es"])?;
/// assert_eq!("el stringo", l.str(ftest::MessageIds::StringName as u64)?);
/// ```
pub struct Lookup {
    requested: Vec<uloc::ULoc>,
    catalog: Catalog,
    // The loader is required to ensure that the unicode locale data is kept
    // in memory while this Lookup is in use.
    #[allow(dead_code)]
    icu_data: icu_data::Loader,
}

impl Lookup {
    /// Creates a new instance of Lookup, with the default ways to look up the
    /// data.
    pub fn new(icu_data: icu_data::Loader, requested: &[&str]) -> Result<Lookup> {
        let supported_locales =
            Lookup::get_available_locales().with_context(|| "while creating Lookup")?;
        // Load all available locale maps.
        let catalog = Lookup::load_locales(&supported_locales[..])
            .with_context(|| "while loading locales")?;
        Lookup::new_internal(icu_data, requested, &supported_locales, catalog)
    }

    // Loads all supported locales from disk.
    fn load_locales(supported: &[impl AsRef<str>]) -> Result<Catalog> {
        let mut catalog = Catalog::new();

        // In the future we may decide to load only the locales we actually need.
        for locale in supported {
            // Directory names look like: ".../assets/locales/en-US".
            let mut locale_dir_path = std::path::PathBuf::from(ASSETS_DIR);
            locale_dir_path.push(locale.as_ref());

            let locale_dir = std::fs::read_dir(&locale_dir_path)
                .with_context(|| format!("while reading {:?}", &locale_dir_path))?;
            for entry in locale_dir {
                let path = entry?.path();
                let file = fs::File::open(&path)
                    .with_context(|| format!("while trying to open {:?}", &path))?;
                let file = io::BufReader::new(file);
                let model = model::Model::from_json_reader(file)
                    .with_context(|| format!("while reading {:?}", &path))?;
                catalog.add(model)?;
            }
        }
        Ok(catalog)
    }

    /// Create a new [Lookup] from parts.  Only to be used in tests.
    #[cfg(test)]
    pub fn new_from_parts(
        icu_data: icu_data::Loader,
        requested: &[&str],
        supported: &Vec<String>,
        catalog: Catalog,
    ) -> Result<Lookup> {
        Lookup::new_internal(icu_data, requested, supported, catalog)
    }

    fn new_internal(
        icu_data: icu_data::Loader,
        requested: &[&str],
        supported: &Vec<String>,
        catalog: Catalog,
    ) -> Result<Lookup> {
        let mut supported_locales = supported
            .iter()
            .map(|s: &String| uloc::ULoc::try_from(s.as_str()))
            .collect::<Result<Vec<_>, _>>()
            .with_context(|| "while determining supported locales")?;

        // Work around a locale fallback resolution bug
        // https://unicode-org.atlassian.net/browse/ICU-20931 which has been fixed in ICU 67.  This
        // has to stay in place until Fuchsia starts using ICU version 67.
        supported_locales.push(uloc::ULoc::try_from("und-und")?);
        let supported_locales = supported_locales;

        // Compute a fallback for each requested locale, and fail if none is available.
        let mut requested_locales = vec![];
        for locale in requested.iter() {
            let (maybe_accepted_locale, accept_result) = uloc::accept_language(
                vec![uloc::ULoc::try_from(*locale)
                    .with_context(|| format!("could not parse as locale: {:}", &locale))?],
                supported_locales.clone(),
            )?;
            match accept_result {
                usys::UAcceptResult::ULOC_ACCEPT_FAILED => {
                    // This may happen if the locale is not at all part of the
                    // set of supported locales.
                }
                _ => match maybe_accepted_locale {
                    None => {
                        return Err(anyhow::anyhow!(
                            "no matching locale found for: requested: {:?}, supported: {:?}",
                            &locale,
                            &supported_locales
                        ));
                    }
                    Some(loc) => {
                        requested_locales.push(loc);
                    }
                },
            }
        }
        // We failed to find locales to request from the list of supported locales.
        if requested_locales.is_empty() {
            return Err(anyhow::anyhow!(
                "no matching locale found for: requested: {:?}, supported: {:?}",
                &requested,
                &supported_locales
            ));
        }
        Ok(Lookup { requested: requested_locales, catalog, icu_data })
    }

    #[cfg(test)]
    fn get_available_locales_for_test() -> Result<Vec<String>> {
        Lookup::get_available_locales()
    }

    // Returns the list of locales for which there are resources present in
    // the locale assets directory.  Errors are returned if the locale assets
    // directory is malformed: since it is prepared at compile time, such an
    // occurrence means that the program is corrupted.
    fn get_available_locales() -> Result<Vec<String>> {
        let locale_dirs = std::fs::read_dir(ASSETS_DIR)
            .with_context(|| format!("while reading {}", ASSETS_DIR))?;
        let mut available_locales: Vec<String> = vec![];
        for entry_or in locale_dirs {
            let entry =
                entry_or.with_context(|| format!("while reading entries in {}", ASSETS_DIR))?;
            // We only ever expect directories corresponding to locale names
            // to be UTF-8 encoded, so this conversion will normally always
            // succeed for directories in `ASSETS_DIR`.
            let name = entry.file_name().into_string().map_err(|os_string| {
                anyhow::anyhow!("OS path not convertible to UTF-8: {:?}", os_string)
            })?;
            let entry_type = entry
                .file_type()
                .with_context(|| format!("while looking up file type for: {:?}", name))?;
            if entry_type.is_dir() {
                available_locales.push(name);
            }
        }
        Ok(available_locales)
    }

    /// Looks up the message by its key, a rust API version of [API::string].
    pub fn str(&self, id: u64) -> Result<&str, LookupStatus> {
        Ok(self
            .string(id)?
            .to_str()
            .with_context(|| format!("str(): while looking up id: {}", &id))?)
    }
}

impl CApi for Lookup {
    /// See the documentation for `API` for details.
    fn string(&self, id: u64) -> Result<&ffi::CStr, LookupStatus> {
        for locale in self.requested.iter() {
            if let Some(s) = self.catalog.get(&locale.to_language_tag(false)?, id) {
                return Ok(s);
            }
        }
        Err(LookupStatus::Unavailable)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl_fuchsia_intl_test as ftest;
    use std::collections::HashSet;

    #[test]
    fn lookup_en() -> Result<(), LookupStatus> {
        let icu_data = icu_data::Loader::new().expect("icu data loaded");
        let l = Lookup::new(icu_data, &vec!["en"])?;
        assert_eq!("text_string", l.string(ftest::MessageIds::StringName as u64)?.to_str()?);
        assert_eq!("text_string_2", l.string(ftest::MessageIds::StringName2 as u64)?.to_str()?);
        Ok(())
    }

    #[test]
    fn lookup_fr() -> Result<(), LookupStatus> {
        let icu_data = icu_data::Loader::new().expect("icu data loaded");
        let l = Lookup::new(icu_data, &vec!["fr"])?;
        assert_eq!("le string", l.string(ftest::MessageIds::StringName as u64)?.to_str()?);
        assert_eq!("le string 2", l.string(ftest::MessageIds::StringName2 as u64)?.to_str()?);
        Ok(())
    }

    #[test]
    fn lookup_es() -> Result<(), LookupStatus> {
        let icu_data = icu_data::Loader::new().expect("icu data loaded");
        let l = Lookup::new(icu_data, &vec!["es"])?;
        assert_eq!("el stringo", l.string(ftest::MessageIds::StringName as u64)?.to_str()?);
        assert_eq!("el stringo 2", l.string(ftest::MessageIds::StringName2 as u64)?.to_str()?);
        Ok(())
    }

    // When "es" is preferred, use it.
    #[test]
    fn lookup_es_en() -> Result<(), LookupStatus> {
        let icu_data = icu_data::Loader::new().expect("icu data loaded");
        let l = Lookup::new(icu_data, &vec!["es", "en"])?;
        assert_eq!("el stringo", l.string(ftest::MessageIds::StringName as u64)?.to_str()?);
        assert_eq!("el stringo 2", l.string(ftest::MessageIds::StringName2 as u64)?.to_str()?);
        Ok(())
    }

    #[test]
    fn lookup_es_419_fallback() -> Result<(), LookupStatus> {
        let icu_data = icu_data::Loader::new().expect("icu data loaded");
        let l = Lookup::new(icu_data, &vec!["es-419-u-ca-gregorian"]).expect("locale exists");
        assert_eq!("el stringo", l.string(ftest::MessageIds::StringName as u64)?.to_str()?);
        assert_eq!("el stringo 2", l.string(ftest::MessageIds::StringName2 as u64)?.to_str()?);
        Ok(())
    }

    #[test]
    fn nonexistent_locale_rejected() -> Result<()> {
        let icu_data = icu_data::Loader::new().expect("icu data loaded");
        match Lookup::new(icu_data, &vec!["nonexistent-locale"]) {
            Ok(_) => Err(anyhow::anyhow!("unexpectedly accepted nonexistent locale")),
            Err(_) => Ok(()),
        }
    }

    #[test]
    fn locale_fallback_accounted_for() -> Result<()> {
        let icu_data = icu_data::Loader::new().expect("icu data loaded");
        // These locales are directly supported by the tests.
        Lookup::new(icu_data.clone(), &vec!["en"])?;
        Lookup::new(icu_data.clone(), &vec!["fr"])?;
        Lookup::new(icu_data.clone(), &vec!["es"])?;

        // The following languages are not directly supported.  Instead they
        // are supported via the locale fallback mechanism.

        // Falls back to "en".
        Lookup::new(icu_data.clone(), &vec!["en-US"])?;
        // Falls back to "es".
        Lookup::new(icu_data.clone(), &vec!["es-ES"])?;
        // Falls back to "es", too.
        Lookup::new(icu_data.clone(), &vec!["es-419"])?;
        Ok(())
    }

    // Exercises the fake behaviors which are part of the fake spec.  The fake
    // behaviors may evolve in the future, but this test gives out the ones that
    // currently exist.
    #[test]
    fn test_fake_lookup() -> Result<(), LookupStatus> {
        let l = FakeLookup::new();
        assert_eq!("Hello {person}!", l.string(1)?.to_str()?);
        // Fake lookups always return "Hello world!", that's a FakeLookup
        // feature.
        assert_eq!("Hello world!", l.string(10)?.to_str()?);
        assert_eq!("Hello world!", l.string(12)?.to_str()?);
        assert_eq!(LookupStatus::Unavailable, l.string(11).unwrap_err());
        assert_eq!(LookupStatus::Unavailable, l.string(41).unwrap_err());
        Ok(())
    }

    #[test]
    fn test_real_lookup() -> Result<(), LookupStatus> {
        let icu_data = icu_data::Loader::new().expect("icu data loaded");
        let l = Lookup::new(icu_data, &vec!["es"])?;
        assert_eq!("el stringo", l.str(ftest::MessageIds::StringName as u64)?);
        Ok(())
    }

    /// Locales have been made part of the resources of the test package.
    #[test]
    fn test_available_locales() -> Result<()> {
        // Iteration order is not deterministic.
        let expected: HashSet<String> = ["es", "en", "fr"].iter().map(|s| s.to_string()).collect();
        assert_eq!(expected, Lookup::get_available_locales_for_test()?.into_iter().collect());
        Ok(())
    }

    /// If an unsupported locale has been requested, ignore it in the list and
    /// fall back to something else.
    #[test]
    fn ignore_unavailable_locales() {
        let icu_data = icu_data::Loader::new().expect("icu data loaded");
        let l = Lookup::new(icu_data, &vec!["sr", "es"]).expect("Lookup::new success");
        assert_eq!(
            "el stringo",
            l.str(ftest::MessageIds::StringName as u64).expect("Lookup::str success")
        );
    }

    /// If there is nothing to fall back to, report an error.
    #[test]
    fn report_unavailable_locales_without_alternative() {
        let icu_data = icu_data::Loader::new().expect("icu data loaded");
        let l = Lookup::new(icu_data, &vec!["sr"]);
        assert!(l.is_err());
    }
}
