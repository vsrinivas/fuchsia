// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod errno {
    pub use nix::errno::Errno;
}

pub mod net {
    pub mod if_ {
        pub use nix::net::if_::if_nametoindex;
    }
}

pub use nix::Error;
