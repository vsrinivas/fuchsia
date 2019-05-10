// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::ns_util::{self, PKG_PATH},
    failure::{err_msg, format_err, Error},
    fidl::endpoints::RequestStream,
    fidl_fuchsia_io::{DirectoryProxy, VMO_FLAG_EXEC, VMO_FLAG_READ},
    fidl_fuchsia_ldsvc::{LoaderRequest, LoaderRequestStream},
    fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::{TryFutureExt, TryStreamExt},
    io_util,
    log::*,
    std::collections::HashMap,
    std::path::PathBuf,
};

/// start will expose the ldsvc.fidl service over the given channel, providing VMO buffers of
/// requested library object names from the given namespace.
pub fn start(ns_map: HashMap<PathBuf, DirectoryProxy>, chan: zx::Channel) {
    fasync::spawn(
        async move {
            // Wait for requests
            let mut stream =
                LoaderRequestStream::from_channel(fasync::Channel::from_channel(chan)?);
            while let Some(req) = await!(stream.try_next())? {
                match req {
                    LoaderRequest::Done { control_handle } => {
                        control_handle.shutdown();
                    }
                    LoaderRequest::LoadObject { object_name, responder } => {
                        // TODO(ZX-3392): The name provided by the client here has a null byte at
                        // the end, which doesn't work from here on out (io.fidl doesn't like it).
                        let object_name = object_name.trim_matches(char::from(0));
                        let object_path = PKG_PATH.join("lib").join(object_name);
                        match await!(load_object(&ns_map, PathBuf::from(object_path))) {
                            Ok(b) => responder.send(zx::sys::ZX_OK, Some(b))?,
                            Err(e) => {
                                warn!("failed to load object: {}", e);
                                responder.send(zx::sys::ZX_ERR_NOT_FOUND, None)?;
                            }
                        };
                    }
                    LoaderRequest::LoadScriptInterpreter { interpreter_name: _, responder } => {
                        // Unimplemented
                        responder.control_handle().shutdown();
                    }
                    LoaderRequest::Config { config: _, responder } => {
                        // Unimplemented
                        responder.control_handle().shutdown();
                    }
                    LoaderRequest::Clone { loader, responder } => {
                        let new_ns_map = ns_util::clone_component_namespace_map(&ns_map)?;
                        start(new_ns_map, loader.into_channel());
                        responder.send(zx::sys::ZX_OK)?;
                    }
                    LoaderRequest::DebugPublishDataSink { data_sink: _, data: _, responder } => {
                        // Unimplemented
                        responder.control_handle().shutdown();
                    }
                    LoaderRequest::DebugLoadConfig { config_name: _, responder } => {
                        // Unimplemented
                        responder.control_handle().shutdown();
                    }
                }
            }
            Ok(())
        }
            .unwrap_or_else(|e: Error| warn!("couldn't run library loader service: {}", e)),
    ); // TODO
}

/// load_object will find the named object in the given namespace and return a VMO containing its
/// contents.
pub async fn load_object(
    ns_map: &HashMap<PathBuf, DirectoryProxy>,
    object_path: PathBuf,
) -> Result<zx::Vmo, Error> {
    for (ns_prefix, current_dir) in ns_map.iter() {
        if object_path.starts_with(ns_prefix) {
            let sub_path = pathbuf_drop_prefix(&object_path, ns_prefix);
            let file_proxy = io_util::open_file(current_dir, &sub_path)?;
            let (status, fidlbuf) = await!(file_proxy.get_buffer(VMO_FLAG_READ | VMO_FLAG_EXEC))
                .map_err(|e| format_err!("reading object at {:?} failed: {}", object_path, e))?;
            let status = zx::Status::from_raw(status);
            if status != zx::Status::OK {
                return Err(format_err!("reading object at {:?} failed: {}", object_path, status));
            }
            return fidlbuf
                .map(|b| b.vmo)
                .ok_or(format_err!("bad status received on get_buffer: {}", status));
        }
    }
    Err(err_msg(format!("requested library not found: {:?}", object_path)))
}

fn pathbuf_drop_prefix(path: &PathBuf, prefix: &PathBuf) -> PathBuf {
    path.clone().iter().skip(prefix.iter().count()).collect()
}
