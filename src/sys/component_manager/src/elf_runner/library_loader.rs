// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{format_err, Error},
    fidl::endpoints::RequestStream,
    fidl_fuchsia_io::{DirectoryProxy, VMO_FLAG_READ},
    fidl_fuchsia_ldsvc::{LoaderRequest, LoaderRequestStream},
    fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::{TryFutureExt, TryStreamExt},
    io_util,
    log::*,
    std::path::PathBuf,
};

/// start will expose the `fuchsia.ldsvc.Loader` service over the given channel, providing VMO
/// buffers of requested library object names from `lib_proxy`.
pub fn start(lib_proxy: DirectoryProxy, chan: zx::Channel) {
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
                        let object_name = object_name.trim_matches(char::from(0)).to_string();
                        match await!(load_vmo(&lib_proxy, object_name)) {
                            Ok(b) => responder.send(zx::sys::ZX_OK, Some(b))?,
                            Err(e) => {
                                warn!("failed to load object: {}", e);
                                responder.send(zx::sys::ZX_ERR_NOT_FOUND, None)?;
                            }
                        }
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
                        let new_lib_proxy = io_util::clone_directory(&lib_proxy)?;
                        start(new_lib_proxy, loader.into_channel());
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
    );
}

/// load_vmo will attempt to open the provided name in `dir_proxy` and return an executable VMO
/// with the contents.
pub async fn load_vmo(dir_proxy: &DirectoryProxy, object_name: String) -> Result<zx::Vmo, Error> {
    let file_proxy = io_util::open_file(dir_proxy, &PathBuf::from(&object_name))?;
    let (status, fidlbuf) = await!(file_proxy.get_buffer(VMO_FLAG_READ))
        .map_err(|e| format_err!("reading object at {:?} failed: {}", object_name, e))?;
    let status = zx::Status::from_raw(status);
    if status != zx::Status::OK {
        return Err(format_err!("reading object at {:?} failed: {}", object_name, status));
    }
    fidlbuf
        .ok_or(format_err!("no buffer returned from GetBuffer"))?
        .vmo
        .replace_as_executable()
        .map_err(|status| format_err!("failed to replace VMO as executable: {}", status))
}

#[cfg(test)]
mod tests {
    use {super::*, fidl::endpoints::Proxy, fidl_fuchsia_ldsvc::LoaderProxy};

    #[fasync::run_singlethreaded(test)]
    async fn load_objects_test() -> Result<(), Error> {
        let pkg_lib = io_util::open_directory_in_namespace("/pkg/lib")?;
        let (client_chan, service_chan) = zx::Channel::create()?;
        start(pkg_lib, service_chan);

        let loader = LoaderProxy::from_channel(fasync::Channel::from_channel(client_chan)?);

        for (obj_name, should_succeed) in vec![
            // Should be able to access lib/ld.so.1
            ("ld.so.1", true),
            // Should be able to access lib/libfdio.so
            ("libfdio.so", true),
            // Should not be able to access lib/lib/ld.so.1
            ("lib/ld.so.1", false),
            // Should not be able to access lib/../lib/ld.so.1
            ("../lib/ld.so.1", false),
            // Should not be able to access test/component_manager_tests
            ("../test/component_manager_tests", false),
            // Should not be able to access lib/bin/hello_world
            ("bin/hello_world", false),
            // Should not be able to access bin/hello_world
            ("../bin/hello_world", false),
            // Should not be able to access meta/component_manager_tests_hello_world.cm
            ("../meta/component_manager_tests_hello_world.cm", false),
        ] {
            let (res, o_vmo) = await!(loader.load_object(obj_name))?;
            if should_succeed {
                assert_eq!(zx::sys::ZX_OK, res);
                assert!(o_vmo.is_some());
            } else {
                assert_ne!(zx::sys::ZX_OK, res);
                assert!(o_vmo.is_none());
            }
        }
        Ok(())
    }
}
