// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    bitflags::bitflags,
    byteorder::NativeEndian,
    fuchsia_async as fasync,
    fuchsia_syslog::fx_log_info,
    fuchsia_zircon::{self as zx, sys},
    num_derive::FromPrimitive,
    num_traits::FromPrimitive,
    std::{convert::TryFrom, mem, sync::Arc},
    zerocopy::{
        byteorder::{U16, U32},
        AsBytes, FromBytes, LayoutVerified, Unaligned,
    },
};

use crate::types::{
    AudioCommandHeader, AudioSampleFormat, AudioStreamFormatRange, ChannelResponder, Decodable,
    Error, ResponseDirectable, Result,
};

const AUDIO_CMD_HEADER_LEN: usize = mem::size_of::<AudioCommandHeader>();

const GET_FORMATS_MAX_RANGES_PER_RESPONSE: usize = 15;

#[repr(C)]
#[derive(Default, AsBytes)]
struct GetFormatsResponse {
    pad: u32,
    format_range_count: u16,
    first_format_range_index: u16,
    format_ranges: [AudioStreamFormatRange; GET_FORMATS_MAX_RANGES_PER_RESPONSE],
}

#[repr(C)]
#[derive(FromBytes, Unaligned)]
struct SetFormatRequest {
    frames_per_second: U32<NativeEndian>,
    sample_format: U32<NativeEndian>,
    channels: U16<NativeEndian>,
}

#[repr(C)]
#[derive(AsBytes)]
struct SetFormatResponse {
    status: sys::zx_status_t,
    pad: u32,
    external_delay_nsec: u64,
}

#[repr(C)]
#[derive(AsBytes)]
struct GetGainResponse {
    cur_mute: bool,
    cur_agc: bool,
    pad1: u16,
    cur_gain: f32,
    can_mute: bool,
    can_agc: bool,
    pad2: u16,
    min_gain: f32,
    max_gain: f32,
    gain_step: f32,
}

bitflags! {
    #[repr(transparent)]
    #[derive(FromBytes)]
    struct SetGainFlags: u32 {
        const MUTE_VALID = 0x0000_0001;
        const AGC_VALID = 0x0000_0002;
        const GAIN_VALID = 0x0000_0004;
        const MUTE = 0x4000_0000;
        const AGC = 0x8000_0000;
    }
}

#[repr(C)]
#[derive(FromBytes)]
struct SetGainRequest {
    flags: SetGainFlags,
    gain: f32,
}

#[repr(C)]
#[derive(AsBytes)]
struct SetGainResponse {
    result: sys::zx_status_t,
    cur_mute: bool,
    cur_agc: bool,
    pad: u16,
    cur_gain: f32,
}

bitflags! {
    #[repr(transparent)]
    struct PlugDetectFlags: u32 {
        const ENABLE_NOTIFICATIONS = 0x4000_0000;
        const DISABLE_NOTIFICATIONS = 0x8000_0000;
    }
}

bitflags! {
    #[repr(transparent)]
    #[derive(AsBytes)]
    struct PlugDetectNotifyFlags: u32 {
        const HARDWIRED = 0x0000_0001;
        const CAN_NOTIFY = 0x0000_0002;
        const PLUGGED = 0x8000_0000;
    }
}

#[repr(C)]
#[derive(AsBytes)]
struct PlugDetectResponse {
    flags: PlugDetectNotifyFlags,
    pad: u32,
    plug_state_time: sys::zx_time_t,
}

#[allow(dead_code)]
type GetUniqueIdResponse = [u8; 16];

type GetStringRequest = U32<NativeEndian>;

#[repr(C)]
#[derive(AsBytes)]
struct GetStringResponse {
    status: sys::zx_status_t,
    id: u32,
    string_len: u32,
    string: [u8; 256 - AUDIO_CMD_HEADER_LEN - mem::size_of::<u32>() * 3],
}

impl GetStringResponse {
    fn build(id: StringId, s: &String) -> GetStringResponse {
        const MAX_STRING_LEN: usize = 256 - AUDIO_CMD_HEADER_LEN - mem::size_of::<u32>() * 3;
        let mut r = GetStringResponse {
            status: sys::ZX_OK,
            id: id as u32,
            string_len: s.len() as u32,
            string: [0; MAX_STRING_LEN],
        };
        let bytes = s.clone().into_bytes();
        if bytes.len() > MAX_STRING_LEN {
            r.string.copy_from_slice(&bytes[0..MAX_STRING_LEN]);
            r.string_len = MAX_STRING_LEN as u32;
        } else {
            r.string[0..bytes.len()].copy_from_slice(&bytes);
        }
        r
    }
}

#[derive(Clone, PartialEq, FromPrimitive, Debug)]
pub(crate) enum StringId {
    Manufacturer = 0x8000_0000,
    Product = 0x8000_0001,
}

impl TryFrom<u32> for StringId {
    type Error = Error;

    fn try_from(value: u32) -> Result<Self> {
        StringId::from_u32(value).ok_or(Error::OutOfRange)
    }
}

#[repr(C)]
#[derive(AsBytes)]
struct GetClockDomainResponse {
    clock_domain: i32,
}

#[derive(Clone, FromPrimitive, Debug)]
pub(crate) enum CommandType {
    GetFormats = 0x1000,
    SetFormat = 0x1001,
    GetGain = 0x1002,
    SetGain = 0x1003,
    PlugDetect = 0x1004,
    GetUniqueId = 0x1005,
    GetString = 0x1006,
    GetClockDomain = 0x1007,
}

const COMMAND_NO_ACK: &u32 = &0x8000_0000;

impl TryFrom<u32> for CommandType {
    type Error = Error;

    fn try_from(value: u32) -> Result<Self> {
        CommandType::from_u32(value).ok_or(Error::OutOfRange)
    }
}

impl From<CommandType> for u32 {
    fn from(v: CommandType) -> u32 {
        v as u32
    }
}

#[derive(Debug)]
pub(crate) enum Request {
    GetFormats {
        responder: GetFormatsResponder,
    },
    SetFormat {
        frames_per_second: u32,
        sample_format: AudioSampleFormat,
        channels: u16,
        responder: SetFormatResponder,
    },
    GetGain {
        responder: GetGainResponder,
    },
    SetGain {
        mute: Option<bool>,
        agc: Option<bool>,
        gain: Option<f32>,
        responder: SetGainResponder,
    },
    PlugDetect {
        notifications: bool,
        responder: PlugDetectResponder,
    },
    GetUniqueId {
        responder: GetUniqueIdResponder,
    },
    GetString {
        id: StringId,
        responder: GetStringResponder,
    },
    GetClockDomain {
        responder: GetClockDomainResponder,
    },
}

impl Decodable for Request {
    fn decode(bytes: &[u8]) -> Result<Request> {
        let (header, rest) =
            LayoutVerified::<_, AudioCommandHeader>::new_unaligned_from_prefix(bytes)
                .ok_or(Error::Encoding)?;
        let ack = (header.command_type.get() & COMMAND_NO_ACK) == 0;
        let cmd_type = CommandType::try_from(header.command_type.get() & !COMMAND_NO_ACK)?;
        let chan_responder = ChannelResponder::build(header.transaction_id.get(), &cmd_type);
        let res = match cmd_type {
            CommandType::GetFormats => {
                Request::GetFormats { responder: GetFormatsResponder(chan_responder) }
            }
            CommandType::SetFormat => {
                let (request, rest) =
                    LayoutVerified::<_, SetFormatRequest>::new_unaligned_from_prefix(rest)
                        .ok_or(Error::Encoding)?;
                if rest.len() > 0 {
                    fx_log_info!("{} extra bytes decoding SetFormatRequest, ignoring", rest.len());
                }
                Request::SetFormat {
                    responder: SetFormatResponder(chan_responder),
                    frames_per_second: request.frames_per_second.get(),
                    sample_format: AudioSampleFormat::try_from(request.sample_format.get())?,
                    channels: request.channels.get(),
                }
            }
            CommandType::GetGain => {
                Request::GetGain { responder: GetGainResponder(chan_responder) }
            }
            CommandType::SetGain => {
                let request =
                    LayoutVerified::<_, SetGainRequest>::new(rest).ok_or(Error::Encoding)?;
                Request::SetGain {
                    mute: if request.flags.contains(SetGainFlags::MUTE_VALID) {
                        Some(request.flags.contains(SetGainFlags::MUTE))
                    } else {
                        None
                    },
                    agc: if request.flags.contains(SetGainFlags::AGC_VALID) {
                        Some(request.flags.contains(SetGainFlags::AGC))
                    } else {
                        None
                    },
                    gain: if request.flags.contains(SetGainFlags::GAIN_VALID) {
                        Some(request.gain)
                    } else {
                        None
                    },
                    responder: SetGainResponder { inner: chan_responder, ack },
                }
            }
            CommandType::PlugDetect => {
                let flags_u32 =
                    LayoutVerified::<_, U32<NativeEndian>>::new(rest).ok_or(Error::Encoding)?;
                let request =
                    PlugDetectFlags::from_bits(flags_u32.get()).ok_or(Error::OutOfRange)?;
                Request::PlugDetect {
                    notifications: request.contains(PlugDetectFlags::ENABLE_NOTIFICATIONS),
                    responder: PlugDetectResponder { inner: chan_responder, ack },
                }
            }
            CommandType::GetUniqueId => {
                Request::GetUniqueId { responder: GetUniqueIdResponder(chan_responder) }
            }
            CommandType::GetString => {
                let request =
                    LayoutVerified::<_, GetStringRequest>::new(rest).ok_or(Error::Encoding)?;
                let id = StringId::try_from(request.get())?;
                Request::GetString {
                    id: id.clone(),
                    responder: GetStringResponder { inner: chan_responder, id },
                }
            }
            CommandType::GetClockDomain => {
                Request::GetClockDomain { responder: GetClockDomainResponder(chan_responder) }
            }
        };
        Ok(res)
    }
}

impl ResponseDirectable for Request {
    fn set_response_channel(&mut self, channel: Arc<fasync::Channel>) {
        match self {
            Request::GetFormats { responder } => responder.0.set_channel(channel),
            Request::SetFormat { responder, .. } => responder.0.set_channel(channel),
            Request::GetGain { responder } => responder.0.set_channel(channel),
            Request::SetGain { responder, .. } => responder.inner.set_channel(channel),
            Request::PlugDetect { responder, .. } => responder.inner.set_channel(channel),
            Request::GetUniqueId { responder } => responder.0.set_channel(channel),
            Request::GetString { responder, .. } => responder.inner.set_channel(channel),
            Request::GetClockDomain { responder } => responder.0.set_channel(channel),
        }
    }
}

#[derive(Debug)]
pub(crate) struct GetFormatsResponder(ChannelResponder);

impl GetFormatsResponder {
    pub fn reply(self, supported_formats: &[AudioStreamFormatRange]) -> Result<()> {
        // TODO: Merge any format ranges that are compatible?
        let total_formats = supported_formats.len();
        for (chunk_idx, ranges) in
            supported_formats.chunks(GET_FORMATS_MAX_RANGES_PER_RESPONSE).enumerate()
        {
            let first_index = chunk_idx * GET_FORMATS_MAX_RANGES_PER_RESPONSE;
            let mut resp = GetFormatsResponse {
                format_range_count: total_formats as u16,
                first_format_range_index: first_index as u16,
                ..Default::default()
            };
            resp.format_ranges[0..ranges.len()].clone_from_slice(ranges);
            self.0.send(&resp.as_bytes())?;
        }
        Ok(())
    }
}

#[derive(Debug)]
pub(crate) struct SetFormatResponder(ChannelResponder);

impl SetFormatResponder {
    pub fn reply(
        self,
        status: zx::Status,
        external_delay_nsec: u64,
        rb_channel: Option<zx::Channel>,
    ) -> Result<()> {
        let resp = SetFormatResponse { status: status.into_raw(), external_delay_nsec, pad: 0 };
        let handles: Vec<zx::Handle> = rb_channel.into_iter().map(Into::into).collect();
        self.0.send_with_handles(&resp.as_bytes(), handles)
    }
}

#[derive(Debug)]
pub(crate) struct GetGainResponder(ChannelResponder);

impl GetGainResponder {
    pub fn reply(
        self,
        mute: Option<bool>,
        agc: Option<bool>,
        gain: f32,
        gain_range: [f32; 2],
        gain_step: f32,
    ) -> Result<()> {
        let resp = GetGainResponse {
            can_mute: mute.is_some(),
            cur_mute: mute.unwrap_or(false),
            can_agc: agc.is_some(),
            cur_agc: agc.unwrap_or(false),
            cur_gain: gain,
            min_gain: gain_range[0],
            max_gain: gain_range[1],
            gain_step,
            pad1: 0,
            pad2: 0,
        };
        self.0.send(&resp.as_bytes())
    }
}

#[derive(Debug)]
pub(crate) struct SetGainResponder {
    inner: ChannelResponder,
    ack: bool,
}

impl SetGainResponder {
    #[allow(dead_code)]
    pub fn reply(
        self,
        result: zx::Status,
        cur_mute: bool,
        cur_agc: bool,
        cur_gain: f32,
    ) -> Result<()> {
        if !self.ack {
            return Ok(());
        }
        let resp =
            SetGainResponse { result: result.into_raw(), cur_mute, cur_agc, cur_gain, pad: 0 };
        self.inner.send(&resp.as_bytes())
    }
}

#[derive(Debug)]
pub(crate) struct PlugDetectResponder {
    inner: ChannelResponder,
    ack: bool,
}

pub enum PlugState {
    /// Hard wired output
    Hardwired,
    /// A Pluggable output:
    ///  - `can_notify` indicates if notificaitons can be sent
    ///  - `plugged` indicates whether the output is currently plugged in
    #[allow(dead_code)] // TODO: implement a way to notify and indicate plugged state
    Pluggable { can_notify: bool, plugged: bool },
}

impl From<PlugState> for PlugDetectNotifyFlags {
    fn from(state: PlugState) -> Self {
        match state {
            PlugState::Hardwired => PlugDetectNotifyFlags::PLUGGED,
            PlugState::Pluggable { can_notify, plugged } => {
                let mut flags = PlugDetectNotifyFlags::empty();
                if plugged {
                    flags.insert(PlugDetectNotifyFlags::PLUGGED)
                }
                if can_notify {
                    flags.insert(PlugDetectNotifyFlags::CAN_NOTIFY)
                }
                flags
            }
        }
    }
}

impl PlugDetectResponder {
    pub fn reply(self, plug_state: PlugState, time: zx::Time) -> Result<()> {
        if !self.ack {
            return Ok(());
        }
        let resp = PlugDetectResponse {
            flags: plug_state.into(),
            plug_state_time: time.into_nanos(),
            pad: 0,
        };
        self.inner.send(&resp.as_bytes())
    }
}

#[derive(Debug)]
pub(crate) struct GetUniqueIdResponder(ChannelResponder);

impl GetUniqueIdResponder {
    pub fn reply(self, id: &[u8; 16]) -> Result<()> {
        self.0.send(id)
    }
}

#[derive(Debug)]
pub(crate) struct GetStringResponder {
    id: StringId,
    inner: ChannelResponder,
}

impl GetStringResponder {
    pub fn reply(self, string: &String) -> Result<()> {
        let resp = GetStringResponse::build(self.id, string);
        self.inner.send(&resp.as_bytes())
    }
}

#[derive(Debug)]
pub(crate) struct GetClockDomainResponder(ChannelResponder);

impl GetClockDomainResponder {
    pub fn reply(self, clock_domain: i32) -> Result<()> {
        let resp = GetClockDomainResponse { clock_domain };
        self.0.send(&resp.as_bytes())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use fidl_fuchsia_media::{AudioChannelId, AudioPcmMode, PcmFormat};
    use fuchsia_async as fasync;
    use fuchsia_zircon::{self as zx, AsHandleRef};
    use std::convert::TryInto;

    fn make_pcmformat(frames_per_second: u32) -> PcmFormat {
        PcmFormat {
            pcm_mode: AudioPcmMode::Linear,
            bits_per_sample: 16,
            frames_per_second,
            channel_map: vec![AudioChannelId::Lf, AudioChannelId::Rf],
        }
    }

    // Expects `len` bytes to be received on `channel`, with the first byte equaling `matching`
    fn expect_channel_recv(channel: &zx::Channel, len: usize, matching: &[u8]) -> zx::MessageBuf {
        let mut buf = zx::MessageBuf::new();
        assert!(channel.read(&mut buf).is_ok());
        let sent = buf.bytes();

        assert_eq!(len, sent.len());

        assert_eq!(matching, &sent[0..matching.len()]);
        buf
    }

    fn setup_request_test() -> (fasync::Executor, zx::Channel, Arc<fasync::Channel>) {
        let exec = fasync::Executor::new().expect("failed to create an executor");
        let (remote, local) = zx::Channel::create().expect("can't make channels");
        let chan = Arc::new(fasync::Channel::from_channel(local).unwrap());
        (exec, remote, chan)
    }

    #[test]
    fn get_formats() {
        let (_, remote, chan) = setup_request_test();
        // 16 formats for testing multiple result messages.
        let ranges: Vec<AudioStreamFormatRange> = vec![
            make_pcmformat(44100),
            make_pcmformat(88200),
            make_pcmformat(22050),
            make_pcmformat(11025),
            make_pcmformat(48000),
            make_pcmformat(24000),
            make_pcmformat(12000),
            make_pcmformat(32000),
            make_pcmformat(16000),
            make_pcmformat(8000),
            make_pcmformat(37800),
            make_pcmformat(64000),
            make_pcmformat(96000),
            make_pcmformat(192000),
            make_pcmformat(4000),
            make_pcmformat(2000),
        ]
        .into_iter()
        .map(|x| x.try_into().unwrap())
        .collect();

        let request: &[u8] = &[
            0xF0, 0x0D, 0x00, 0x00, // transaction id
            0x00, 0x10, 0x00, 0x00, // get_formats
        ];

        let response_size = mem::size_of::<(AudioCommandHeader, GetFormatsResponse)>();

        let r = Request::decode(request);
        if let Ok(Request::GetFormats { mut responder }) = r {
            responder.0.set_channel(chan.clone());
            assert!(responder.reply(&ranges[0..1]).is_ok());
            let expected_response: &[u8] = &[
                0xF0, 0x0D, 0x00, 0x00, // transaction_id
                0x00, 0x10, 0x00, 0x00, // get_formats
                0x00, 0x00, 0x00, 0x00, // pad
                0x01, 0x00, // format_range_count
                0x00, 0x00, // first_format_range_index
                0x04, 0x00, 0x00, 0x00, // 0: sample_formats (16-bit PCM)
                0x44, 0xAC, 0x00, 0x00, // 0: min_frames_per_second (44100)
                0x44, 0xAC, 0x00, 0x00, // 0: max_frames_per_second (44100)
                0x02, 0x02, // 0: min_channels, max_channels (2)
                0x01, 0x00, // 0: flags (FPS_CONTINUOUS)
            ]; // rest of bytes are not mattering

            expect_channel_recv(&remote, response_size, expected_response);
        } else {
            panic!("expected GetFormats got {:?}", r);
        }

        let request: &[u8] = &[
            0xFE, 0xED, 0x00, 0x00, // transaction id
            0x00, 0x10, 0x00, 0x00, // get_formats
        ];

        let r = Request::decode(request);
        assert!(r.is_ok());
        if let Request::GetFormats { mut responder } = r.unwrap() {
            responder.0.set_channel(chan.clone());

            assert!(responder.reply(&ranges).is_ok());
            let expected_preamble: &[u8] = &[
                0xFE, 0xED, 0x00, 0x00, // transaction_id
                0x00, 0x10, 0x00, 0x00, // get_formats
                0x00, 0x00, 0x00, 0x00, // pad
                0x10, 0x00, // format_range_count - 16
                0x00, 0x00, // first_format_range_index
            ]; // Rest is 15 of the formats

            expect_channel_recv(&remote, response_size, expected_preamble);

            let expected_preamble: &[u8] = &[
                0xFE, 0xED, 0x00, 0x00, // transaction_id
                0x00, 0x10, 0x00, 0x00, // get_formats
                0x00, 0x00, 0x00, 0x00, // pad
                0x10, 0x00, // format_range_count - 16
                0x0F, 0x00, // first_format_range_index - 15
                0x04, 0x00, 0x00, 0x00, // 15: sample_formats (16-bit PCM)
                0xD0, 0x07, 0x00, 0x00, // 15: min_frames_per_second (2000)
                0xD0, 0x07, 0x00, 0x00, // 15: max_frames_per_second (2000)
                0x02, 0x02, // 15: min_channels, max_channels (2)
                0x01, 0x00, // 15: flags (FPS_CONTINUOUS)
            ]; // Rest doesn't matter

            expect_channel_recv(&remote, response_size, expected_preamble);
        } else {
            panic!("expected to decode to GetFormats");
        }
    }

    #[test]
    fn set_format() {
        let (_, remote, chan) = setup_request_test();

        let request: &[u8] = &[
            0xF0, 0x0D, 0x00, 0x00, // transaction id
            0x01, 0x10, 0x00, 0x00, // set_format
            0x44, 0xAC, 0x00, 0x00, // frames_per_second (44100)
            0x04, 0x00, 0x00, 0x00, // 16 bit PCM
            0x02, 0x00, // channels: 2
        ];

        let response_size = mem::size_of::<(AudioCommandHeader, SetFormatResponse)>();

        let r = Request::decode(request);
        assert!(r.is_ok(), "request didn't parse: {:?}", r);

        if let Ok(Request::SetFormat {
            frames_per_second,
            sample_format,
            channels,
            mut responder,
        }) = r
        {
            assert_eq!(44100, frames_per_second);
            assert_eq!(
                AudioSampleFormat::Sixteen { unsigned: false, invert_endian: false },
                sample_format
            );
            assert_eq!(2, channels);

            responder.0.set_channel(chan);

            let (there, _) = zx::Channel::create().expect("can't make channels");

            let handleid = &there.raw_handle();

            // 27 is a random test value.
            assert!(responder.reply(zx::Status::OK, 27, Some(there)).is_ok());

            let expected_bytes: &[u8] = &[
                0xF0, 0x0D, 0x00, 0x00, // transaction id
                0x01, 0x10, 0x00, 0x00, // set_format
                0x00, 0x00, 0x00, 0x00, // zx::status::ok
                0x00, 0x00, 0x00, 0x00, // padding
                0x1B, 0x00, 0x00, 0x00, // 27 nanosec external delay
                0x00, 0x00, 0x00, 0x00,
            ];

            let mut mb = expect_channel_recv(&remote, response_size, expected_bytes);

            assert_eq!(1, mb.n_handles());
            assert_eq!(handleid, &(mb.take_handle(0).unwrap()).raw_handle());
        } else {
            panic!("expected to decode to SetFormat: {:?}", r);
        }
    }

    #[test]
    fn get_gain() {
        let (_, remote, chan) = setup_request_test();

        let request: &[u8] = &[
            0xF0, 0x0D, 0x00, 0x00, // transaction id
            0x02, 0x10, 0x00, 0x00, // get_gain
        ];

        let response_size =
            mem::size_of::<AudioCommandHeader>() + mem::size_of::<GetGainResponse>();

        let r = Request::decode(request);
        if let Ok(Request::GetGain { mut responder }) = r {
            responder.0.set_channel(chan);
            assert!(responder.reply(None, Some(false), -6.0, [-10.0, 0.0], 0.5).is_ok());
            let expected_response: &[u8] = &[
                0xF0, 0x0D, 0x00, 0x00, // transaction_id
                0x02, 0x10, 0x00, 0x00, // get_gain
                0x00, // cur_mute: false
                0x00, // cur_agc: false
                0x00, 0x00, // 2x padding bytes that I don't care about
                0x00, 0x00, 0xc0, 0xc0, // -6.0 db current gain (in float 32-bit)
                0x00, // can_mute: false
                0x01, // can_agc: true
                0x00, 0x00, // 2x padding bytes that I don't care about
                0x00, 0x00, 0x20, 0xc1, // -10.00 min_gain
                0x00, 0x00, 0x00, 0x00, // 0.0 max_gain
                0x00, 0x00, 0x00, 0x3f, // 0.5 gain_step
            ];

            expect_channel_recv(&remote, response_size, expected_response);
        } else {
            panic!("decoding GetGain failed: {:?}", r);
        }
    }

    #[test]
    fn set_gain() {
        let (_, remote, chan) = setup_request_test();

        let request: &[u8] = &[
            0xF0, 0x0D, 0x00, 0x00, // transaction id
            0x03, 0x10, 0x00, 0x00, // set_gain
            0x05, 0x00, 0x00, 0x00, // flags: mute valid + gain valid + no mute
            0x00, 0x00, 0x20, 0xc1, // gain: -10.0
        ];

        let response_size =
            mem::size_of::<AudioCommandHeader>() + mem::size_of::<SetGainResponse>();

        let r = Request::decode(request);
        if let Ok(Request::SetGain { mute, agc, gain, mut responder }) = r {
            assert_eq!(None, agc);
            assert_eq!(Some(false), mute);
            assert_eq!(Some(-10.0), gain);

            responder.inner.set_channel(chan);

            assert!(responder.reply(zx::Status::OK, false, false, -10.0).is_ok());
            let expected_response: &[u8] = &[
                0xF0, 0x0D, 0x00, 0x00, // transaction_id
                0x03, 0x10, 0x00, 0x00, // set_gain
                0x00, 0x00, 0x00, 0x00, // status::OK
                0x00, // mute: false
                0x00, // agc: false
                0x00, 0x00, // padding bytes
                0x00, 0x00, 0x20, 0xc1, // -10.0 db current gain (in float 32-bit)
            ];

            expect_channel_recv(&remote, response_size, expected_response);
        } else {
            panic!("decoding SetGain failed: {:?}", r);
        }
    }

    #[test]
    fn plug_detect() {
        let (_, remote, chan) = setup_request_test();

        let request: &[u8] = &[
            0xF0, 0x0D, 0x00, 0x00, // transaction id
            0x04, 0x10, 0x00, 0x00, // plug_detect
            0x00, 0x00, 0x00, 0x40, // flags: enable notifications
        ];

        let response_size =
            mem::size_of::<AudioCommandHeader>() + mem::size_of::<PlugDetectResponse>();

        let r = Request::decode(request);
        if let Ok(Request::PlugDetect { notifications, mut responder }) = r {
            assert_eq!(true, notifications);

            responder.inner.set_channel(chan);

            let state = PlugState::Pluggable { can_notify: false, plugged: true };

            assert!(responder.reply(state, zx::Time::from_nanos(27)).is_ok());
            let expected_response: &[u8] = &[
                0xF0, 0x0D, 0x00, 0x00, // transaction_id
                0x04, 0x10, 0x00, 0x00, // plug_detect
                0x00, 0x00, 0x00, 0x80, // flags: can't notify + plugged
                0x00, 0x00, 0x00, 0x00, // pad to align
                0x1B, 0x00, 0x00, 0x00, // plugged at 27 nanosec
                0x00, 0x00, 0x00, 0x00,
            ];

            expect_channel_recv(&remote, response_size, expected_response);
        } else {
            panic!("decoding PlugDetect failed: {:?}", r);
        }
    }

    #[test]
    fn get_unique_id() {
        let (_, remote, chan) = setup_request_test();

        let request: &[u8] = &[
            0xF0, 0x0D, 0x00, 0x00, // transaction id
            0x05, 0x10, 0x00, 0x00, // get_unique_id
        ];

        let response_size = mem::size_of::<(AudioCommandHeader, GetUniqueIdResponse)>();

        let r = Request::decode(request);
        if let Ok(Request::GetUniqueId { mut responder }) = r {
            responder.0.set_channel(chan);

            assert!(responder
                .reply(&[
                    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C,
                    0x0D, 0x0E, 0x0F
                ])
                .is_ok());
            let expected_response: &[u8] = &[
                0xF0, 0x0D, 0x00, 0x00, // transaction_id
                0x05, 0x10, 0x00, 0x00, // get_unique_id
                0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D,
                0x0E, 0x0F, // (same id as above)
            ];

            expect_channel_recv(&remote, response_size, expected_response);
        } else {
            panic!("decoding GetUniqueId failed: {:?}", r);
        }
    }

    #[test]
    fn get_string() {
        let (_, remote, chan) = setup_request_test();

        let request: &[u8] = &[
            0xF0, 0x0D, 0x00, 0x00, // transaction id
            0x06, 0x10, 0x00, 0x00, // get_string
            0x01, 0x00, 0x00, 0x80, // string_id: product
        ];

        let response_size = mem::size_of::<(AudioCommandHeader, GetStringResponse)>();

        let r = Request::decode(request);
        if let Ok(Request::GetString { id, mut responder }) = r {
            assert_eq!(StringId::Product, id);

            responder.inner.set_channel(chan);

            assert!(responder.reply(&"Fuchsia💖".to_string()).is_ok());
            let expected_response: &[u8] = &[
                0xF0, 0x0D, 0x00, 0x00, // transaction_id
                0x06, 0x10, 0x00, 0x00, // get_string_id
                0x00, 0x00, 0x00, 0x00, // status: OK
                0x01, 0x00, 0x00, 0x80, // string_id: product
                0x0B, 0x00, 0x00, 0x00, // string_len: 11
                0x46, 0x75, 0x63, 0x68, 0x73, 0x69, 0x61, 0xF0, 0x9F, 0x92,
                0x96, // String: Fuchsia💖
            ];

            expect_channel_recv(&remote, response_size, expected_response);
        } else {
            panic!("decoding GetString failed: {:?}", r);
        }
    }

    #[test]
    fn get_clock_domain() {
        let (_, remote, chan) = setup_request_test();

        let request: &[u8] = &[
            0xF0, 0x0D, 0x00, 0x00, // transaction id
            0x07, 0x10, 0x00, 0x00, // get_clock_domain
        ];

        let response_size =
            mem::size_of::<AudioCommandHeader>() + mem::size_of::<GetClockDomainResponse>();

        let r: Request = Request::decode(request).expect("decoding GetClockDomain failed");

        let mut responder = match r {
            Request::GetClockDomain { responder } => responder,
            _ => panic!("Incorrect request for GetClockDomain: {:?}", r),
        };

        responder.0.set_channel(chan);
        assert!(responder.reply(0).is_ok());
        let expected_response: &[u8] = &[
            0xF0, 0x0D, 0x00, 0x00, // transaction_id
            0x07, 0x10, 0x00, 0x00, // get_clock_domain
            0x00, 0x00, 0x00, 0x00, // CLOCK_DOMAIN_MONOTONIC (== 0)
        ];
        expect_channel_recv(&remote, response_size, expected_response);
    }
}
