#![no_implicit_prelude]

struct Type {
    rep: ::std::ptr::NonNull<::rust_icu_sys::UMessageFormat>,
}

::rust_icu_common::simple_drop_impl!(Type, umsg_close);
