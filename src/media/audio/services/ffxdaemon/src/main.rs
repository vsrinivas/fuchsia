// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_audio_ffxdaemon::AudioDaemonCreateAudioRendererResponse;

use {
    anyhow::{self, Context, Error},
    fidl::{
        self,
        endpoints::{ClientEnd, Proxy},
        HandleBased,
    },
    fidl_fuchsia_audio_ffxdaemon::{
        AudioDaemonRequest, AudioDaemonRequestStream, VmoWrapperMarker, VmoWrapperRequest,
    },
    fidl_fuchsia_media, fidl_fuchsia_media_audio,
    fuchsia_component::server::ServiceFs,
    fuchsia_inspect::{component, health::Reporter},
    fuchsia_zircon::{self as zx},
    futures::prelude::*,
};

/// Wraps all hosted protocols into a single type that can be matched against
/// and dispatched.
enum IncomingRequest {
    AudioDaemon(AudioDaemonRequestStream),
}
struct AudioDaemon<'a> {
    // Keep daemon and audio proxy around with same lifetime. Avoid dangling reference
    audio: &'a fidl_fuchsia_media::AudioProxy,
}

// VmoWrapper Protocol. serve() called with each new AudioDaemon created so multiple
// AudioDaemons don't write to the same memory.
async fn serve(
    vmo: zx::Vmo,
    endpoint: fidl::endpoints::ServerEnd<VmoWrapperMarker>,
) -> Result<(), Error> {
    let mut stream = endpoint.into_stream()?;

    // Write vmo, return zx status on responder
    // FIDL asks kernel in try_next if any new messages are sent along channel. (in this case, from ffx)
    while let Ok(Some(request)) = stream.try_next().await {
        match request {
            VmoWrapperRequest::Write { responder, payload } => {
                let data =
                    payload.data.ok_or(anyhow::anyhow!("No data passed to VmoWrapper write."))?;
                let offset = payload
                    .offset
                    .ok_or(anyhow::anyhow!("No offset passed to VmoWrapper write."))?;

                vmo.write(&data, offset)?;
                responder
                    .send(&mut Ok(()))
                    .expect("Error sending response to VmoWrapper write call.");
            }
        }
    }
    Ok(())
}

impl<'a> AudioDaemon<'a> {
    pub fn new(audio_component: &'a fidl_fuchsia_media::AudioProxy) -> Self {
        Self { audio: audio_component }
    }

    async fn serve(&mut self, mut stream: AudioDaemonRequestStream) -> Result<(), Error> {
        while let Ok(Some(request)) = stream.try_next().await {
            match request {
                AudioDaemonRequest::CreateAudioRenderer { payload, responder } => {
                    let (client_end, server_end) = fidl::endpoints::create_endpoints::<
                        fidl_fuchsia_media::AudioRendererMarker,
                    >()?;

                    let (gain_control_client_end, gain_control_server_end) =
                        fidl::endpoints::create_endpoints::<
                            fidl_fuchsia_media_audio::GainControlMarker,
                        >()?;

                    self.audio.create_audio_renderer(server_end)?;

                    let buffer_size = payload
                        .buffer_size
                        .ok_or(anyhow::anyhow!("No buffer size passed to CreateAudioRenderer"))?;
                    let vmo = zx::Vmo::create(buffer_size).expect("Failed to allocate VMO.");

                    let (vmo_client_end, vmo_server_end) =
                        fidl::endpoints::create_endpoints::<VmoWrapperMarker>()?;

                    let proxy = client_end.into_proxy()?;
                    proxy.bind_gain_control(gain_control_server_end)?;
                    proxy.add_payload_buffer(
                        0,
                        vmo // pass same vmo to serve
                            .duplicate_handle(zx::Rights::SAME_RIGHTS)?,
                    )?;

                    // Create endpoints for VmoWrapper, pass server end to serve method, pass client end to responder.
                    // Client end will be used for calls to write() (in this case from ffx play).
                    let response = AudioDaemonCreateAudioRendererResponse {
                        renderer: Some(ClientEnd::new(
                            proxy.into_channel().unwrap().into_zx_channel(),
                        )),
                        vmo_channel: Some(vmo_client_end),
                        gain_control: Some(gain_control_client_end),
                        ..AudioDaemonCreateAudioRendererResponse::EMPTY
                    };
                    responder
                        .send(&mut Ok(response))
                        .expect("Failed to send response to CreateAudioRendererRequest");

                    // Waits for serve to finish so that each new AudioDaemon will get the vmo proxy calls completed.
                    serve(vmo.duplicate_handle(zx::Rights::SAME_RIGHTS).unwrap(), vmo_server_end)
                        .await?;
                }
            }
        }
        Ok(())
    }
}

#[fuchsia::main(logging = true)]
async fn main() -> Result<(), anyhow::Error> {
    let mut service_fs = ServiceFs::new_local();
    let audio_component =
        fuchsia_component::client::connect_to_protocol::<fidl_fuchsia_media::AudioMarker>()
            .context("Failed to connect to fuchsia.media.Audio")?;

    // Initialize inspect
    inspect_runtime::serve(component::inspector(), &mut service_fs)?;
    component::health().set_starting_up();

    // Add services here. E.g:
    service_fs.dir("svc").add_fidl_service(IncomingRequest::AudioDaemon);
    service_fs.take_and_serve_directory_handle().context("Failed to serve outgoing namespace")?;

    component::health().set_ok();

    service_fs
        .for_each_concurrent(None, |request: IncomingRequest| async {
            // match on `request` and handle each protocol.
            let mut audio_daemon = AudioDaemon::new(&audio_component);

            match request {
                IncomingRequest::AudioDaemon(stream) => {
                    audio_daemon.serve(stream).await.unwrap_or_else(|e: Error| {
                        panic!("Couldn't serve audio daemon requests{:?}", e)
                    })
                }
            }
        })
        .await;

    Ok(())
}

#[cfg(test)]
mod tests {
    #[fuchsia::test]
    async fn smoke_test() {
        assert!(true);
    }
}
