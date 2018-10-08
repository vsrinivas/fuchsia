// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::{format_err, Error};

use fuchsia_wayland_core as wl;
use wayland::{WlCompositor, WlCompositorRequest};

pub struct Compositor;

impl Compositor {
    pub fn new() -> Self {
        Compositor
    }
}

impl wl::RequestReceiver<WlCompositor> for Compositor {
    fn receive(
        _this: wl::ObjectRef<Self>, _request: WlCompositorRequest, _client: &mut wl::Client,
    ) -> Result<(), Error> {
        Err(format_err!("wl_compositor is not implemented"))
    }
}
