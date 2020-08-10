// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{builtin::capability::BuiltinCapability, capability::*},
    anyhow::Error,
    async_trait::async_trait,
    cm_rust::{CapabilityNameOrPath, CapabilityPath},
    fidl_fuchsia_boot as fboot,
    fuchsia_runtime::job_default,
    fuchsia_zircon as zx,
    futures::prelude::*,
    lazy_static::lazy_static,
    std::{convert::TryInto, sync::Arc},
};

lazy_static! {
    pub static ref ROOT_JOB_CAPABILITY_PATH: CapabilityPath =
        "/svc/fuchsia.boot.RootJob".try_into().unwrap();
    pub static ref ROOT_JOB_FOR_INSPECT_CAPABILITY_PATH: CapabilityPath =
        "/svc/fuchsia.boot.RootJobForInspect".try_into().unwrap();
}

/// An implementation of the `fuchsia.boot.RootJob` protocol.
pub struct RootJob {
    capability_path: &'static CapabilityPath,
    rights: zx::Rights,
}

impl RootJob {
    pub fn new(capability_path: &'static CapabilityPath, rights: zx::Rights) -> Arc<Self> {
        Arc::new(Self { capability_path, rights })
    }
}

#[async_trait]
impl BuiltinCapability for RootJob {
    const NAME: &'static str = "RootJob";
    type Marker = fboot::RootJobMarker;

    async fn serve(self: Arc<Self>, mut stream: fboot::RootJobRequestStream) -> Result<(), Error> {
        let job = job_default();
        while let Some(fboot::RootJobRequest::Get { responder }) = stream.try_next().await? {
            responder.send(job.duplicate(self.rights)?)?;
        }
        Ok(())
    }

    fn matches_routed_capability(&self, capability: &InternalCapability) -> bool {
        matches!(capability, InternalCapability::Protocol(CapabilityNameOrPath::Path(path)) if *path == *self.capability_path)
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::model::{
            hooks::{Event, EventPayload, Hooks},
            moniker::AbsoluteMoniker,
        },
        fidl::endpoints::ClientEnd,
        fuchsia_async as fasync,
        fuchsia_zircon::AsHandleRef,
        fuchsia_zircon_sys as sys,
        futures::lock::Mutex,
        std::path::PathBuf,
    };

    #[fasync::run_singlethreaded(test)]
    async fn has_correct_rights() -> Result<(), Error> {
        let root_job = RootJob::new(&ROOT_JOB_CAPABILITY_PATH, zx::Rights::TRANSFER);
        let (proxy, stream) = fidl::endpoints::create_proxy_and_stream::<fboot::RootJobMarker>()?;
        fasync::Task::local(
            root_job.serve(stream).unwrap_or_else(|err| panic!("Error serving root job: {}", err)),
        )
        .detach();

        let root_job = proxy.get().await?;
        let info = zx::Handle::from(root_job).basic_info()?;
        assert_eq!(info.rights, zx::Rights::TRANSFER);
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn can_connect() -> Result<(), Error> {
        let root_job = RootJob::new(&ROOT_JOB_CAPABILITY_PATH, zx::Rights::SAME_RIGHTS);
        let hooks = Hooks::new(None);
        hooks.install(root_job.hooks()).await;

        let provider = Arc::new(Mutex::new(None));
        let source = CapabilitySource::AboveRoot {
            capability: InternalCapability::Protocol(CapabilityNameOrPath::Path(
                ROOT_JOB_CAPABILITY_PATH.clone(),
            )),
        };

        let event = Event::new_for_test(
            AbsoluteMoniker::root(),
            "fuchsia-pkg://root",
            Ok(EventPayload::CapabilityRouted { source, capability_provider: provider.clone() }),
        );
        hooks.dispatch(&event).await?;

        let (client, mut server) = zx::Channel::create()?;
        if let Some(provider) = provider.lock().await.take() {
            provider.open(0, 0, PathBuf::new(), &mut server).await?;
        }

        let client = ClientEnd::<fboot::RootJobMarker>::new(client)
            .into_proxy()
            .expect("Failed to create proxy");
        let handle = client.get().await?;
        assert_ne!(handle.raw_handle(), sys::ZX_HANDLE_INVALID);
        Ok(())
    }
}
