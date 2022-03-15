// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use proc_macro::TokenStream;
use quote::quote;

const FXBUG_REGEX: &str = r"https://fxbug\.dev/[0-9]+";

/// Provides an unambiguous conditional compilation tied to a URL for bug
/// tracking.
///
/// This macro expands to annotate items with `[cfg(test)]`, and is meant to be
/// used in production code that is being built iteratively and which has tests,
/// but doesn't yet have production usages outside of test configurations.
///
/// Example:
///
/// ```
/// #[todo_unused::todo_unused("https://fxbug.dev/000")]
/// fn new_yet_unused_functionality() {}
/// ```
/// Expands to
///
/// ```
/// #[cfg(test)]
/// fn new_yet_unused_functionality() {}
/// ```
///
/// `cfg(test)` is preferable to `allow(dead_code)` because it cannot rot, while
/// a dead code allowance may end up outliving its usefulness if it gets used
/// far away. The choice to encode a URL with a trackable and actionable bug is
/// a cultural statement that changes should be kept small enough to be
/// digestible, but that any code that is not exercised in production should be
/// tracked and followed up on with urgency.
#[proc_macro_attribute]
pub fn todo_unused(attrs: TokenStream, input: TokenStream) -> TokenStream {
    if attrs.is_empty() {
        return syn::Error::new_spanned(
            proc_macro2::TokenStream::from(attrs),
            "missing required bug URL",
        )
        .to_compile_error()
        .into();
    }
    let attrs = syn::parse_macro_input!(attrs as syn::LitStr);
    let re = regex::Regex::new(FXBUG_REGEX).expect("failed to compile internal regex");
    if !re.is_match(&attrs.value()) {
        return syn::Error::new_spanned(
            attrs,
            "bug URL must be in the form https://fxbug.dev/0000",
        )
        .to_compile_error()
        .into();
    }

    let input = proc_macro2::TokenStream::from(input);
    quote! {
        #[cfg(test)]
        #input
    }
    .into()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn fxbug_regex() {
        let re = regex::Regex::new(FXBUG_REGEX).unwrap();
        for (str, expect_match) in [
            ("https://fxbug.dev/1234", true),
            ("http://fxbug.dev/1234", false),
            ("https://fxbug.dev/", false),
            ("https://fxbugxdev/1234", false),
        ] {
            assert_eq!(re.is_match(str), expect_match, "{}", str);
        }
    }
}
