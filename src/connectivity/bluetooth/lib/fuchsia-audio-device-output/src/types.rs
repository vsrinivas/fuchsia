// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    bitflags::bitflags,
    byteorder::NativeEndian,
    fuchsia_async as fasync,
    fuchsia_zircon::{self as zx, MessageBuf},
    futures::{
        ready,
        stream::{FusedStream, Stream},
        task::{Context, Poll, Waker},
    },
    parking_lot::Mutex,
    std::{collections::VecDeque, convert::TryFrom, pin::Pin, result, sync::Arc},
    thiserror::Error,
    zerocopy::{AsBytes, FromBytes, Unaligned, U32},
};

/// Result type alias for brevity.
pub type Result<T> = result::Result<T, Error>;

/// The Error type of the fuchsia-media library
#[derive(Error, Debug, PartialEq)]
pub enum Error {
    /// The value that was received was out of range
    #[error("Value was out of range")]
    OutOfRange,

    /// The header was invalid when parsing a message.
    #[error("Invalid Header for a message")]
    InvalidHeader,

    /// Can't encode into a buffer
    #[error("Encoding error")]
    Encoding,

    /// Encountered an IO error reading
    #[error("Encountered an IO error reading from the channel: {}", _0)]
    PeerRead(zx::Status),

    /// Encountered an IO error writing
    #[error("Encountered an IO error writing to the channel: {}", _0)]
    PeerWrite(zx::Status),

    /// Other IO Error
    #[error("Encountered an IO error: {}", _0)]
    IOError(zx::Status),

    /// Action tried in an invalid state
    #[error("Tried to do an action in an invalid state")]
    InvalidState,

    /// Responder doesn't have a channel
    #[error("No channel found for reply")]
    NoChannel,

    /// When a message hasn't been implemented yet, the parser will return this.
    #[error("Message has not been implemented yet")]
    UnimplementedMessage,

    #[doc(hidden)]
    #[error("__Nonexhaustive error should never be created.")]
    __Nonexhaustive,
}

/// A decodable type can be created from a byte buffer.
/// The type returned is separate (copied) from the buffer once decoded.
pub(crate) trait Decodable: Sized {
    /// Decodes into a new object, or returns an error.
    fn decode(buf: &[u8]) -> Result<Self>;
}

/// A encodable type can write itself into a byte buffer.
pub(crate) trait Encodable: Sized {
    /// Returns the number of bytes necessary to encode |self|
    fn encoded_len(&self) -> usize;

    /// Writes the encoded version of |self| at the start of |buf|
    /// |buf| must be at least size() length.
    fn encode(&self, buf: &mut [u8]) -> Result<()>;
}

pub(crate) struct MaybeStream<T: Stream>(Option<T>);

impl<T: Stream + Unpin> MaybeStream<T> {
    pub(crate) fn set(&mut self, stream: T) {
        self.0 = Some(stream)
    }

    fn poll_next(&mut self, cx: &mut Context<'_>) -> Poll<Option<T::Item>> {
        Pin::new(self.0.as_mut().unwrap()).poll_next(cx)
    }
}

impl<T: Stream> Default for MaybeStream<T> {
    fn default() -> Self {
        MaybeStream(None)
    }
}

impl<T: Stream + Unpin> Stream for MaybeStream<T> {
    type Item = T::Item;

    fn poll_next(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        if self.0.is_none() {
            return Poll::Pending;
        }
        self.get_mut().poll_next(cx)
    }
}

impl<T: FusedStream + Stream + Unpin> FusedStream for MaybeStream<T> {
    fn is_terminated(&self) -> bool {
        if self.0.is_none() {
            false
        } else {
            self.0.as_ref().unwrap().is_terminated()
        }
    }
}

#[derive(Debug)]
struct RequestQueue<T> {
    listener: RequestListener,
    queue: VecDeque<T>,
}

impl<T> Default for RequestQueue<T> {
    fn default() -> Self {
        RequestQueue { listener: RequestListener::default(), queue: VecDeque::<T>::new() }
    }
}

#[derive(Debug)]
enum RequestListener {
    /// No one is listening.
    None,
    /// Someone wants to listen but hasn't polled.
    New,
    /// Someone is listening, and can be woken whith the waker.
    Some(Waker),
}

impl Default for RequestListener {
    fn default() -> Self {
        RequestListener::None
    }
}

#[derive(Debug)]
pub(crate) struct ChannelInner<T: Decodable> {
    /// The Channel
    channel: Arc<fasync::Channel>,

    /// A request queue for the channel
    queue: Mutex<RequestQueue<T>>,
}

pub trait ResponseDirectable {
    /// Directs any response to be sent through `channel`
    fn set_response_channel(&mut self, channel: Arc<fasync::Channel>);
}

impl<T: Decodable> ChannelInner<T> {
    pub(crate) fn new(channel: zx::Channel) -> Result<ChannelInner<T>> {
        let chan = fasync::Channel::from_channel(channel)
            .or_else(|_| Err(Error::IOError(zx::Status::IO)))?;
        Ok(ChannelInner { channel: Arc::new(chan), queue: Mutex::<RequestQueue<T>>::default() })
    }

    pub(crate) fn take_request_stream(s: Arc<Self>) -> RequestStream<T> {
        {
            let mut lock = s.queue.lock();
            if let RequestListener::None = lock.listener {
                lock.listener = RequestListener::New;
            } else {
                panic!("Request stream has already been taken");
            }
        }

        RequestStream { inner: s }
    }

    // Attempts to receive a new request by processing all packets on the socket.
    // Resolves to a request T if one was received or an error if there was
    // an error reading from the socket.
    fn poll_recv_request(&self, cx: &mut Context<'_>) -> Poll<Result<T>>
    where
        T: Decodable,
    {
        let _ = self.recv_all(cx)?;

        let mut lock = self.queue.lock();
        if let Some(request) = lock.queue.pop_front() {
            Poll::Ready(Ok(request))
        } else {
            lock.listener = RequestListener::Some(cx.waker().clone());
            Poll::Pending
        }
    }

    fn recv_all(&self, cx: &mut Context<'_>) -> Result<()>
    where
        T: Decodable,
    {
        let mut buf = MessageBuf::new();
        loop {
            match self.channel.recv_from(cx, &mut buf) {
                Poll::Ready(Err(e)) => return Err(Error::PeerRead(e)),
                Poll::Pending => return Ok(()),
                Poll::Ready(Ok(())) => (),
            };
            let request = T::decode(buf.bytes())?;
            let mut lock = self.queue.lock();
            lock.queue.push_back(request);
        }
    }
}

pub(crate) struct RequestStream<T: Decodable> {
    inner: Arc<ChannelInner<T>>,
}

impl<T: Decodable> Unpin for RequestStream<T> {}

impl<T: Decodable> Drop for RequestStream<T> {
    fn drop(&mut self) {
        self.inner.queue.lock().listener = RequestListener::None;
    }
}

impl<T: Decodable + Unpin + ResponseDirectable> FusedStream for RequestStream<T> {
    fn is_terminated(&self) -> bool {
        false
    }
}

impl<T: ResponseDirectable + Decodable> Stream for RequestStream<T> {
    type Item = Result<T>;

    fn poll_next(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        Poll::Ready(match ready!(self.inner.poll_recv_request(cx)) {
            Ok(mut x) => {
                x.set_response_channel(self.inner.channel.clone());
                Some(Ok(x))
            }
            Err(e) => Some(Err(e)),
        })
    }
}

#[derive(Debug)]
pub(crate) struct ChannelResponder {
    channel: Option<Arc<fasync::Channel>>,
    transaction_id: u32,
    command_type: u32,
}

impl ChannelResponder {
    pub fn build<U>(transaction_id: u32, command_type: &U) -> ChannelResponder
    where
        U: Into<u32> + Clone,
    {
        ChannelResponder {
            transaction_id,
            command_type: command_type.clone().into(),
            channel: None,
        }
    }

    pub fn set_channel(&mut self, channel: Arc<fasync::Channel>) {
        self.channel = Some(channel);
    }

    fn make_header(&self) -> AudioCommandHeader {
        AudioCommandHeader {
            transaction_id: U32::new(self.transaction_id),
            command_type: U32::new(self.command_type),
        }
    }

    pub fn send(&self, payload: &[u8]) -> Result<()> {
        self.send_with_handles(payload, vec![])
    }

    pub fn send_with_handles(&self, payload: &[u8], mut handles: Vec<zx::Handle>) -> Result<()> {
        if self.channel.is_none() {
            return Err(Error::NoChannel);
        }
        let mut packet = Vec::new();
        packet.extend_from_slice(self.make_header().as_bytes());
        packet.extend_from_slice(payload);
        self.channel.as_ref().unwrap().write(&packet, &mut handles).map_err(Error::PeerWrite)?;
        Ok(())
    }
}

#[repr(C)]
#[derive(Debug, Clone, AsBytes, FromBytes, Unaligned)]
pub(crate) struct AudioCommandHeader {
    pub transaction_id: U32<NativeEndian>,
    pub command_type: U32<NativeEndian>,
}

#[derive(Debug, PartialEq, Clone)]
pub enum AudioSampleFormat {
    BitStream,
    Eight { unsigned: bool },
    Sixteen { unsigned: bool, invert_endian: bool },
    TwentyPacked { unsigned: bool, invert_endian: bool },
    TwentyFourPacked { unsigned: bool, invert_endian: bool },
    TwentyIn32 { unsigned: bool, invert_endian: bool },
    TwentyFourIn32 { unsigned: bool, invert_endian: bool },
    ThirtyTwo { unsigned: bool, invert_endian: bool },
    Float { invert_endian: bool },
}

impl AudioSampleFormat {
    fn unsigned(&self) -> bool {
        match self {
            AudioSampleFormat::BitStream | AudioSampleFormat::Float { .. } => false,
            AudioSampleFormat::Eight { unsigned }
            | AudioSampleFormat::Sixteen { unsigned, .. }
            | AudioSampleFormat::TwentyPacked { unsigned, .. }
            | AudioSampleFormat::TwentyFourPacked { unsigned, .. }
            | AudioSampleFormat::TwentyIn32 { unsigned, .. }
            | AudioSampleFormat::TwentyFourIn32 { unsigned, .. }
            | AudioSampleFormat::ThirtyTwo { unsigned, .. } => *unsigned,
        }
    }

    fn invert_endian(&self) -> bool {
        match self {
            AudioSampleFormat::BitStream | AudioSampleFormat::Eight { .. } => false,
            AudioSampleFormat::Sixteen { invert_endian, .. }
            | AudioSampleFormat::TwentyPacked { invert_endian, .. }
            | AudioSampleFormat::TwentyFourPacked { invert_endian, .. }
            | AudioSampleFormat::TwentyIn32 { invert_endian, .. }
            | AudioSampleFormat::TwentyFourIn32 { invert_endian, .. }
            | AudioSampleFormat::ThirtyTwo { invert_endian, .. }
            | AudioSampleFormat::Float { invert_endian } => *invert_endian,
        }
    }

    /// Compute the size of an audio frame based on the sample format.
    /// Returns Err(OutOfRange) in the case where it cannot be computed
    /// (bad channel count, bad sample format)
    pub fn compute_frame_size(&self, channels: usize) -> Result<usize> {
        let bytes_per_channel = match self {
            AudioSampleFormat::Eight { .. } => 1,
            AudioSampleFormat::Sixteen { .. } => 2,
            AudioSampleFormat::TwentyFourPacked { .. } => 3,
            AudioSampleFormat::TwentyIn32 { .. }
            | AudioSampleFormat::TwentyFourIn32 { .. }
            | AudioSampleFormat::ThirtyTwo { .. }
            | AudioSampleFormat::Float { .. } => 4,
            _ => return Err(Error::OutOfRange),
        };
        Ok(channels * bytes_per_channel)
    }
}

impl TryFrom<u32> for AudioSampleFormat {
    type Error = Error;

    fn try_from(value: u32) -> Result<Self> {
        const UNSIGNED_FLAG: u32 = 1u32 << 30;
        const INVERT_ENDIAN_FLAG: u32 = 1u32 << 31;
        const FLAG_MASK: u32 = UNSIGNED_FLAG | INVERT_ENDIAN_FLAG;
        let unsigned = value & UNSIGNED_FLAG != 0;
        let invert_endian = value & INVERT_ENDIAN_FLAG != 0;
        let res = match value & !FLAG_MASK {
            0x0000_0001 => AudioSampleFormat::BitStream,
            0x0000_0002 => AudioSampleFormat::Eight { unsigned },
            0x0000_0004 => AudioSampleFormat::Sixteen { unsigned, invert_endian },
            0x0000_0010 => AudioSampleFormat::TwentyPacked { unsigned, invert_endian },
            0x0000_0020 => AudioSampleFormat::TwentyFourPacked { unsigned, invert_endian },
            0x0000_0040 => AudioSampleFormat::TwentyIn32 { unsigned, invert_endian },
            0x0000_0080 => AudioSampleFormat::TwentyFourIn32 { unsigned, invert_endian },
            0x0000_0100 => AudioSampleFormat::ThirtyTwo { unsigned, invert_endian },
            0x0000_0200 => AudioSampleFormat::Float { invert_endian },
            _ => return Err(Error::OutOfRange),
        };
        Ok(res)
    }
}

impl From<&AudioSampleFormat> for u32 {
    fn from(v: &AudioSampleFormat) -> u32 {
        const UNSIGNED_FLAG: u32 = 1u32 << 30;
        const INVERT_ENDIAN_FLAG: u32 = 1u32 << 31;
        let unsigned_flag = if v.unsigned() { UNSIGNED_FLAG } else { 0 };
        let endian_flag = if v.invert_endian() { INVERT_ENDIAN_FLAG } else { 0 };
        let format_bits = match v {
            AudioSampleFormat::BitStream => 0x0000_0001,
            AudioSampleFormat::Eight { .. } => 0x0000_0002,
            AudioSampleFormat::Sixteen { .. } => 0x0000_0004,
            AudioSampleFormat::TwentyPacked { .. } => 0x0000_0010,
            AudioSampleFormat::TwentyFourPacked { .. } => 0x0000_0020,
            AudioSampleFormat::TwentyIn32 { .. } => 0x0000_0040,
            AudioSampleFormat::TwentyFourIn32 { .. } => 0x0000_0080,
            AudioSampleFormat::ThirtyTwo { .. } => 0x0000_0100,
            AudioSampleFormat::Float { .. } => 0x0000_0200,
        };
        unsigned_flag | endian_flag | format_bits
    }
}

bitflags! {
    #[repr(transparent)]
    #[derive(Default, AsBytes)]
    struct AudioStreamRangeFlags: u16 {
        const FPS_CONTINUOUS   = 0b001;
        const FPS_48000_FAMILY = 0b010;
        const FPS_44100_FAMILY = 0b100;
    }
}

#[repr(C)]
#[derive(Default, Clone, AsBytes)]
pub(crate) struct AudioStreamFormatRange {
    sample_formats: u32,
    min_frames_per_second: u32,
    max_frames_per_second: u32,
    min_channels: u8,
    max_channels: u8,
    flags: AudioStreamRangeFlags,
}

impl TryFrom<fidl_fuchsia_media::PcmFormat> for AudioStreamFormatRange {
    type Error = Error;

    fn try_from(format: fidl_fuchsia_media::PcmFormat) -> Result<Self> {
        // TODO: support more bit formats.
        let supported_sample_format = match format.bits_per_sample {
            16 => AudioSampleFormat::Sixteen { unsigned: false, invert_endian: false },
            _ => return Err(Error::OutOfRange),
        };

        let channels = format.channel_map.len() as u8;
        Ok(AudioStreamFormatRange {
            sample_formats: (&supported_sample_format).into(),
            min_frames_per_second: format.frames_per_second,
            max_frames_per_second: format.frames_per_second,
            min_channels: channels,
            max_channels: channels,
            flags: AudioStreamRangeFlags::FPS_CONTINUOUS,
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use fuchsia_async::Executor;
    use futures::stream::StreamExt;

    struct CountStream {
        count: usize,
    }

    impl CountStream {
        fn new() -> CountStream {
            CountStream { count: 0 }
        }
    }

    impl Stream for CountStream {
        type Item = usize;

        fn poll_next(mut self: Pin<&mut Self>, _cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
            self.count += 1;
            Poll::Ready(Some(self.count))
        }
    }

    #[test]
    fn maybestream() {
        let mut exec = Executor::new().unwrap();

        let mut s = MaybeStream::default();

        let mut next_fut = s.next();
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut next_fut));
        next_fut = s.next();
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut next_fut));

        s.set(CountStream::new());

        next_fut = s.next();
        assert_eq!(Poll::Ready(Some(1)), exec.run_until_stalled(&mut next_fut));

        next_fut = s.next();
        assert_eq!(Poll::Ready(Some(2)), exec.run_until_stalled(&mut next_fut));
    }
}
