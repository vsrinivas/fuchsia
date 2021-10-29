// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Encoding diagnostic records using the Fuchsia Tracing format.

use crate::{ArgType, Header, SeverityExt, StringRef};
use fidl_fuchsia_diagnostics::Severity;
use fidl_fuchsia_diagnostics_stream::{Argument, Record, Value};
use fuchsia_zircon as zx;
use std::fmt::Debug;
use std::{array::TryFromSliceError, io::Cursor};
use thiserror::Error;
use tracing::Event;
use tracing_core::field::{Field, Visit};
use tracing_log::NormalizeEvent;

/// An `Encoder` wraps any value implementing `BufMutShared` and writes diagnostic stream records
/// into it.
pub struct Encoder<B> {
    pub(crate) buf: B,
}

impl<B> Encoder<B>
where
    B: BufMutShared,
{
    /// Create a new `Encoder` from the provided buffer.
    pub fn new(buf: B) -> Self {
        Self { buf }
    }

    /// Returns a reference to the underlying buffer being used for encoding.
    pub fn inner(&self) -> &B {
        &self.buf
    }

    /// Writes a [`tracing::Event`] to the buffer as a record.
    ///
    /// Fails if there is insufficient space in the buffer for encoding.
    pub fn write_event(
        &mut self,
        event: &Event<'_>,
        tags: Option<&[String]>,
        pid: zx::Koid,
        tid: zx::Koid,
        dropped: u32,
    ) -> Result<(), EncodingError> {
        let mut builder = RecordBuilder::from_tracing_event(
            event,
            pid.raw_koid() as u64,
            tid.raw_koid() as u64,
            dropped,
        );
        if let Some(tags) = tags {
            for tag in tags {
                builder.add_tag(tag.as_ref());
            }
        }
        self.write_record(&builder.inner)
    }

    /// Writes a `Record` to the buffer, including extra fields to match the behavior for recording
    /// an event.
    pub fn write_record_for_test(
        &mut self,
        record: &Record,
        tags: Option<&[String]>,
        pid: zx::Koid,
        tid: zx::Koid,
        file: &str,
        line: u32,
        dropped: u32,
    ) -> Result<(), EncodingError> {
        let mut builder = RecordBuilder::new(
            record.severity,
            pid.raw_koid() as u64,
            tid.raw_koid() as u64,
            Some(file),
            Some(line),
            dropped,
        );
        if let Some(tags) = tags {
            for tag in tags {
                builder.add_tag(tag.as_ref());
            }
        }
        builder.inner.arguments.extend(record.arguments.iter().cloned());
        self.write_record(&builder.inner)
    }

    /// Write a record with this encoder.
    ///
    /// Fails if there is insufficient space in the buffer for encoding, but it does not yet attempt
    /// to zero out any partial record writes.
    pub fn write_record(&mut self, record: &Record) -> Result<(), EncodingError> {
        // TODO(fxbug.dev/59992): on failure, zero out the region we were using
        let starting_idx = self.buf.cursor();

        let header_slot = self.buf.put_slot(std::mem::size_of::<u64>())?;
        self.write_i64(record.timestamp)?;

        for arg in &record.arguments {
            self.write_argument(arg)?;
        }

        let mut header = Header(0);
        header.set_type(crate::TRACING_FORMAT_LOG_RECORD_TYPE);
        header.set_severity(record.severity.into_primitive());

        let length = self.buf.cursor() - starting_idx;
        header.set_len(length);

        assert_eq!(length % 8, 0, "all records must be written 8-byte aligned");
        self.buf.fill_slot(header_slot, &header.0.to_le_bytes());

        Ok(())
    }

    pub(super) fn write_argument(&mut self, argument: &Argument) -> Result<(), EncodingError> {
        let starting_idx = self.buf.cursor();

        let header_slot = self.buf.put_slot(std::mem::size_of::<Header>())?;

        let mut header = Header(0);

        self.write_string(&argument.name)?;
        header.set_name_ref(StringRef::for_str(&argument.name).mask());

        match &argument.value {
            Value::SignedInt(s) => {
                header.set_type(ArgType::I64 as u8);
                self.write_i64(*s)
            }
            Value::UnsignedInt(u) => {
                header.set_type(ArgType::U64 as u8);
                self.write_u64(*u)
            }
            Value::Floating(f) => {
                header.set_type(ArgType::F64 as u8);
                self.write_f64(*f)
            }
            Value::Text(t) => {
                header.set_type(ArgType::String as u8);
                header.set_value_ref(StringRef::for_str(t).mask());
                self.write_string(t)
            }
            _ => Err(EncodingError::Unsupported),
        }?;

        let record_len = self.buf.cursor() - starting_idx;
        assert_eq!(record_len % 8, 0, "arguments must be 8-byte aligned");

        header.set_size_words((record_len / 8) as u16);
        self.buf.fill_slot(header_slot, &header.0.to_le_bytes());

        Ok(())
    }

    /// Write an unsigned integer.
    fn write_u64(&mut self, n: u64) -> Result<(), EncodingError> {
        self.buf.put_u64_le(n).map_err(|_| EncodingError::BufferTooSmall)
    }

    /// Write a signed integer.
    fn write_i64(&mut self, n: i64) -> Result<(), EncodingError> {
        self.buf.put_i64_le(n).map_err(|_| EncodingError::BufferTooSmall)
    }

    /// Write a floating-point number.
    fn write_f64(&mut self, n: f64) -> Result<(), EncodingError> {
        self.buf.put_f64(n).map_err(|_| EncodingError::BufferTooSmall)
    }

    /// Write a string padded to 8-byte alignment.
    fn write_string(&mut self, src: &str) -> Result<(), EncodingError> {
        self.buf.put_slice(src.as_bytes()).map_err(|_| EncodingError::BufferTooSmall)?;
        unsafe {
            let align = std::mem::size_of::<u64>();
            let num_padding_bytes = (align - src.len() % align) % align;
            // TODO(fxbug.dev/59993) need to enforce that the buffer is zeroed
            self.buf.advance_cursor(num_padding_bytes);
        }
        Ok(())
    }
}

struct RecordBuilder {
    inner: Record,
}

macro_rules! arg {
    ($name:expr, $ty:ident($value:expr)) => {
        Argument { name: $name.into(), value: Value::$ty($value.into()) }
    };
}

impl RecordBuilder {
    fn new(
        severity: Severity,
        pid: u64,
        tid: u64,
        file: Option<&str>,
        line: Option<u32>,
        dropped: u32,
    ) -> Self {
        let timestamp = zx::Time::get_monotonic().into_nanos();
        let mut arguments = vec![];

        arguments.push(arg!("pid", UnsignedInt(pid)));
        arguments.push(arg!("tid", UnsignedInt(tid)));

        if dropped > 0 {
            arguments.push(arg!("num_dropped", UnsignedInt(dropped)));
        }

        if severity >= Severity::Error {
            if let Some(file) = file {
                arguments.push(arg!("file", Text(file)));
            }

            if let Some(line) = line {
                arguments.push(arg!("line", UnsignedInt(line)));
            }
        }

        Self { inner: Record { timestamp, severity, arguments } }
    }

    fn push_arg(&mut self, field: &Field, make_arg: impl FnOnce() -> Argument) {
        if !matches!(field.name(), "log.target" | "log.module_path" | "log.file" | "log.line") {
            self.inner.arguments.push(make_arg());
        }
    }

    fn add_tag(&mut self, tag: &str) {
        self.inner.arguments.push(arg!("tag", Text(tag.to_string())));
    }

    fn from_tracing_event(event: &Event<'_>, pid: u64, tid: u64, dropped: u32) -> Self {
        // normalizing is needed to get log records to show up in trace metadata correctly
        let metadata = event.normalized_metadata();
        let metadata = if let Some(ref m) = metadata { m } else { event.metadata() };

        // TODO(fxbug.dev/56090) do this without allocating all the intermediate values
        let mut builder =
            Self::new(metadata.severity(), pid, tid, metadata.file(), metadata.line(), dropped);
        event.record(&mut builder);
        builder
    }
}

impl Visit for RecordBuilder {
    fn record_debug(&mut self, field: &Field, value: &dyn Debug) {
        self.push_arg(field, || arg!(field.name(), Text(format!("{:?}", value))));
    }

    fn record_str(&mut self, field: &Field, value: &str) {
        self.push_arg(field, || arg!(field.name(), Text(value.to_string())));
    }

    fn record_i64(&mut self, field: &Field, value: i64) {
        self.push_arg(field, || arg!(field.name(), SignedInt(value)));
    }

    fn record_u64(&mut self, field: &Field, value: u64) {
        self.push_arg(field, || arg!(field.name(), UnsignedInt(value)));
    }
    // TODO(fxbug.dev/56049) support bools natively and impl record_bool
}

/// Analogous to `bytes::BufMut`, but immutably-sized and appropriate for use in shared memory.
pub trait BufMutShared {
    /// Returns the number of total bytes this container can store. Shared memory buffers are not
    /// expected to resize and this should return the same value during the entire lifetime of the
    /// buffer.
    fn capacity(&self) -> usize;

    /// Returns the current position into which the next write is expected.
    fn cursor(&self) -> usize;

    /// Advance the write cursor by `n` bytes. This is marked unsafe because a malformed caller may
    /// cause a subsequent out-of-bounds write.
    unsafe fn advance_cursor(&mut self, n: usize);

    /// Write a copy of the `src` slice into the buffer, starting at the provided offset.
    ///
    /// Implementations are not expected to bounds check the requested copy, although they may do
    /// so and still satisfy this trait's contract.
    unsafe fn put_slice_at(&mut self, src: &[u8], offset: usize);

    /// Returns whether the buffer has sufficient remaining capacity to write an incoming value.
    fn has_remaining(&self, num_bytes: usize) -> bool {
        (self.cursor() + num_bytes) <= self.capacity()
    }

    /// Advances the write cursor without immediately writing any bytes to the buffer. The returned
    /// struct offers the ability to later write to the provided portion of the buffer.
    fn put_slot(&mut self, width: usize) -> Result<WriteSlot, EncodingError> {
        if self.has_remaining(width) {
            let slot = WriteSlot { range: self.cursor()..(self.cursor() + width) };
            unsafe {
                self.advance_cursor(width);
            }
            Ok(slot)
        } else {
            Err(EncodingError::BufferTooSmall)
        }
    }

    /// Write `src` into the provided slot that was created at a previous point in the stream.
    fn fill_slot(&mut self, slot: WriteSlot, src: &[u8]) {
        assert_eq!(
            src.len(),
            slot.range.end - slot.range.start,
            "WriteSlots can only insert exactly-sized content into the buffer"
        );
        unsafe {
            self.put_slice_at(src, slot.range.start);
        }
    }

    /// Writes the contents of the `src` buffer to `self`, starting at `self.cursor()` and
    /// advancing the cursor by `src.len()`.
    ///
    /// # Panics
    ///
    /// This function panics if there is not enough remaining capacity in `self`.
    fn put_slice(&mut self, src: &[u8]) -> Result<(), ()> {
        if self.has_remaining(src.len()) {
            unsafe {
                self.put_slice_at(src, self.cursor());
                self.advance_cursor(src.len());
            }
            Ok(())
        } else {
            Err(())
        }
    }

    /// Writes an unsigned 64 bit integer to `self` in little-endian byte order.
    ///
    /// Advances the cursor by 8 bytes.
    ///
    /// # Examples
    ///
    /// ```
    /// use bytes::BufMut;
    ///
    /// let mut buf = vec![0; 8];
    /// buf.put_u64_le_at(0x0102030405060708, 0);
    /// assert_eq!(buf, b"\x08\x07\x06\x05\x04\x03\x02\x01");
    /// ```
    ///
    /// # Panics
    ///
    /// This function panics if there is not enough remaining capacity in `self`.
    fn put_u64_le(&mut self, n: u64) -> Result<(), ()> {
        self.put_slice(&n.to_le_bytes())
    }

    /// Writes a signed 64 bit integer to `self` in little-endian byte order.
    ///
    /// The cursor position is advanced by 8.
    ///
    /// # Examples
    ///
    /// ```
    /// use bytes::BufMut;
    ///
    /// let mut buf = vec![0; 8];
    /// buf.put_i64_le_at(0x0102030405060708, 0);
    /// assert_eq!(buf, b"\x08\x07\x06\x05\x04\x03\x02\x01");
    /// ```
    ///
    /// # Panics
    ///
    /// This function panics if there is not enough remaining capacity in `self`.
    fn put_i64_le(&mut self, n: i64) -> Result<(), ()> {
        self.put_slice(&n.to_le_bytes())
    }

    /// Writes a double-precision IEEE 754 floating point number to `self`.
    ///
    /// The cursor position is advanced by 8.
    ///
    /// # Examples
    ///
    /// ```
    /// use bytes::BufMut;
    ///
    /// let mut buf = vec![];
    /// buf.put_i64_le(0x0102030405060708);
    /// assert_eq!(buf, b"\x08\x07\x06\x05\x04\x03\x02\x01");
    /// ```
    ///
    /// # Panics
    ///
    /// This function panics if there is not enough remaining capacity in `self`.
    fn put_f64(&mut self, n: f64) -> Result<(), ()> {
        self.put_slice(&n.to_bits().to_ne_bytes())
    }
}

/// A region of the buffer which was advanced past and can later be filled in.
#[must_use]
pub struct WriteSlot {
    range: std::ops::Range<usize>,
}

impl<'a, T: BufMutShared + ?Sized> BufMutShared for &'a mut T {
    fn capacity(&self) -> usize {
        (**self).capacity()
    }

    fn cursor(&self) -> usize {
        (**self).cursor()
    }

    unsafe fn advance_cursor(&mut self, n: usize) {
        (**self).advance_cursor(n);
    }

    unsafe fn put_slice_at(&mut self, to_put: &[u8], offset: usize) {
        (**self).put_slice_at(to_put, offset);
    }
}

impl<T: BufMutShared + ?Sized> BufMutShared for Box<T> {
    fn capacity(&self) -> usize {
        (**self).capacity()
    }

    fn cursor(&self) -> usize {
        (**self).cursor()
    }

    unsafe fn advance_cursor(&mut self, n: usize) {
        (**self).advance_cursor(n);
    }

    unsafe fn put_slice_at(&mut self, to_put: &[u8], offset: usize) {
        (**self).put_slice_at(to_put, offset);
    }
}

impl BufMutShared for Cursor<Vec<u8>> {
    fn capacity(&self) -> usize {
        self.get_ref().len()
    }

    fn cursor(&self) -> usize {
        self.position() as usize
    }

    unsafe fn advance_cursor(&mut self, n: usize) {
        self.set_position(self.position() + n as u64);
    }

    unsafe fn put_slice_at(&mut self, to_put: &[u8], offset: usize) {
        let dest = &mut self.get_mut()[offset..(offset + to_put.len())];
        dest.copy_from_slice(to_put);
    }
}

impl BufMutShared for Cursor<&mut [u8]> {
    fn capacity(&self) -> usize {
        self.get_ref().len()
    }

    fn cursor(&self) -> usize {
        self.position() as usize
    }

    unsafe fn advance_cursor(&mut self, n: usize) {
        self.set_position(self.position() + n as u64);
    }

    unsafe fn put_slice_at(&mut self, to_put: &[u8], offset: usize) {
        let dest = &mut self.get_mut()[offset..(offset + to_put.len())];
        dest.copy_from_slice(to_put);
    }
}

/// An error that occurred while encoding data to the stream format.
#[derive(Debug, Error)]
pub enum EncodingError {
    /// The provided buffer is too small.
    #[error("buffer is too small")]
    BufferTooSmall,

    /// We attempted to encode values which are not yet supported by this implementation of
    /// the Fuchsia Tracing format.
    #[error("unsupported value type")]
    Unsupported,
}

impl From<TryFromSliceError> for EncodingError {
    fn from(_: TryFromSliceError) -> Self {
        EncodingError::BufferTooSmall
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use once_cell::sync::Lazy;
    use std::sync::Mutex;
    use tracing::Subscriber;
    use tracing_subscriber::{
        layer::{Context, Layer, SubscriberExt},
        Registry,
    };

    #[fuchsia::test]
    fn build_basic_record() {
        let builder = RecordBuilder::new(Severity::Info, 0, 0, None, None, 0);
        assert_eq!(
            builder.inner,
            Record {
                timestamp: builder.inner.timestamp,
                severity: Severity::Info,
                arguments: vec![
                    Argument { name: "pid".into(), value: Value::UnsignedInt(0) },
                    Argument { name: "tid".into(), value: Value::UnsignedInt(0) }
                ]
            }
        );
    }

    #[fuchsia::test]
    fn build_records_with_location() {
        let info = RecordBuilder::new(Severity::Info, 0, 0, Some("foo.rs"), Some(10), 0);
        assert_eq!(
            info.inner,
            Record {
                timestamp: info.inner.timestamp,
                severity: Severity::Info,
                arguments: vec![
                    Argument { name: "pid".into(), value: Value::UnsignedInt(0) },
                    Argument { name: "tid".into(), value: Value::UnsignedInt(0) },
                ]
            }
        );

        let error = RecordBuilder::new(Severity::Error, 0, 0, Some("foo.rs"), Some(10), 0);
        assert_eq!(
            error.inner,
            Record {
                timestamp: error.inner.timestamp,
                severity: Severity::Error,
                arguments: vec![
                    Argument { name: "pid".into(), value: Value::UnsignedInt(0) },
                    Argument { name: "tid".into(), value: Value::UnsignedInt(0) },
                    Argument { name: "file".into(), value: Value::Text("foo.rs".into()) },
                    Argument { name: "line".into(), value: Value::UnsignedInt(10) },
                ]
            }
        );
    }

    #[fuchsia::test]
    fn build_record_with_dropped_count() {
        let builder = RecordBuilder::new(Severity::Info, 0, 0, None, None, 7);
        assert_eq!(
            builder.inner,
            Record {
                timestamp: builder.inner.timestamp,
                severity: Severity::Info,
                arguments: vec![
                    Argument { name: "pid".into(), value: Value::UnsignedInt(0) },
                    Argument { name: "tid".into(), value: Value::UnsignedInt(0) },
                    Argument { name: "num_dropped".into(), value: Value::UnsignedInt(7) },
                ]
            }
        );
    }

    // TODO(fxbug.dev/71242) remove tracing-specific test code
    #[test]
    fn build_record_from_tracing_event() {
        #[derive(Debug)]
        struct PrintMe(u32);

        struct RecordBuilderLayer;
        impl<S: Subscriber> Layer<S> for RecordBuilderLayer {
            fn on_event(&self, event: &Event<'_>, _cx: Context<'_, S>) {
                *LAST_RECORD.lock().unwrap() =
                    Some(RecordBuilder::from_tracing_event(event, 0, 0, 0));
            }
        }
        static LAST_RECORD: Lazy<Mutex<Option<RecordBuilder>>> = Lazy::new(|| Mutex::new(None));

        tracing::subscriber::set_global_default(Registry::default().with(RecordBuilderLayer))
            .unwrap();
        tracing::info!(
            is_a_str = "hahaha",
            is_debug = ?PrintMe(5),
            is_signed = -500,
            is_unsigned = 1000u64,
            "blarg this is a message"
        );

        let last_record = LAST_RECORD.lock().unwrap().as_ref().unwrap().inner.clone();
        assert_eq!(
            last_record,
            Record {
                timestamp: last_record.timestamp,
                severity: Severity::Info,
                arguments: vec![
                    Argument { name: "pid".into(), value: Value::UnsignedInt(0) },
                    Argument { name: "tid".into(), value: Value::UnsignedInt(0) },
                    Argument {
                        name: "message".into(),
                        value: Value::Text("blarg this is a message".into())
                    },
                    Argument { name: "is_a_str".into(), value: Value::Text("hahaha".into()) },
                    Argument { name: "is_debug".into(), value: Value::Text("PrintMe(5)".into()) },
                    Argument { name: "is_signed".into(), value: Value::SignedInt(-500) },
                    Argument { name: "is_unsigned".into(), value: Value::UnsignedInt(1000) },
                ]
            }
        );
    }
}
