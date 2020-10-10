// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Handles negotiating buffer sets with the codec server and sysmem.

use crate::{buffer_collection_constraints::*, Result};
use anyhow::Context as _;
use fidl::encoding::Decodable;
use fidl::endpoints::{create_endpoints, ClientEnd};
use fidl_fuchsia_media::*;
use fidl_fuchsia_sysmem::*;
use fuchsia_component::client;
use fuchsia_stream_processors::*;
use fuchsia_zircon as zx;
use std::{
    convert::TryFrom,
    fmt,
    iter::{IntoIterator, StepBy},
    ops::RangeFrom,
};
use thiserror::Error;

#[derive(Debug, Error)]
pub enum Error {
    ReclaimClientTokenChannel,
    ServerOmittedBufferVmo,
    PacketReferencesInvalidBuffer,
    VmoReadFail(zx::Status),
}

impl fmt::Display for Error {
    fn fmt(&self, w: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt::Debug::fmt(&self, w)
    }
}

/// The pattern to use when advancing ordinals.
#[derive(Debug, Clone, Copy)]
pub enum OrdinalPattern {
    /// Odd ordinal pattern starts at 1 and moves in increments of 2: [1,3,5..]
    Odd,
    /// All ordinal pattern starts at 1 and moves in increments of 1: [1,2,3..]
    All,
}

impl IntoIterator for OrdinalPattern {
    type Item = u64;
    type IntoIter = StepBy<RangeFrom<Self::Item>>;
    fn into_iter(self) -> Self::IntoIter {
        let (start, step) = match self {
            OrdinalPattern::Odd => (1, 2),
            OrdinalPattern::All => (1, 1),
        };
        (start..).step_by(step)
    }
}

pub fn get_ordinal(pattern: &mut <OrdinalPattern as IntoIterator>::IntoIter) -> u64 {
    pattern.next().expect("Getting next item in infinite pattern")
}

pub enum BufferSetType {
    Input,
    Output,
}

pub struct BufferSetFactory;

// This client only intends to be filling one input buffer or hashing one output buffer at any given
// time.
const MIN_BUFFER_COUNT_FOR_CAMPING: u32 = 1;

impl BufferSetFactory {
    pub async fn buffer_set(
        buffer_lifetime_ordinal: u64,
        constraints: ValidStreamBufferConstraints,
        codec: &mut StreamProcessorProxy,
        buffer_set_type: BufferSetType,
        buffer_collection_constraints: Option<BufferCollectionConstraints>,
    ) -> Result<BufferSet> {
        let (collection_client, settings) =
            Self::settings(buffer_lifetime_ordinal, constraints, buffer_collection_constraints)
                .await?;

        vlog!(2, "Got settings; waiting for buffers. {:?}", settings);

        match buffer_set_type {
            BufferSetType::Input => codec
                .set_input_buffer_partial_settings(settings)
                .context("Sending input partial settings to codec")?,
            BufferSetType::Output => codec
                .set_output_buffer_partial_settings(settings)
                .context("Sending output partial settings to codec")?,
        };

        let (status, collection_info) =
            collection_client.wait_for_buffers_allocated().await.context("Waiting for buffers")?;
        vlog!(2, "Sysmem responded: {:?}", status);
        let collection_info = zx::Status::ok(status).map(|_| collection_info)?;

        if let BufferSetType::Output = buffer_set_type {
            vlog!(2, "Completing settings for output.");
            codec.complete_output_buffer_partial_settings(buffer_lifetime_ordinal)?;
        }

        //collection_client.close()?;

        vlog!(
            2,
            "Got {} buffers of size {:?}",
            collection_info.buffer_count,
            collection_info.settings.buffer_settings.size_bytes
        );
        vlog!(3, "Buffer collection is: {:#?}", collection_info.settings);
        for (i, buffer) in collection_info.buffers.iter().enumerate() {
            // We enumerate beyond collection_info.buffer_count just for debugging
            // purposes at this log level.
            vlog!(3, "Buffer {} is : {:#?}", i, buffer);
        }

        Ok(BufferSet::try_from(BufferSetSpec {
            proxy: collection_client,
            buffer_lifetime_ordinal,
            collection_info,
        })?)
    }

    async fn settings(
        buffer_lifetime_ordinal: u64,
        constraints: ValidStreamBufferConstraints,
        buffer_collection_constraints: Option<BufferCollectionConstraints>,
    ) -> Result<(BufferCollectionProxy, StreamBufferPartialSettings)> {
        let (client_token, client_token_request) =
            create_endpoints::<BufferCollectionTokenMarker>()?;
        let (codec_token, codec_token_request) = create_endpoints::<BufferCollectionTokenMarker>()?;
        let client_token = client_token.into_proxy()?;

        let sysmem_client =
            client::connect_to_service::<AllocatorMarker>().context("Connecting to sysmem")?;

        sysmem_client
            .allocate_shared_collection(client_token_request)
            .context("Allocating shared collection")?;
        client_token.duplicate(std::u32::MAX, codec_token_request)?;

        let (collection_client, collection_request) = create_endpoints::<BufferCollectionMarker>()?;
        sysmem_client.bind_shared_collection(
            ClientEnd::new(
                client_token
                    .into_channel()
                    .map_err(|_| Error::ReclaimClientTokenChannel)?
                    .into_zx_channel(),
            ),
            collection_request,
        )?;
        let collection_client = collection_client.into_proxy()?;
        collection_client.sync().await.context("Syncing codec_token_request with sysmem")?;

        let mut collection_constraints =
            buffer_collection_constraints.unwrap_or(BUFFER_COLLECTION_CONSTRAINTS_DEFAULT);
        assert_eq!(
            collection_constraints.min_buffer_count_for_camping, 0,
            "min_buffer_count_for_camping should default to 0 before we've set it"
        );
        collection_constraints.min_buffer_count_for_camping = MIN_BUFFER_COUNT_FOR_CAMPING;

        vlog!(3, "Our buffer collection constraints are: {:#?}", collection_constraints);

        // By design we must say true even if all our fields are left at
        // default, or sysmem will not give us buffer handles.
        let has_constraints = true;
        collection_client
            .set_constraints(has_constraints, &mut collection_constraints)
            .context("Sending buffer constraints to sysmem")?;

        Ok((
            collection_client,
            StreamBufferPartialSettings {
                buffer_lifetime_ordinal: Some(buffer_lifetime_ordinal),
                buffer_constraints_version_ordinal: Some(
                    constraints.buffer_constraints_version_ordinal,
                ),
                sysmem_token: Some(codec_token),
                ..StreamBufferPartialSettings::new_empty()
            },
        ))
    }
}

struct BufferSetSpec {
    proxy: BufferCollectionProxy,
    buffer_lifetime_ordinal: u64,
    collection_info: BufferCollectionInfo2,
}

#[derive(Debug, PartialEq)]
pub struct Buffer {
    pub data: zx::Vmo,
    pub start: u64,
    pub size: u64,
}

#[derive(Debug)]
pub struct BufferSet {
    pub proxy: BufferCollectionProxy,
    pub buffers: Vec<Buffer>,
    pub buffer_lifetime_ordinal: u64,
    pub buffer_size: usize,
}

impl TryFrom<BufferSetSpec> for BufferSet {
    type Error = anyhow::Error;
    fn try_from(mut src: BufferSetSpec) -> std::result::Result<Self, Self::Error> {
        let mut buffers = vec![];
        for (i, buffer) in src.collection_info.buffers
            [0..(src.collection_info.buffer_count as usize)]
            .iter_mut()
            .enumerate()
        {
            buffers.push(Buffer {
                data: buffer.vmo.take().ok_or(Error::ServerOmittedBufferVmo).context(format!(
                    "Trying to ingest {}th buffer of {}: {:#?}",
                    i, src.collection_info.buffer_count, buffer
                ))?,
                start: buffer.vmo_usable_start,
                size: src.collection_info.settings.buffer_settings.size_bytes as u64,
            });
        }

        Ok(Self {
            proxy: src.proxy,
            buffers,
            buffer_lifetime_ordinal: src.buffer_lifetime_ordinal,
            buffer_size: src.collection_info.settings.buffer_settings.size_bytes as usize,
        })
    }
}

impl BufferSet {
    pub fn read_packet(&self, packet: &ValidPacket) -> Result<Vec<u8>> {
        let buffer = self
            .buffers
            .get(packet.buffer_index as usize)
            .ok_or(Error::PacketReferencesInvalidBuffer)?;
        let mut dest = vec![0; packet.valid_length_bytes as usize];
        buffer.data.read(&mut dest, packet.start_offset as u64).map_err(Error::VmoReadFail)?;
        Ok(dest)
    }
}
