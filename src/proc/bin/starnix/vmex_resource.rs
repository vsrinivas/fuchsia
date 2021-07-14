// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_kernel as fkernel;
use fuchsia_component::client::connect_channel_to_protocol;
use fuchsia_zircon as zx;
use lazy_static::lazy_static;

lazy_static! {
    pub static ref VMEX_RESOURCE: zx::Resource = {
        let (client_end, server_end) = zx::Channel::create().unwrap();
        connect_channel_to_protocol::<fkernel::VmexResourceMarker>(server_end)
            .expect("couldn't connect to fuchsia.kernel.VmexResource");
        let service = fkernel::VmexResourceSynchronousProxy::new(client_end);
        service.get(zx::Time::INFINITE).expect("couldn't talk to fuchsia.kernel.VmexResource")
    };
}
