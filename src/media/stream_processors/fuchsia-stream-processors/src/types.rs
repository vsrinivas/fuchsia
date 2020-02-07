// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_table_validation::*;
use fuchsia_zircon as zx;

use fidl_fuchsia_media::{
    Packet, PacketHeader, StreamBufferConstraints, StreamBufferSettings, StreamOutputConstraints,
};

#[allow(unused)]
#[derive(ValidFidlTable, Copy, Clone, Debug, PartialEq)]
#[fidl_table_src(StreamBufferSettings)]
pub struct ValidStreamBufferSettings {
    pub buffer_lifetime_ordinal: u64,
    pub buffer_constraints_version_ordinal: u64,
    pub packet_count_for_server: u32,
    pub packet_count_for_client: u32,
    pub per_packet_buffer_bytes: u32,
    #[fidl_field_type(default = false)]
    pub single_buffer_mode: bool,
}

#[derive(ValidFidlTable)]
#[fidl_table_src(StreamBufferConstraints)]
#[fidl_table_validator(StreamBufferConstraintsValidator)]
pub struct ValidStreamBufferConstraints {
    pub buffer_constraints_version_ordinal: u64,
    pub default_settings: ValidStreamBufferSettings,
    pub per_packet_buffer_bytes_min: u32,
    pub per_packet_buffer_bytes_recommended: u32,
    pub per_packet_buffer_bytes_max: u32,
    pub packet_count_for_server_min: u32,
    pub packet_count_for_server_recommended: u32,
    pub packet_count_for_server_recommended_max: u32,
    pub packet_count_for_server_max: u32,
    pub packet_count_for_client_min: u32,
    pub packet_count_for_client_max: u32,
    pub single_buffer_mode_allowed: bool,
    #[fidl_field_type(default = false)]
    pub is_physically_contiguous_required: bool,
    #[fidl_field_type(optional)]
    pub very_temp_kludge_bti_handle: Option<zx::Handle>,
}

pub struct StreamBufferConstraintsValidator;

#[derive(Debug)]
pub enum StreamBufferConstraintsError {
    VersionOrdinalZero,
    SingleBufferMode,
    ConstraintsNoBtiHandleForPhysicalBuffers,
}

impl Validate<ValidStreamBufferConstraints> for StreamBufferConstraintsValidator {
    type Error = StreamBufferConstraintsError;
    fn validate(candidate: &ValidStreamBufferConstraints) -> std::result::Result<(), Self::Error> {
        if candidate.buffer_constraints_version_ordinal == 0 {
            // An ordinal of 0 in StreamBufferConstraints is not allowed.
            return Err(StreamBufferConstraintsError::VersionOrdinalZero);
        }

        if candidate.default_settings.single_buffer_mode {
            // StreamBufferConstraints should never suggest single buffer mode.
            return Err(StreamBufferConstraintsError::SingleBufferMode);
        }

        if candidate.is_physically_contiguous_required
            && candidate
                .very_temp_kludge_bti_handle
                .as_ref()
                .map(|h| h.is_invalid())
                .unwrap_or(true)
        {
            // The bti handle must be provided if the buffers need to be physically contiguous.
            return Err(StreamBufferConstraintsError::ConstraintsNoBtiHandleForPhysicalBuffers);
        }

        Ok(())
    }
}

#[derive(ValidFidlTable)]
#[fidl_table_src(StreamOutputConstraints)]
pub struct ValidStreamOutputConstraints {
    pub stream_lifetime_ordinal: u64,
    pub buffer_constraints_action_required: bool,
    pub buffer_constraints: ValidStreamBufferConstraints,
}

#[derive(ValidFidlTable, Clone, Copy, Debug, PartialEq)]
#[fidl_table_src(PacketHeader)]
pub struct ValidPacketHeader {
    pub buffer_lifetime_ordinal: u64,
    pub packet_index: u32,
}

#[derive(ValidFidlTable, Clone, Copy, Debug, PartialEq)]
#[fidl_table_src(Packet)]
pub struct ValidPacket {
    pub header: ValidPacketHeader,
    pub buffer_index: u32,
    pub stream_lifetime_ordinal: u64,
    pub start_offset: u32,
    pub valid_length_bytes: u32,
    #[fidl_field_type(optional)]
    pub timestamp_ish: Option<u64>,
    #[fidl_field_type(default = false)]
    pub start_access_unit: bool,
    #[fidl_field_type(default = false)]
    pub known_end_access_unit: bool,
}

#[cfg(test)]
mod test {

    use super::*;

    use std::convert::TryFrom;

    #[test]
    fn validate_stream_buffer_constraints() {
        let stream_buffer_settings = ValidStreamBufferSettings {
            buffer_lifetime_ordinal: 1,
            buffer_constraints_version_ordinal: 0,
            packet_count_for_server: 1,
            packet_count_for_client: 1,
            per_packet_buffer_bytes: 1,
            single_buffer_mode: false,
        };

        let invalid_version_ordinal = StreamBufferConstraints {
            buffer_constraints_version_ordinal: Some(0),
            default_settings: Some(stream_buffer_settings.into()),
            per_packet_buffer_bytes_min: Some(0),
            per_packet_buffer_bytes_recommended: Some(1),
            per_packet_buffer_bytes_max: Some(2),
            packet_count_for_server_min: Some(0),
            packet_count_for_server_recommended: Some(1),
            packet_count_for_server_recommended_max: Some(2),
            packet_count_for_server_max: Some(3),
            packet_count_for_client_min: Some(1),
            packet_count_for_client_max: Some(2),
            single_buffer_mode_allowed: Some(false),
            is_physically_contiguous_required: Some(false),
            very_temp_kludge_bti_handle: None,
        };

        let err = ValidStreamBufferConstraints::try_from(invalid_version_ordinal);

        assert!(err.is_err());

        let stream_buffer_settings_request_single = ValidStreamBufferSettings {
            buffer_lifetime_ordinal: 1,
            buffer_constraints_version_ordinal: 0,
            packet_count_for_server: 1,
            packet_count_for_client: 1,
            per_packet_buffer_bytes: 1,
            single_buffer_mode: true,
        };
        let invalid_single_buffer = StreamBufferConstraints {
            buffer_constraints_version_ordinal: Some(0),
            default_settings: Some(stream_buffer_settings_request_single.into()),
            per_packet_buffer_bytes_min: Some(0),
            per_packet_buffer_bytes_recommended: Some(1),
            per_packet_buffer_bytes_max: Some(2),
            packet_count_for_server_min: Some(0),
            packet_count_for_server_recommended: Some(1),
            packet_count_for_server_recommended_max: Some(2),
            packet_count_for_server_max: Some(3),
            packet_count_for_client_min: Some(1),
            packet_count_for_client_max: Some(2),
            single_buffer_mode_allowed: Some(false),
            is_physically_contiguous_required: Some(false),
            very_temp_kludge_bti_handle: None,
        };

        let err = ValidStreamBufferConstraints::try_from(invalid_single_buffer);

        assert!(err.is_err());

        let invalid_continuous = StreamBufferConstraints {
            buffer_constraints_version_ordinal: Some(0),
            default_settings: Some(stream_buffer_settings.into()),
            per_packet_buffer_bytes_min: Some(0),
            per_packet_buffer_bytes_recommended: Some(1),
            per_packet_buffer_bytes_max: Some(2),
            packet_count_for_server_min: Some(0),
            packet_count_for_server_recommended: Some(1),
            packet_count_for_server_recommended_max: Some(2),
            packet_count_for_server_max: Some(3),
            packet_count_for_client_min: Some(1),
            packet_count_for_client_max: Some(2),
            single_buffer_mode_allowed: Some(false),
            is_physically_contiguous_required: Some(true),
            very_temp_kludge_bti_handle: None,
        };

        let err = ValidStreamBufferConstraints::try_from(invalid_continuous);

        assert!(err.is_err());
    }
}
