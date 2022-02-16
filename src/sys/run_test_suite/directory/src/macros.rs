// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Generates an enum value that supports an |all_variants| method that returns all
/// possible values of the enum.
#[macro_export]
macro_rules! enumerable_enum {
    ($(#[$meta:meta])* $name:ident {
        $($(#[$variant_meta:meta])* $variant:ident),*,
    }) => {
        $(#[$meta])*
        pub enum $name {
            $($(#[$variant_meta])* $variant),*
        }

        impl $name {
            #[cfg(test)]
            fn all_variants() -> Vec<Self> {
                vec![
                    $($name::$variant),*
                ]
            }
        }
    }
}

#[cfg(test)]
mod test {
    #[test]
    fn test_all_variants() {
        enumerable_enum! {
            #[derive(Debug, PartialEq)]
            TestEnum {
                VarOne,
                VarTwo,
            }
        }

        assert_eq!(TestEnum::all_variants(), vec![TestEnum::VarOne, TestEnum::VarTwo,]);
    }
}
