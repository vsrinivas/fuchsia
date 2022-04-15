// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl::endpoints::Proxy;
use fidl_fuchsia_bluetooth_bredr as bredr;
use fuchsia_bluetooth::{profile::ValidScoConnectionParameters, types::PeerId};
use fuchsia_inspect_derive::Unit;
use futures::{Future, FutureExt, StreamExt};
use std::convert::TryInto;
use tracing::{debug, info};

use crate::error::ScoConnectError;
use crate::features::CodecId;

/// The components of an active SCO connection.
/// Dropping this struct will close the SCO connection.
#[derive(Debug)]
pub struct ScoConnection {
    /// The parameters that this connection was set up with.
    pub params: ValidScoConnectionParameters,
    /// Protocol which holds the connection open. Held so when this is dropped the connection closes.
    proxy: bredr::ScoConnectionProxy,
}

impl Unit for ScoConnection {
    type Data = <ValidScoConnectionParameters as Unit>::Data;
    fn inspect_create(&self, parent: &fuchsia_inspect::Node, name: impl AsRef<str>) -> Self::Data {
        self.params.inspect_create(parent, name)
    }

    fn inspect_update(&self, data: &mut Self::Data) {
        self.params.inspect_update(data)
    }
}

impl ScoConnection {
    pub fn on_closed(&self) -> impl Future<Output = ()> + 'static {
        self.proxy.on_closed().extend_lifetime().map(|_| ())
    }

    pub fn is_closed(&self) -> bool {
        self.proxy.is_closed()
    }

    #[cfg(test)]
    pub fn build(params: bredr::ScoConnectionParameters, proxy: bredr::ScoConnectionProxy) -> Self {
        ScoConnection { params: params.try_into().unwrap(), proxy }
    }
}

#[derive(Clone)]
pub struct ScoConnector {
    proxy: bredr::ProfileProxy,
}

const COMMON_SCO_PARAMS: bredr::ScoConnectionParameters = bredr::ScoConnectionParameters {
    air_frame_size: Some(60), // Chosen to match legacy usage.
    // IO parameters are to fit 16-bit PSM Signed audio input expected from the audio chip.
    io_coding_format: Some(bredr::CodingFormat::LinearPcm),
    io_frame_size: Some(16),
    io_pcm_data_format: Some(fidl_fuchsia_hardware_audio::SampleFormat::PcmSigned),
    path: Some(bredr::DataPath::Offload),
    ..bredr::ScoConnectionParameters::EMPTY
};

/// If all eSCO parameters fail to setup a connection, these parameters are required to be
/// supported by all peers.  HFP 1.8 Section 5.7.1.
const SCO_PARAMS_FALLBACK: bredr::ScoConnectionParameters = bredr::ScoConnectionParameters {
    parameter_set: Some(bredr::HfpParameterSet::CvsdD1),
    air_coding_format: Some(bredr::CodingFormat::Cvsd),
    // IO bandwidth to match an 8khz audio rate.
    io_bandwidth: Some(16000),
    ..COMMON_SCO_PARAMS
};

// pub in this crate for tests
pub(crate) fn parameter_sets_for_codec(codec_id: CodecId) -> Vec<bredr::ScoConnectionParameters> {
    use bredr::HfpParameterSet::*;
    match codec_id {
        CodecId::MSBC => {
            let params_fn = |set| bredr::ScoConnectionParameters {
                parameter_set: Some(set),
                air_coding_format: Some(bredr::CodingFormat::Msbc),
                // IO bandwidth to match an 16khz audio rate. (x2 for input + output)
                io_bandwidth: Some(32000),
                ..COMMON_SCO_PARAMS
            };
            // TODO(b/200305833): Disable MsbcT1 for now as it results in bad audio
            //vec![params_fn(MsbcT2), params_fn(MsbcT1)]
            vec![params_fn(MsbcT2)]
        }
        // CVSD parameter sets
        _ => {
            let params_fn = |set| bredr::ScoConnectionParameters {
                parameter_set: Some(set),
                ..SCO_PARAMS_FALLBACK
            };
            vec![params_fn(CvsdS4), params_fn(CvsdS1)]
        }
    }
}

fn parameters_for_codecs(codecs: Vec<CodecId>) -> Vec<bredr::ScoConnectionParameters> {
    codecs
        .into_iter()
        .map(parameter_sets_for_codec)
        .flatten()
        .chain([SCO_PARAMS_FALLBACK.clone()])
        .collect()
}

#[derive(Debug, Clone, PartialEq, Copy)]
enum ScoInitiatorRole {
    Initiate,
    Accept,
}

impl ScoConnector {
    pub fn build(proxy: bredr::ProfileProxy) -> Self {
        Self { proxy }
    }

    async fn setup_sco_connection(
        proxy: bredr::ProfileProxy,
        peer_id: PeerId,
        role: ScoInitiatorRole,
        params: Vec<bredr::ScoConnectionParameters>,
    ) -> Result<ScoConnection, ScoConnectError> {
        let (client, mut requests) =
            fidl::endpoints::create_request_stream::<bredr::ScoConnectionReceiverMarker>()?;
        proxy.connect_sco(
            &mut peer_id.into(),
            /* initiate = */ role == ScoInitiatorRole::Initiate,
            &mut params.into_iter(),
            client,
        )?;
        let connection = match requests.next().await {
            Some(Ok(bredr::ScoConnectionReceiverRequest::Connected {
                connection,
                params,
                control_handle: _,
            })) => {
                let params = params.try_into().map_err(|_| ScoConnectError::ScoInvalidArguments)?;
                let proxy = connection.into_proxy().map_err(|_| ScoConnectError::ScoFailed)?;
                ScoConnection { params, proxy }
            }
            Some(Ok(bredr::ScoConnectionReceiverRequest::Error { error, .. })) => {
                return Err(error.into())
            }
            Some(Err(e)) => return Err(e.into()),
            None => return Err(ScoConnectError::ScoCanceled),
        };
        Ok(connection)
    }

    pub fn connect(
        &self,
        peer_id: PeerId,
        codecs: Vec<CodecId>,
    ) -> impl Future<Output = Result<ScoConnection, ScoConnectError>> + 'static {
        let params = parameters_for_codecs(codecs);
        info!("Initiating SCO connection for {}: {:?}", peer_id, &params);

        let proxy = self.proxy.clone();
        async move {
            for param in params {
                let result = Self::setup_sco_connection(
                    proxy.clone(),
                    peer_id,
                    ScoInitiatorRole::Initiate,
                    vec![param.clone()],
                )
                .await;
                match &result {
                    // Return early if there is a FIDL issue, or we succeeded.
                    Err(ScoConnectError::Fidl { .. }) | Ok(_) => return result,
                    // Otherwise continue to try the next params.
                    Err(e) => debug!(?peer_id, ?param, ?e, "Connection failed, trying next set.."),
                }
            }
            info!(?peer_id, "Exhausted SCO connection parameters");
            Err(ScoConnectError::ScoFailed)
        }
    }

    pub fn accept(
        &self,
        peer_id: PeerId,
        codecs: Vec<CodecId>,
    ) -> impl Future<Output = Result<ScoConnection, ScoConnectError>> + 'static {
        let params = parameters_for_codecs(codecs);
        info!("Accepting SCO connection for {}: {:?}.", peer_id, &params);

        let proxy = self.proxy.clone();
        Self::setup_sco_connection(proxy, peer_id, ScoInitiatorRole::Accept, params)
    }
}
