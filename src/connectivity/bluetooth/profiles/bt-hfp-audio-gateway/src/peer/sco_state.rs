// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_inspect as inspect;
use fuchsia_inspect_derive::{IValue, Unit};
use futures::future::{Fuse, FusedFuture, Future};
use futures::FutureExt;
use std::{fmt, pin::Pin};
use vigil::Vigil;

use crate::{a2dp, error::ScoConnectError, sco_connector::ScoConnection};

#[derive(Debug)]
pub struct ScoActive {
    pub sco_connection: ScoConnection,
    pub _pause_token: Option<a2dp::PauseToken>,
}

impl Unit for ScoActive {
    type Data = <ScoConnection as Unit>::Data;
    fn inspect_create(&self, parent: &inspect::Node, name: impl AsRef<str>) -> Self::Data {
        self.sco_connection.inspect_create(parent, name)
    }

    fn inspect_update(&self, data: &mut Self::Data) {
        self.sco_connection.inspect_update(data)
    }
}

pub type ScoStateFuture<T> = Pin<Box<dyn Future<Output = Result<T, ScoConnectError>>>>;
pub enum ScoState {
    /// No call is in progress.
    Inactive,
    /// A call has been made active, and we are negotiating codecs before setting up the SCO connection.
    /// This state prevents a race where the call has been made active but SCO not yet set up, and the peer
    /// task, seeing that the connection is not Active, attempts to set up the SCO connection a second time,
    SettingUp,
    /// The HF has closed the remote SCO connection so we are waiting for the call to be set transferred to AG.
    /// This state prevents a race where the SCO connection has been torn down but the call not yet set to inactive
    /// by the call manager, so the peer task attempts to mark the call as inactive a second time.
    TearingDown,
    /// A call is transferred to the AG and we are waiting for the HF to initiate a SCO connection.
    AwaitingRemote(ScoStateFuture<ScoConnection>),
    /// A call is active an dso is the SCO connection.
    Active(Vigil<ScoActive>),
}

impl ScoState {
    pub fn is_active(&self) -> bool {
        match self {
            Self::Active(_) => true,
            _ => false,
        }
    }

    pub fn on_connected<'a>(
        &'a mut self,
    ) -> impl Future<Output = Result<ScoConnection, ScoConnectError>> + FusedFuture + 'a {
        match self {
            Self::AwaitingRemote(ref mut fut) => fut.fuse(),
            _ => Fuse::terminated(),
        }
    }
}

impl Default for ScoState {
    fn default() -> Self {
        Self::Inactive
    }
}

impl fmt::Debug for ScoState {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            ScoState::Inactive => write!(f, "Inactive"),
            ScoState::SettingUp => write!(f, "SettingUp"),
            ScoState::TearingDown => write!(f, "TearingDown"),
            ScoState::AwaitingRemote(_) => write!(f, "AwaitingRemote"),
            ScoState::Active(active) => write!(f, "Active({:?})", active),
        }
    }
}

impl std::fmt::Display for ScoState {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let state = match &self {
            ScoState::Inactive => "Inactive",
            ScoState::SettingUp => "SettingUp",
            ScoState::TearingDown => "TearingDown",
            ScoState::AwaitingRemote(_) => "AwaitingRemote",
            ScoState::Active(_) => "Active",
        };
        write!(f, "{}", state)
    }
}

impl Unit for ScoState {
    type Data = inspect::Node;
    fn inspect_create(&self, parent: &inspect::Node, name: impl AsRef<str>) -> Self::Data {
        let mut node = parent.create_child(String::from(name.as_ref()));
        self.inspect_update(&mut node);
        node
    }

    fn inspect_update(&self, data: &mut Self::Data) {
        data.clear_recorded();
        data.record_string("status", &format!("{}", self));
        if let ScoState::Active(active) = &self {
            let node = active.inspect_create(data, "parameters");
            data.record(node);
        }
    }
}

pub type InspectableScoState = IValue<ScoState>;

#[cfg(test)]
mod tests {
    use super::*;

    use fidl_fuchsia_bluetooth_bredr as bredr;
    use fuchsia_inspect::assert_data_tree;
    use fuchsia_inspect_derive::WithInspect;

    #[fuchsia::test]
    async fn sco_state_inspect_tree() {
        let inspect = inspect::Inspector::new();

        let mut state =
            InspectableScoState::default().with_inspect(inspect.root(), "sco_connection").unwrap();
        // Default inspect tree.
        assert_data_tree!(inspect, root: {
            sco_connection: {
                status: "Inactive",
            }
        });

        state.iset(ScoState::SettingUp);
        assert_data_tree!(inspect, root: {
            sco_connection: {
                status: "SettingUp",
            }
        });

        state.iset(ScoState::AwaitingRemote(Box::pin(async { Err(ScoConnectError::ScoFailed) })));
        assert_data_tree!(inspect, root: {
            sco_connection: {
                status: "AwaitingRemote",
            }
        });

        let params = bredr::ScoConnectionParameters {
            parameter_set: Some(bredr::HfpParameterSet::CvsdD1),
            air_coding_format: Some(bredr::CodingFormat::Cvsd),
            air_frame_size: Some(60),
            io_bandwidth: Some(16000),
            io_coding_format: Some(bredr::CodingFormat::LinearPcm),
            io_frame_size: Some(16),
            io_pcm_data_format: Some(fidl_fuchsia_hardware_audio::SampleFormat::PcmSigned),
            io_pcm_sample_payload_msb_position: Some(1),
            path: Some(bredr::DataPath::Offload),
            ..bredr::ScoConnectionParameters::EMPTY
        };
        let (sco_proxy, _sco_stream) =
            fidl::endpoints::create_proxy_and_stream::<bredr::ScoConnectionMarker>()
                .expect("ScoConnection proxy and stream");
        let vigil = Vigil::new(ScoActive {
            sco_connection: ScoConnection::build(params, sco_proxy),
            _pause_token: None,
        });
        state.iset(ScoState::Active(vigil));
        assert_data_tree!(inspect, root: {
            sco_connection: {
                status: "Active",
                parameters: {
                    parameter_set: "CvsdD1",
                    air_coding_format: "Cvsd",
                    air_frame_size: 60u64,
                    io_bandwidth: 16000u64,
                    io_coding_format: "LinearPcm",
                    io_frame_size: 16u64,
                    io_pcm_data_format: "PcmSigned",
                    io_pcm_sample_payload_msb_position: 1u64,
                    path: "Offload",
                },
            }
        });

        state.iset(ScoState::TearingDown);
        assert_data_tree!(inspect, root: {
            sco_connection: {
                status: "TearingDown",
            }
        });
    }
}
