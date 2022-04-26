// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_runtime::{take_startup_handle, HandleInfo, HandleType},
    fuchsia_zircon::Socket,
};

// This program take a startup handle as a socket and write to it.
fn main() {
    // Uncomment the next line to see if the default job is the job passed in numbered_handles.
    // print!("job_default's name is {:?}", job_default().get_name().expect("fail to get name"));

    let socket: Socket = take_startup_handle(HandleInfo::new(HandleType::User0, 0))
        .expect("fail to take startup handle")
        .into();
    socket.write(b"Hello, World!").expect("fail to write socket");
}
