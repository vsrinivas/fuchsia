#![no_implicit_prelude]

#[test]
fn hygiene() {
    let mut status = ::rust_icu_sys::UErrorCode::U_ZERO_ERROR;
    unsafe { ::rust_icu_sys::versioned_function!(u_init)(&mut status) };
}
