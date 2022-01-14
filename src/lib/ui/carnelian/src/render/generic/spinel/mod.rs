// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::mem::MaybeUninit;

use euclid::default::Size2D;
use fidl::endpoints::ClientEnd;
use fidl_fuchsia_sysmem::BufferCollectionTokenMarker;
use fuchsia_vulkan::*;

use crate::{drawing::DisplayRotation, render::generic::Backend};

macro_rules! spn {
    ( $result:expr ) => {{
        let result = $result;
        assert_eq!(
            result,
            ::spinel_rs_sys::SpnResult::SpnSuccess,
            "Spinel failed with: {:?}",
            result,
        );
    }};
}

macro_rules! vk {
    ( $result:expr ) => {{
        let result = $result;
        assert_eq!(result, ::vk_sys::SUCCESS, "Vulkan failed with: {:?}", result);
    }};
}

macro_rules! cstr {
    ( $bytes:expr ) => {
        ::std::ffi::CStr::from_bytes_with_nul($bytes).expect("CStr must end with '\\0'")
    };
}

pub unsafe fn init<T>(f: impl FnOnce(*mut T)) -> T {
    let mut value = MaybeUninit::uninit();
    f(value.as_mut_ptr());
    value.assume_init()
}

mod composition;
mod context;
mod image;
mod path;
mod raster;

pub use composition::SpinelComposition;
pub use context::InnerContext;
pub use context::SpinelContext;
pub use image::SpinelImage;
pub use path::{SpinelPath, SpinelPathBuilder};
pub use raster::{SpinelRaster, SpinelRasterBuilder};

#[derive(Clone, Copy, Debug, Default, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub struct Spinel;

impl Backend for Spinel {
    type Image = SpinelImage;
    type Context = SpinelContext;
    type Path = SpinelPath;
    type PathBuilder = SpinelPathBuilder;
    type Raster = SpinelRaster;
    type RasterBuilder = SpinelRasterBuilder;
    type Composition = SpinelComposition;

    fn new_context(
        token: ClientEnd<BufferCollectionTokenMarker>,
        size: Size2D<u32>,
        display_rotation: DisplayRotation,
    ) -> SpinelContext {
        SpinelContext::new(token, size, display_rotation)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use fidl::endpoints::create_endpoints;

    use euclid::size2;

    use crate::{drawing::DisplayRotation, render::generic};

    #[test]
    fn spinel_init() {
        generic::tests::run(|| {
            let (token, _) =
                create_endpoints::<BufferCollectionTokenMarker>().expect("create_endpoint");
            Spinel::new_context(token, size2(100, 100), DisplayRotation::Deg0);
        });
    }
}
