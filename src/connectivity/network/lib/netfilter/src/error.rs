// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Context as _;

use fidl_fuchsia_net_filter as filter;

pub trait FidlReturn {
    type Item;
    fn transform_result(self) -> Result<Self::Item, anyhow::Error>;
}

macro_rules! impl_trait {
    ($error:ident) => {
        pub struct $error(pub filter::$error);

        impl std::fmt::Debug for $error {
            fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
                std::fmt::Debug::fmt(&self.0, f)
            }
        }

        impl std::fmt::Display for $error {
            fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
                std::fmt::Debug::fmt(self, f)
            }
        }

        impl std::error::Error for $error {}

        impl<T> FidlReturn for Result<Result<T, filter::$error>, fidl::Error> {
            type Item = T;
            fn transform_result(self) -> Result<Self::Item, anyhow::Error> {
                Ok(self.context("FIDL error")?.map_err($error).context("Filter error")?)
            }
        }
    };
}

impl_trait!(FilterEnableInterfaceError);
impl_trait!(FilterDisableInterfaceError);
impl_trait!(FilterGetRulesError);
impl_trait!(FilterUpdateRulesError);
impl_trait!(FilterGetNatRulesError);
impl_trait!(FilterUpdateNatRulesError);
impl_trait!(FilterGetRdrRulesError);
impl_trait!(FilterUpdateRdrRulesError);
