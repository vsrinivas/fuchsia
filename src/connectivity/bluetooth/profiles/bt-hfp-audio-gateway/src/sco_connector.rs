// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl::AsHandleRef;
use fidl_fuchsia_bluetooth_bredr as bredr;
use fuchsia_async as fasync;
use fuchsia_bluetooth::types::PeerId;
use fuchsia_zircon as zx;
use futures::{Future, FutureExt, StreamExt};
use tracing::info;

use crate::error::ScoConnectError;
use crate::features::CodecId;

/// The components of an active SCO connection.
/// Dropping this struct will close the SCO connection.
#[derive(Debug)]
pub struct ScoConnection {
    /// The parameters that this connection was set up with.
    pub params: bredr::ScoConnectionParameters,
    /// Socket which holds the connection open. Held so when this is dropped the connection closes.
    socket: zx::Socket,
}

impl ScoConnection {
    pub fn on_closed(&self) -> impl Future<Output = ()> + 'static {
        fasync::OnSignals::new(&self.socket, zx::Signals::SOCKET_PEER_CLOSED)
            .extend_lifetime()
            .map(|_| ())
    }

    pub fn is_closed(&self) -> bool {
        self.socket
            .as_handle_ref()
            .wait(zx::Signals::SOCKET_PEER_CLOSED, zx::Time::after(zx::Duration::from_nanos(0)))
            .is_ok()
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
pub(crate) fn parameters_for_codec(codec_id: CodecId) -> bredr::ScoConnectionParameters {
    match codec_id {
        CodecId::MSBC => bredr::ScoConnectionParameters {
            parameter_set: Some(bredr::HfpParameterSet::MsbcT2),
            air_coding_format: Some(bredr::CodingFormat::Msbc),
            // IO bandwidth to match an 16khz audio rate.
            io_bandwidth: Some(32000),
            ..COMMON_SCO_PARAMS
        },
        // CVSD fallback
        _ => bredr::ScoConnectionParameters {
            parameter_set: Some(bredr::HfpParameterSet::CvsdS4),
            ..SCO_PARAMS_FALLBACK
        },
    }
}

impl ScoConnector {
    pub fn build(proxy: bredr::ProfileProxy) -> Self {
        Self { proxy }
    }

    async fn await_sco_socket(
        requests: &mut bredr::ScoConnectionReceiverRequestStream,
    ) -> Result<ScoConnection, ScoConnectError> {
        let socket = match requests.next().await {
            Some(Ok(bredr::ScoConnectionReceiverRequest::Connected {
                connection,
                params,
                control_handle: _,
            })) => connection
                .socket
                .map(|socket| ScoConnection { socket, params })
                .ok_or(ScoConnectError::MissingSocket)?,
            Some(Ok(bredr::ScoConnectionReceiverRequest::Error { error, .. })) => {
                return Err(error.into())
            }
            Some(Err(e)) => return Err(e.into()),
            None => return Err(ScoConnectError::ScoCanceled),
        };
        Ok(socket)
    }

    pub async fn connect(
        &self,
        peer_id: PeerId,
        codecs: Vec<CodecId>,
    ) -> Result<ScoConnection, ScoConnectError> {
        let mut params = codecs.into_iter().map(parameters_for_codec).collect::<Vec<_>>();
        params.push(SCO_PARAMS_FALLBACK.clone());

        info!("Initiating SCO connection for {}: {:?}.", peer_id, params.clone(),);

        let mut result = Err(ScoConnectError::ScoFailed);
        for param in params {
            let (client, mut requests) =
                fidl::endpoints::create_request_stream::<bredr::ScoConnectionReceiverMarker>()?;
            self.proxy.connect_sco(
                &mut peer_id.into(),
                /* initiate = */ true,
                &mut vec![param].into_iter(),
                client,
            )?;
            result = Self::await_sco_socket(&mut requests).await;
            if result.is_ok() {
                break;
            }
        }
        result
    }

    pub async fn accept(
        &self,
        peer_id: PeerId,
        codecs: Vec<CodecId>,
    ) -> Result<ScoConnection, ScoConnectError> {
        let mut params = codecs.into_iter().map(parameters_for_codec).collect::<Vec<_>>();
        params.push(SCO_PARAMS_FALLBACK.clone());

        info!("Accepting SCO connection for {}: {:?}.", peer_id, params.clone(),);

        let (client, mut requests) =
            fidl::endpoints::create_request_stream::<bredr::ScoConnectionReceiverMarker>()?;
        self.proxy.connect_sco(
            &mut peer_id.into(),
            /* initiate = */ false,
            &mut params.into_iter(),
            client,
        )?;
        Self::await_sco_socket(&mut requests).await
    }
}
