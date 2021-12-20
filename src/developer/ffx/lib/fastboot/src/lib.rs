// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod boot;
pub mod common;
pub mod info;
pub mod lock;
pub mod manifest;
pub mod unlock;

////////////////////////////////////////////////////////////////////////////////
// tests
pub mod test {
    use crate::common::file::FileResolver;
    use anyhow::Result;
    use async_trait::async_trait;
    use fidl::endpoints::{create_proxy_and_stream, Proxy};
    use fidl_fuchsia_developer_bridge::{FastbootProxy, FastbootRequest};
    use std::default::Default;
    use std::io::Write;
    use std::path::{Path, PathBuf};
    use std::sync::{Arc, Mutex};

    #[derive(Default)]
    pub struct FakeServiceCommands {
        pub staged_files: Vec<String>,
        pub oem_commands: Vec<String>,
        pub variables: Vec<String>,
        pub bootloader_reboots: usize,
        pub boots: usize,
    }

    pub struct TestResolver {
        manifest: PathBuf,
    }

    impl TestResolver {
        pub fn new() -> Self {
            let mut test = PathBuf::new();
            test.push("./flash.json");
            Self { manifest: test }
        }
    }

    #[async_trait(?Send)]
    impl FileResolver for TestResolver {
        fn manifest(&self) -> &Path {
            self.manifest.as_path()
        }

        async fn get_file<W: Write>(&mut self, _writer: &mut W, file: &str) -> Result<String> {
            Ok(file.to_owned())
        }
    }

    fn setup_fake_fastboot_proxy<R: 'static>(mut handle_request: R) -> FastbootProxy
    where
        R: FnMut(fidl::endpoints::Request<<FastbootProxy as fidl::endpoints::Proxy>::Protocol>),
    {
        use futures::TryStreamExt;
        let (proxy, mut stream) =
            create_proxy_and_stream::<<FastbootProxy as Proxy>::Protocol>().unwrap();
        fuchsia_async::Task::local(async move {
            while let Ok(Some(req)) = stream.try_next().await {
                handle_request(req);
            }
        })
        .detach();
        proxy
    }

    pub fn setup() -> (Arc<Mutex<FakeServiceCommands>>, FastbootProxy) {
        let state = Arc::new(Mutex::new(FakeServiceCommands { ..Default::default() }));
        (
            state.clone(),
            setup_fake_fastboot_proxy(move |req| match req {
                FastbootRequest::Prepare { listener, responder } => {
                    listener.into_proxy().unwrap().on_reboot().unwrap();
                    responder.send(&mut Ok(())).unwrap();
                }
                FastbootRequest::GetVar { responder, .. } => {
                    let mut state = state.lock().unwrap();
                    let var = state.variables.pop().unwrap_or("test".to_string());
                    responder.send(&mut Ok(var)).unwrap();
                }
                FastbootRequest::GetAllVars { listener, responder, .. } => {
                    listener.into_proxy().unwrap().on_variable("test", "test").unwrap();
                    responder.send(&mut Ok(())).unwrap();
                }
                FastbootRequest::Flash { listener, responder, .. } => {
                    listener.into_proxy().unwrap().on_finished().unwrap();
                    responder.send(&mut Ok(())).unwrap();
                }
                FastbootRequest::GetStaged { responder, .. } => {
                    responder.send(&mut Ok(())).unwrap();
                }
                FastbootRequest::Erase { responder, .. } => {
                    responder.send(&mut Ok(())).unwrap();
                }
                FastbootRequest::Boot { responder } => {
                    let mut state = state.lock().unwrap();
                    state.boots += 1;
                    responder.send(&mut Ok(())).unwrap();
                }
                FastbootRequest::Reboot { responder } => {
                    responder.send(&mut Ok(())).unwrap();
                }
                FastbootRequest::RebootBootloader { listener, responder } => {
                    listener.into_proxy().unwrap().on_reboot().unwrap();
                    let mut state = state.lock().unwrap();
                    state.bootloader_reboots += 1;
                    responder.send(&mut Ok(())).unwrap();
                }
                FastbootRequest::ContinueBoot { responder } => {
                    responder.send(&mut Ok(())).unwrap();
                }
                FastbootRequest::SetActive { responder, .. } => {
                    responder.send(&mut Ok(())).unwrap();
                }
                FastbootRequest::Stage { path, responder, .. } => {
                    let mut state = state.lock().unwrap();
                    state.staged_files.push(path);
                    responder.send(&mut Ok(())).unwrap();
                }
                FastbootRequest::Oem { command, responder } => {
                    let mut state = state.lock().unwrap();
                    state.oem_commands.push(command);
                    responder.send(&mut Ok(())).unwrap();
                }
            }),
        )
    }
}
