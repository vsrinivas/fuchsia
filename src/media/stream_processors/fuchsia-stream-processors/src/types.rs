// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_table_validation::*;

use fidl_fuchsia_media::{
    Packet, PacketHeader, StreamBufferConstraints, StreamBufferSettings, StreamOutputConstraints,
};

#[derive(ValidFidlTable)]
#[fidl_table_src(StreamBufferConstraints)]
#[fidl_table_validator(StreamBufferConstraintsValidator)]
pub struct ValidStreamBufferConstraints {
    pub buffer_constraints_version_ordinal: u64,
    #[fidl_field_type(optional)]
    pub default_settings: Option<StreamBufferSettings>,
    #[fidl_field_type(optional)]
    pub per_packet_buffer_bytes_min: Option<u32>,
    #[fidl_field_type(optional)]
    pub per_packet_buffer_bytes_recommended: Option<u32>,
    #[fidl_field_type(optional)]
    pub per_packet_buffer_bytes_max: Option<u32>,
    #[fidl_field_type(optional)]
    pub packet_count_for_server_min: Option<u32>,
    #[fidl_field_type(optional)]
    pub packet_count_for_server_recommended: Option<u32>,
    #[fidl_field_type(optional)]
    pub packet_count_for_server_recommended_max: Option<u32>,
    #[fidl_field_type(optional)]
    pub packet_count_for_server_max: Option<u32>,
    #[fidl_field_type(optional)]
    pub packet_count_for_client_min: Option<u32>,
    #[fidl_field_type(optional)]
    pub packet_count_for_client_max: Option<u32>,
    #[fidl_field_type(optional)]
    pub single_buffer_mode_allowed: Option<bool>,
    #[fidl_field_type(optional)]
    pub is_physically_contiguous_required: Option<bool>,
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
        let invalid_version_ordinal = StreamBufferConstraints {
            buffer_constraints_version_ordinal: Some(0),
            ..StreamBufferConstraints::EMPTY
        };

        let err = ValidStreamBufferConstraints::try_from(invalid_version_ordinal);

        assert!(err.is_err());

        let invalid_single_buffer = StreamBufferConstraints {
            buffer_constraints_version_ordinal: Some(0),
            ..StreamBufferConstraints::EMPTY
        };

        let err = ValidStreamBufferConstraints::try_from(invalid_single_buffer);

        assert!(err.is_err());

        let invalid_continuous = StreamBufferConstraints {
            buffer_constraints_version_ordinal: Some(0),
            ..StreamBufferConstraints::EMPTY
        };

        let err = ValidStreamBufferConstraints::try_from(invalid_continuous);

        assert!(err.is_err());
    }
}
