// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Keep all consts and type defs for completeness.
#![allow(dead_code)]

use {
    iota::iota,
    zerocopy::{AsBytes, FromBytes},
};

pub use zerocopy::byteorder::little_endian::{U32 as LE32, U64 as LE64};

// The following structures are adapted from:
// https://www.kraxel.org/virtio/virtio-v1.1-cs01-sound-v8.html#x1-49500014
//
// Naming conventions:
//
// - Where constants are given a name in the spec, they have the same name
//   here (VIRTIO_SND_*).
//
// - Each struct defined in the spec has the same name here, with snake_case
//   converted to CamelCase. For example: virtio_snd_cfg -> VirtioSndCfg.
//
// - We use type aliases to provide each control message with explicitly
//   named request and response type. For example: JackInfo{Request,Response}.
//
// All struct fields use integers, even fields that are logically enums, so
// that each struct can derive AsBytes, FromBytes (we can't derive AsBytes, FromBytes from an enum
// field unless the enum covers all possible bit patterns, which isn't true
// of any the enums below).

//
// 5.14.2 Virtqueues
//

pub const CONTROLQ: u16 = 0;
pub const EVENTQ: u16 = 1;
pub const TXQ: u16 = 2;
pub const RXQ: u16 = 3;

//
// 5.14.4 Device Configuration Layout
//

#[derive(Debug, Copy, Clone, AsBytes, FromBytes)]
#[repr(C, packed)]
pub struct VirtioSndConfig {
    pub jacks: LE32,
    pub streams: LE32,
    pub chmaps: LE32,
}

//
// 5.14.6 Device Operation
//

// Jack control request types
iota! {
    pub const VIRTIO_SND_R_JACK_INFO: u32 = 1 + iota;
        , VIRTIO_SND_R_JACK_REMAP
}

// PCM control request types
iota! {
    pub const VIRTIO_SND_R_PCM_INFO: u32 = 0x0100 + iota;
        , VIRTIO_SND_R_PCM_SET_PARAMS
        , VIRTIO_SND_R_PCM_PREPARE
        , VIRTIO_SND_R_PCM_RELEASE
        , VIRTIO_SND_R_PCM_START
        , VIRTIO_SND_R_PCM_STOP
}

// Channel map control request types
iota! {
    pub const VIRTIO_SND_R_CHMAP_INFO: u32 = 0x0200 + iota;
}

// Jack event types
iota! {
    pub const VIRTIO_SND_EVT_JACK_CONNECTED: u32 = 0x1000 + iota;
        , VIRTIO_SND_EVT_JACK_DISCONNECTED
}

// PCM event types
iota! {
    pub const VIRTIO_SND_EVT_PCM_PERIOD_ELAPSED: u32 = 0x1100 + iota;
        , VIRTIO_SND_EVT_PCM_XRUN
}

// Common status codes
iota! {
    pub const VIRTIO_SND_S_OK: u32 = 0x8000 + iota;
        , VIRTIO_SND_S_BAD_MSG
        , VIRTIO_SND_S_NOT_SUPP
        , VIRTIO_SND_S_IO_ERR
}

// Data flow directions
iota! {
    pub const VIRTIO_SND_D_OUTPUT: u8 = iota;
        , VIRTIO_SND_D_INPUT
}

// A common header
#[derive(Debug, Copy, Clone, AsBytes, FromBytes)]
#[repr(C, packed)]
pub struct VirtioSndHdr {
    pub code: LE32,
}

// An event notification
#[derive(Debug, Copy, Clone, AsBytes, FromBytes)]
#[repr(C, packed)]
pub struct VirtioSndEvent {
    pub hdr: VirtioSndHdr, // .code = VIRTIO_SND_EVT_*
    pub data: LE32,
}

pub type GenericRequest = VirtioSndHdr; // .code = VIRTIO_SND_R_*
pub type GenericResponse = VirtioSndHdr; // .code = VIRTIO_SND_S_*

//
// 5.14.6.1 Item Information Request
//

#[derive(Debug, Copy, Clone, AsBytes, FromBytes)]
#[repr(C, packed)]
pub struct VirtioSndQueryInfo {
    pub hdr: VirtioSndHdr, // .code = VIRTIO_SND_R_*_INFO
    pub start_id: LE32,
    pub count: LE32,
    pub size: LE32,
}

#[derive(Debug, Copy, Clone, AsBytes, FromBytes)]
#[repr(C, packed)]
pub struct VirtioSndInfo {
    pub hda_fn_nid: LE32,
}

pub type GenericInfoRequest = VirtioSndQueryInfo;
pub type GenericInfoResponse = VirtioSndInfo;

//
// 5.14.6.4 Jack Control Messages
//

#[derive(Debug, Copy, Clone, AsBytes, FromBytes)]
#[repr(C, packed)]
pub struct VirtioSndJackHdr {
    pub hdr: VirtioSndHdr,
    pub jack_id: LE32,
}

//
// 5.14.6.4.1 VIRTIO_SND_R_JACK_INFO
//

// Supported jack features
iota! {
    pub const VIRTIO_SND_JACK_F_REMAP: u32 = iota;
}

#[derive(Debug, Copy, Clone, AsBytes, FromBytes)]
#[repr(C, packed)]
pub struct VirtioSndJackInfo {
    pub hdr: VirtioSndInfo,
    pub features: LE32, // 1 << VIRTIO_SND_JACK_F_*
    pub hda_reg_defconf: LE32,
    pub hda_reg_caps: LE32,
    pub connected: u8,

    pub padding: [u8; 7],
}

pub type JackInfoRequest = GenericInfoRequest;
pub type JackInfoResponse = VirtioSndJackInfo;

//
// 5.14.6.4.2 VIRTIO_SND_R_JACK_REMAP
//

#[derive(Debug, Copy, Clone, AsBytes, FromBytes)]
#[repr(C, packed)]
pub struct VirtioSndJackRemap {
    pub hdr: VirtioSndJackHdr, // .code = VIRTIO_SND_R_JACK_REMAP
    pub association: LE32,
    pub sequence: LE32,
}

pub type JackRemapRequest = VirtioSndJackRemap;
pub type JackRemapResponse = GenericResponse;

//
// 5.14.6.6 PCM Control Messages
//

#[derive(Debug, Copy, Clone, AsBytes, FromBytes)]
#[repr(C, packed)]
pub struct VirtioSndPcmHdr {
    pub hdr: VirtioSndHdr,
    pub stream_id: LE32,
}

//
// 5.14.6.6.2 VIRTIO_SND_R_PCM_INFO
//

// Supported PCM stream features
iota! {
    pub const VIRTIO_SND_PCM_F_SHMEM_HOST: u32 = iota;
        , VIRTIO_SND_PCM_F_SHMEM_GUEST
        , VIRTIO_SND_PCM_F_MSG_POLLING
        , VIRTIO_SND_PCM_F_EVT_SHMEM_PERIODS
        , VIRTIO_SND_PCM_F_EVT_XRUNS
}

// Supported PCM sample formats
iota! {
    // Analog formats (width / physical width)
    pub const VIRTIO_SND_PCM_FMT_IMA_ADPCM: u8 = iota;  //  4 /  4 bits
        , VIRTIO_SND_PCM_FMT_MU_LAW                     //  8 /  8 bits
        , VIRTIO_SND_PCM_FMT_A_LAW                      //  8 /  8 bits
        , VIRTIO_SND_PCM_FMT_S8                         //  8 /  8 bits
        , VIRTIO_SND_PCM_FMT_U8                         //  8 /  8 bits
        , VIRTIO_SND_PCM_FMT_S16                        // 16 / 16 bits
        , VIRTIO_SND_PCM_FMT_U16                        // 16 / 16 bits
        , VIRTIO_SND_PCM_FMT_S18_3                      // 18 / 24 bits
        , VIRTIO_SND_PCM_FMT_U18_3                      // 18 / 24 bits
        , VIRTIO_SND_PCM_FMT_S20_3                      // 20 / 24 bits
        , VIRTIO_SND_PCM_FMT_U20_3                      // 20 / 24 bits
        , VIRTIO_SND_PCM_FMT_S24_3                      // 24 / 24 bits
        , VIRTIO_SND_PCM_FMT_U24_3                      // 24 / 24 bits
        , VIRTIO_SND_PCM_FMT_S20                        // 20 / 32 bits
        , VIRTIO_SND_PCM_FMT_U20                        // 20 / 32 bits
        , VIRTIO_SND_PCM_FMT_S24                        // 24 / 32 bits
        , VIRTIO_SND_PCM_FMT_U24                        // 24 / 32 bits
        , VIRTIO_SND_PCM_FMT_S32                        // 32 / 32 bits
        , VIRTIO_SND_PCM_FMT_U32                        // 32 / 32 bits
        , VIRTIO_SND_PCM_FMT_FLOAT                      // 32 / 32 bits
        , VIRTIO_SND_PCM_FMT_FLOAT64                    // 64 / 64 bits

    // Digital formats (width / physical width)
        , VIRTIO_SND_PCM_FMT_DSD_U8                     //  8 /  8 bits
        , VIRTIO_SND_PCM_FMT_DSD_U16                    // 16 / 16 bits
        , VIRTIO_SND_PCM_FMT_DSD_U32                    // 32 / 32 bits
        , VIRTIO_SND_PCM_FMT_IEC958_SUBFRAME            // 32 / 32 bits
}

// Supported PCM frame rates
iota! {
    pub const VIRTIO_SND_PCM_RATE_5512: u8 = iota;
        , VIRTIO_SND_PCM_RATE_8000
        , VIRTIO_SND_PCM_RATE_11025
        , VIRTIO_SND_PCM_RATE_16000
        , VIRTIO_SND_PCM_RATE_22050
        , VIRTIO_SND_PCM_RATE_32000
        , VIRTIO_SND_PCM_RATE_44100
        , VIRTIO_SND_PCM_RATE_48000
        , VIRTIO_SND_PCM_RATE_64000
        , VIRTIO_SND_PCM_RATE_88200
        , VIRTIO_SND_PCM_RATE_96000
        , VIRTIO_SND_PCM_RATE_176400
        , VIRTIO_SND_PCM_RATE_192000
        , VIRTIO_SND_PCM_RATE_384000
}

#[derive(Debug, Copy, Clone, AsBytes, FromBytes)]
#[repr(C, packed)]
pub struct VirtioSndPcmInfo {
    pub hdr: VirtioSndInfo,
    pub features: LE32, // 1 << VIRTIO_SND_PCM_F_*
    pub formats: LE64,  // 1 << VIRTIO_SND_PCM_FMT_*
    pub rates: LE64,    // 1 << VIRTIO_SND_PCM_RATE_*
    pub direction: u8,
    pub channels_min: u8,
    pub channels_max: u8,

    pub padding: [u8; 5],
}

pub type PcmInfoRequest = GenericInfoRequest;
pub type PcmInfoResponse = VirtioSndPcmInfo;

//
// 5.14.6.6.3 VIRTIO_SND_R_PCM_SET_PARAMS
//

#[derive(Debug, Copy, Clone, AsBytes, FromBytes)]
#[repr(C, packed)]
pub struct VirtioSndPcmSetParams {
    pub hdr: VirtioSndPcmHdr, // .hdr.code = VIRTIO_SND_R_PCM_SET_PARAMS
    pub buffer_bytes: LE32,
    pub period_bytes: LE32,
    pub features: LE32, // 1 << VIRTIO_SND_PCM_F_*
    pub channels: u8,
    pub format: u8,
    pub rate: u8,

    pub padding: u8,
}

pub type PcmSetParamsRequest = VirtioSndPcmSetParams;
pub type PcmSetParamsResponse = GenericResponse;

//
// 5.14.6.6.4 VIRTIO_SND_R_PCM_PREPARE
// 5.14.6.6.5 VIRTIO_SND_R_PCM_RELEASE
// 5.14.6.6.6 VIRTIO_SND_R_PCM_START
// 5.14.6.6.7 VIRTIO_SND_R_PCM_STOP
//

pub type PcmPrepareRequest = VirtioSndPcmHdr;
pub type PcmPrepareResponse = GenericResponse;

pub type PcmReleaseRequest = VirtioSndPcmHdr;
pub type PcmReleaseResponse = GenericResponse;

pub type PcmStartRequest = VirtioSndPcmHdr;
pub type PcmStartResponse = GenericResponse;

pub type PcmStopRequest = VirtioSndPcmHdr;
pub type PcmStopResponse = GenericResponse;

//
// 5.14.6.8 PCM I/O Messages
//

// Header for an I/O message.
#[derive(Debug, Copy, Clone, AsBytes, FromBytes)]
#[repr(C, packed)]
pub struct VirtioSndPcmXfer {
    pub stream_id: LE32,
}

// Status of an I/O message.
#[derive(Debug, Copy, Clone, AsBytes, FromBytes)]
#[repr(C, packed)]
pub struct VirtioSndPcmStatus {
    pub status: LE32,
    pub latency_bytes: LE32,
}

//
// 5.14.6.9.1 VIRTIO_SND_R_CHMAP_INFO
//

// Standard channel position definition
iota! {
    pub const VIRTIO_SND_CHMAP_NONE: u8 = iota;  //  undefined
        , VIRTIO_SND_CHMAP_NA                    //  silent
        , VIRTIO_SND_CHMAP_MONO                  //  mono stream
        , VIRTIO_SND_CHMAP_FL                    //  front left
        , VIRTIO_SND_CHMAP_FR                    //  front right
        , VIRTIO_SND_CHMAP_RL                    //  rear left
        , VIRTIO_SND_CHMAP_RR                    //  rear right
        , VIRTIO_SND_CHMAP_FC                    //  front center
        , VIRTIO_SND_CHMAP_LFE                   //  low frequency (LFE)
        , VIRTIO_SND_CHMAP_SL                    //  side left
        , VIRTIO_SND_CHMAP_SR                    //  side right
        , VIRTIO_SND_CHMAP_RC                    //  rear center
        , VIRTIO_SND_CHMAP_FLC                   //  front left center
        , VIRTIO_SND_CHMAP_FRC                   //  front right center
        , VIRTIO_SND_CHMAP_RLC                   //  rear left center
        , VIRTIO_SND_CHMAP_RRC                   //  rear right center
        , VIRTIO_SND_CHMAP_FLW                   //  front left wide
        , VIRTIO_SND_CHMAP_FRW                   //  front right wide
        , VIRTIO_SND_CHMAP_FLH                   //  front left high
        , VIRTIO_SND_CHMAP_FCH                   //  front center high
        , VIRTIO_SND_CHMAP_FRH                   //  front right high
        , VIRTIO_SND_CHMAP_TC                    //  top center
        , VIRTIO_SND_CHMAP_TFL                   //  top front left
        , VIRTIO_SND_CHMAP_TFR                   //  top front right
        , VIRTIO_SND_CHMAP_TFC                   //  top front center
        , VIRTIO_SND_CHMAP_TRL                   //  top rear left
        , VIRTIO_SND_CHMAP_TRR                   //  top rear right
        , VIRTIO_SND_CHMAP_TRC                   //  top rear center
        , VIRTIO_SND_CHMAP_TFLC                  //  top front left center
        , VIRTIO_SND_CHMAP_TFRC                  //  top front right center
        , VIRTIO_SND_CHMAP_TSL                   //  top side left
        , VIRTIO_SND_CHMAP_TSR                   //  top side right
        , VIRTIO_SND_CHMAP_LLFE                  //  left LFE
        , VIRTIO_SND_CHMAP_RLFE                  //  right LFE
        , VIRTIO_SND_CHMAP_BC                    //  bottom center
        , VIRTIO_SND_CHMAP_BLC                   //  bottom left center
        , VIRTIO_SND_CHMAP_BRC                   //  bottom right center
}

// Maximum possible number of channels
pub const VIRTIO_SND_CHMAP_MAX_SIZE: usize = 18;

#[derive(Debug, Copy, Clone, AsBytes, FromBytes)]
#[repr(C, packed)]
pub struct VirtioSndChmapInfo {
    pub hdr: VirtioSndInfo,
    pub direction: u8,
    pub channels: u8,
    pub positions: [u8; VIRTIO_SND_CHMAP_MAX_SIZE],
}

pub type ChmapInfoRequest = GenericInfoRequest;
pub type ChmapInfoResponse = VirtioSndChmapInfo;
