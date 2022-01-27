// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        capability::CapabilitySource,
        model::{
            component::{ComponentInstance, StartReason},
            error::ModelError,
            routing::{RouteRequest, RouteSource},
        },
    },
    fuchsia_zircon as zx,
    std::{path::PathBuf, sync::Arc},
};

/// A container for the data needed to open a capability.
pub enum OpenOptions<'a> {
    Directory(OpenDirectoryOptions<'a>),
    Protocol(OpenProtocolOptions<'a>),
    Resolver(OpenResolverOptions<'a>),
    Runner(OpenRunnerOptions<'a>),
    Service(OpenServiceOptions<'a>),
    Storage(OpenStorageOptions<'a>),
}

impl<'a> OpenOptions<'a> {
    /// Creates an `OpenOptions` for a capability that can be installed in a namespace,
    /// or an error if `route_request` specifies a capability that cannot be installed
    /// in a namespace.
    pub fn for_namespace_capability(
        route_request: &RouteRequest,
        flags: u32,
        open_mode: u32,
        relative_path: String,
        server_chan: &'a mut zx::Channel,
    ) -> Result<Self, ModelError> {
        match route_request {
            RouteRequest::UseDirectory(_) | RouteRequest::ExposeDirectory(_) => {
                Ok(Self::Directory(OpenDirectoryOptions {
                    flags,
                    open_mode,
                    relative_path,
                    server_chan,
                }))
            }
            RouteRequest::UseProtocol(_) | RouteRequest::ExposeProtocol(_) => {
                Ok(Self::Protocol(OpenProtocolOptions {
                    flags,
                    open_mode,
                    relative_path,
                    server_chan,
                }))
            }
            RouteRequest::UseService(_) | RouteRequest::ExposeService(_) => {
                Ok(Self::Service(OpenServiceOptions {
                    flags,
                    open_mode,
                    relative_path,
                    server_chan,
                }))
            }
            // TODO(fxbug.dev/50716): This StartReason is wrong. We need to refactor the Storage
            // capability to plumb through the correct StartReason.
            RouteRequest::UseStorage(_) => Ok(Self::Storage(OpenStorageOptions {
                open_mode,
                server_chan,
                start_reason: StartReason::Eager,
            })),
            _ => Err(ModelError::unsupported("capability cannot be installed in a namespace")),
        }
    }
}

pub struct OpenDirectoryOptions<'a> {
    pub flags: u32,
    pub open_mode: u32,
    pub relative_path: String,
    pub server_chan: &'a mut zx::Channel,
}

pub struct OpenProtocolOptions<'a> {
    pub flags: u32,
    pub open_mode: u32,
    pub relative_path: String,
    pub server_chan: &'a mut zx::Channel,
}

pub struct OpenResolverOptions<'a> {
    pub flags: u32,
    pub open_mode: u32,
    pub server_chan: &'a mut zx::Channel,
}

pub struct OpenRunnerOptions<'a> {
    pub flags: u32,
    pub open_mode: u32,
    pub server_chan: &'a mut zx::Channel,
}

pub struct OpenServiceOptions<'a> {
    pub flags: u32,
    pub open_mode: u32,
    pub relative_path: String,
    pub server_chan: &'a mut zx::Channel,
}

pub struct OpenStorageOptions<'a> {
    pub open_mode: u32,
    pub server_chan: &'a mut zx::Channel,
    pub start_reason: StartReason,
}

/// A request to open a capability at its source.
pub struct OpenRequest<'a> {
    pub flags: u32,
    pub open_mode: u32,
    pub relative_path: PathBuf,
    pub source: CapabilitySource,
    pub target: &'a Arc<ComponentInstance>,
    pub server_chan: &'a mut zx::Channel,
}

impl<'a> OpenRequest<'a> {
    /// Creates a request to open a capability with source `route_source` for `target`.
    pub fn new(
        route_source: RouteSource,
        target: &'a Arc<ComponentInstance>,
        options: OpenOptions<'a>,
    ) -> Self {
        match route_source {
            RouteSource::Directory(source, directory_state) => {
                if let OpenOptions::Directory(open_dir_options) = options {
                    return Self {
                        flags: open_dir_options.flags,
                        open_mode: open_dir_options.open_mode,
                        relative_path: directory_state
                            .make_relative_path(open_dir_options.relative_path),
                        source,
                        target,
                        server_chan: open_dir_options.server_chan,
                    };
                }
            }
            RouteSource::Protocol(source) => {
                if let OpenOptions::Protocol(open_protocol_options) = options {
                    return Self {
                        flags: open_protocol_options.flags,
                        open_mode: open_protocol_options.open_mode,
                        relative_path: PathBuf::from(open_protocol_options.relative_path),
                        source,
                        target,
                        server_chan: open_protocol_options.server_chan,
                    };
                }
            }
            RouteSource::Service(source) => {
                if let OpenOptions::Service(open_service_options) = options {
                    return Self {
                        flags: open_service_options.flags,
                        open_mode: open_service_options.open_mode,
                        relative_path: PathBuf::from(open_service_options.relative_path),
                        source,
                        target,
                        server_chan: open_service_options.server_chan,
                    };
                }
            }
            RouteSource::Resolver(source) => {
                if let OpenOptions::Resolver(open_resolver_options) = options {
                    return Self {
                        flags: open_resolver_options.flags,
                        open_mode: open_resolver_options.open_mode,
                        relative_path: PathBuf::new(),
                        source,
                        target,
                        server_chan: open_resolver_options.server_chan,
                    };
                }
            }
            RouteSource::Runner(source) => {
                if let OpenOptions::Runner(open_runner_options) = options {
                    return Self {
                        flags: open_runner_options.flags,
                        open_mode: open_runner_options.open_mode,
                        relative_path: PathBuf::new(),
                        source,
                        target,
                        server_chan: open_runner_options.server_chan,
                    };
                }
            }
            _ => panic!("unsupported route source"),
        }
        panic!("route source type did not match option type")
    }
}
