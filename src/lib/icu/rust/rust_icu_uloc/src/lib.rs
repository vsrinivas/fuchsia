use {
    rust_icu_common as common, rust_icu_sys::versioned_function, rust_icu_sys::*,
    std::convert::From, std::convert::TryFrom, std::ffi, std::os::raw,
};

/// A representation of a Unicode locale.
///
/// For the time being, only basic conversion and methods are in fact implemented.
#[derive(Debug)]
pub struct ULoc {
    // A locale's representation in C is really just a string.
    repr: String,
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
        let repr = s.to_str().map_err(|_| common::Error::string_with_interior_nul())?;
        ULoc { repr: String::from(repr) }.canonicalize()
    }
}

impl ULoc {
    /// Implements `uloc_canonicalize` from ICU4C.
    pub fn canonicalize(&self) -> Result<ULoc, common::Error> {
        let mut status = common::Error::OK_CODE;
        let repr = ffi::CString::new(self.repr.clone())
            .map_err(|_| common::Error::string_with_interior_nul())?;
        const CAP: usize = 1024;
        let mut buf: Vec<u8> = vec![0; CAP];

        // Requires that repr is a valid pointer
        let full_len = unsafe {
            assert!(common::Error::is_ok(status));
            versioned_function!(uloc_canonicalize)(
                repr.as_ptr(),
                buf.as_mut_ptr() as *mut raw::c_char,
                CAP as i32,
                &mut status,
            )
        } as usize;
        common::Error::ok_or_warning(status)?;
        if full_len > CAP {
            buf.resize(full_len, 0);
            // Same unsafe requirements as above, plus full_len must be exactly
            // the output buffer size.
            unsafe {
                assert!(common::Error::is_ok(status));
                versioned_function!(uloc_canonicalize)(
                    repr.as_ptr(),
                    buf.as_mut_ptr() as *mut raw::c_char,
                    full_len as i32,
                    &mut status,
                )
            };
            common::Error::ok_or_warning(status)?;
        }
        // Adjust the size of the buffer here.
        buf.resize(full_len, 0);
        let s = String::from_utf8(buf);
        match s {
            Ok(repr) => Ok(ULoc { repr }),
            Err(_) => Err(common::Error::string_with_interior_nul()),
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
}

/// Gets the current system default locale.
///
/// Implements `uloc_getDefault` from ICU4C.
pub fn get_default() -> ULoc {
    let loc = unsafe { versioned_function!(uloc_getDefault)() };
    let uloc_cstr = unsafe { ffi::CStr::from_ptr(loc) };
    crate::ULoc::try_from(uloc_cstr).expect("could not convert default locale to ULoc")
}

/// Sets the current defaultl system locale.
///
/// Implements `uloc_setDefault` from ICU4C.
pub fn set_default(loc: &ULoc) -> Result<(), common::Error> {
    let mut status = common::Error::OK_CODE;
    let asciiz = ffi::CString::new(loc.repr.clone())
        .map_err(|_| common::Error::string_with_interior_nul())?;
    unsafe { versioned_function!(uloc_setDefault)(asciiz.as_ptr(), &mut status) };
    common::Error::ok_or_warning(status)
}

#[cfg(test)]
mod tests {
    use super::*;

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
}
