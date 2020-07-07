// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Emits the original implementation against a specific FIDL library.
macro_rules! before_impl {
    ($fidl_library:ident) => {
        struct AddMethodFakeProxy;

        impl $fidl_library::AddMethodProxyInterface for AddMethodFakeProxy {
            fn existing_method(&self) -> Result<(), fidl::Error> {
                Ok(())
            }
        }

        struct RemoveMethodFakeProxy;

        impl $fidl_library::RemoveMethodProxyInterface for RemoveMethodFakeProxy {
            fn existing_method(&self) -> Result<(), fidl::Error> {
                Ok(())
            }
            fn old_method(&self) -> Result<(), fidl::Error> {
                Ok(())
            }
        }
    };
}
