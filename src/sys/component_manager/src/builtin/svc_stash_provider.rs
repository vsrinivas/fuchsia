// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::builtin::capability::BuiltinCapability,
    ::routing::capability_source::InternalCapability, anyhow::Error, async_trait::async_trait,
    cm_rust::CapabilityName, fidl::endpoints::ProtocolMarker, fidl_fuchsia_boot as fuchsia_boot,
    fuchsia_zircon::Channel, futures::prelude::*, lazy_static::lazy_static, parking_lot::Mutex,
    std::sync::Arc,
};

lazy_static! {
    static ref SVC_STASH_CAPABILITY_NAME: CapabilityName = "fuchsia.boot.SvcStashProvider".into();
}

pub struct SvcStashCapability {
    channel: Mutex<Option<fidl::endpoints::ServerEnd<fuchsia_boot::SvcStashMarker>>>,
}

impl SvcStashCapability {
    pub fn new(channel: Channel) -> Arc<Self> {
        Arc::new(Self { channel: Mutex::new(Some(fidl::endpoints::ServerEnd::new(channel))) })
    }
}

#[async_trait]
impl BuiltinCapability for SvcStashCapability {
    const NAME: &'static str = "SvcStashProviderCapability";
    type Marker = fuchsia_boot::SvcStashProviderMarker;

    async fn serve(
        self: Arc<Self>,
        mut stream: <Self::Marker as ProtocolMarker>::RequestStream,
    ) -> Result<(), Error> {
        while let Some(fuchsia_boot::SvcStashProviderRequest::Get { responder }) =
            stream.try_next().await?
        {
            // If the channel is valid return it, if not ZX_ERR_NO_UNAVAILABLE.
            let channel = self.channel.lock().take();
            match channel {
                Some(channel) => responder.send(&mut Ok(channel))?,
                None => responder.send(&mut Err(fuchsia_zircon::Status::UNAVAILABLE.into_raw()))?,
            }
        }
        Ok(())
    }

    fn matches_routed_capability(&self, capability: &InternalCapability) -> bool {
        capability.matches_protocol(&SVC_STASH_CAPABILITY_NAME)
    }
}

#[cfg(all(test))]
mod tests {
    use {
        super::*,
        crate::{
            builtin::capability::BuiltinCapability,
            capability::CapabilitySource,
            model::hooks::{Event, EventPayload, Hooks},
        },
        cm_task_scope::TaskScope,
        fidl::endpoints::ClientEnd,
        fidl_fuchsia_io as fio,
        fuchsia_zircon::sys,
        fuchsia_zircon::AsHandleRef,
        futures::lock::Mutex,
        moniker::{AbsoluteMoniker, AbsoluteMonikerBase},
        std::path::PathBuf,
        std::sync::Weak,
    };

    // Just need a channel to stash.
    async fn get_svc_stash_handle() -> Result<Channel, Error> {
        let (_p1, p2) = Channel::create()?;
        Ok(p2)
    }

    #[fuchsia::test]
    async fn can_connect_to_svc_stash_provider_service() -> Result<(), Error> {
        let svc_stash_provider = SvcStashCapability::new(get_svc_stash_handle().await?);
        let hooks = Hooks::new();
        hooks.install(svc_stash_provider.hooks()).await;

        let provider = Arc::new(Mutex::new(None));
        let source = CapabilitySource::Builtin {
            capability: InternalCapability::Protocol(SVC_STASH_CAPABILITY_NAME.clone()),
            top_instance: Weak::new(),
        };

        let event = Event::new_for_test(
            AbsoluteMoniker::root(),
            "fuchsia-pkg://root",
            Ok(EventPayload::CapabilityRouted { source, capability_provider: provider.clone() }),
        );
        hooks.dispatch(&event).await?;

        let (client, mut server) = Channel::create()?;
        let task_scope = TaskScope::new();
        if let Some(provider) = provider.lock().await.take() {
            provider
                .open(task_scope.clone(), fio::OpenFlags::empty(), 0, PathBuf::new(), &mut server)
                .await?;
        };

        let svc_stash_provider_client =
            ClientEnd::<fuchsia_boot::SvcStashProviderMarker>::new(client)
                .into_proxy()
                .expect("failed to create launcher proxy");
        let svc_stash = svc_stash_provider_client.get().await?;
        assert_ne!(svc_stash.unwrap().raw_handle(), sys::ZX_HANDLE_INVALID);

        // Second call must fail.
        let svc_stash_2 = svc_stash_provider_client.get().await?;
        assert!(svc_stash_2.is_err());
        Ok(())
    }
}
