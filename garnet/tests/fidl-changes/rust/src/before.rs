// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This builds the version of the implementations before the change is begun against a specified FIDL library.
macro_rules! before_impl {
    ($name:ident, $fidl_library:ident) => {
        struct $name();
        impl $name {
            async fn add_method_service(chan: fasync::Channel) -> Result<(), fidl::Error> {
                let mut stream = $fidl_library::AddMethodRequestStream::from_channel(chan);
                while let Some(req) = stream.try_next().await? {
                    match req {
                        $fidl_library::AddMethodRequest::ExistingMethod { .. } => {}
                    }
                }
                Ok(())
            }

            async fn remove_method_service(chan: fasync::Channel) -> Result<(), fidl::Error> {
                let mut stream = $fidl_library::RemoveMethodRequestStream::from_channel(chan);
                while let Some(req) = stream.try_next().await? {
                    match req {
                        $fidl_library::RemoveMethodRequest::ExistingMethod { .. } => {}
                        $fidl_library::RemoveMethodRequest::OldMethod { .. } => {}
                    }
                }
                Ok(())
            }

            async fn add_event_service(chan: fasync::Channel) -> Result<(), fidl::Error> {
                let mut stream = $fidl_library::AddEventRequestStream::from_channel(chan);
                while let Some(req) = stream.try_next().await? {
                    match req {
                        $fidl_library::AddEventRequest::ExistingMethod { .. } => {}
                    }
                }
                Ok(())
            }

            async fn remove_event_service(chan: fasync::Channel) -> Result<(), fidl::Error> {
                let mut stream = $fidl_library::RemoveEventRequestStream::from_channel(chan);
                while let Some(req) = stream.try_next().await? {
                    match req {
                        $fidl_library::RemoveEventRequest::ExistingMethod { .. } => {}
                    }
                }
                Ok(())
            }
        }
    };
}
