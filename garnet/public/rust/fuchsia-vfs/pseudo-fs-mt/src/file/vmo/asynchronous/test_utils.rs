// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Common utilities used by tests for the VMO backed files.

use super::{
    read_only, read_write, write_only, AsyncFile, ConsumeVmoResult, InitVmoResult, NewVmo,
    StubConsumeVmoRes,
};

use {
    fuchsia_zircon::{Status, Vmo, VmoOptions},
    futures::future::BoxFuture,
    std::sync::{
        atomic::{AtomicUsize, Ordering},
        Arc,
    },
    void::Void,
};

#[doc(hidden)]
pub mod reexport {
    pub use void::unreachable;
}

/// `simple_init_*` family of functions will set the capacity of the generated file to the larger
/// of this value or the provided initial content size.  It removes some of the repetition from the
/// tests as all the existing tests are actually quite happy with this default.
const DEFAULT_MIN_CAPACITY: u64 = 100;

pub fn simple_init_vmo(
    content: &[u8],
) -> impl Fn() -> BoxFuture<'static, InitVmoResult> + Send + Sync + 'static {
    let capacity = std::cmp::max(content.len() as u64, DEFAULT_MIN_CAPACITY);
    simple_init_vmo_with_capacity(content, capacity)
}

pub fn simple_init_vmo_with_capacity(
    content: &[u8],
    capacity: u64,
) -> impl Fn() -> BoxFuture<'static, InitVmoResult> + Send + Sync + 'static {
    let content = content.to_vec();
    move || {
        // In "production" code we would instead wrap `content` in a smart pointer to be able to
        // share it with the async block, but for tests it is fine to just clone it.
        let content = content.clone();
        Box::pin(async move {
            let size = content.len() as u64;
            let vmo_size = std::cmp::max(size, capacity);
            let vmo = Vmo::create(vmo_size)?;
            vmo.write(&content, 0)?;
            Ok(NewVmo { vmo, size, capacity })
        })
    }
}

pub fn simple_init_vmo_resizable(
    content: &[u8],
) -> impl Fn() -> BoxFuture<'static, InitVmoResult> + Send + Sync + 'static {
    let capacity = std::cmp::max(content.len() as u64, DEFAULT_MIN_CAPACITY);
    simple_init_vmo_resizable_with_capacity(content, capacity)
}

pub fn simple_init_vmo_resizable_with_capacity(
    content: &[u8],
    capacity: u64,
) -> impl Fn() -> BoxFuture<'static, InitVmoResult> + Send + Sync + 'static {
    let content = content.to_vec();
    move || {
        let content = content.clone();
        Box::pin(async move {
            let size = content.len() as u64;
            let vmo_size = std::cmp::max(size, capacity);
            let vmo = Vmo::create_with_opts(VmoOptions::RESIZABLE, vmo_size)?;
            vmo.write(&content, 0)?;
            Ok(NewVmo { vmo, size, capacity })
        })
    }
}

pub fn simple_read_only(
    content: &[u8],
) -> Arc<
    AsyncFile<
        impl Fn() -> BoxFuture<'static, InitVmoResult> + Send + Sync + 'static,
        BoxFuture<'static, InitVmoResult>,
        fn(Vmo) -> StubConsumeVmoRes,
        StubConsumeVmoRes,
    >,
> {
    read_only(simple_init_vmo(content))
}

pub enum AssertVmoContentError {
    VmoReadFailed(Status),
    UnexpectedContent(Vec<u8>),
}

pub fn assert_vmo_content(vmo: &Vmo, expected: &[u8]) -> Result<(), AssertVmoContentError> {
    let mut buffer = Vec::with_capacity(expected.len());
    buffer.resize(expected.len(), 0);
    vmo.read(&mut buffer, 0).map_err(AssertVmoContentError::VmoReadFailed)?;
    if buffer != expected {
        Err(AssertVmoContentError::UnexpectedContent(buffer))
    } else {
        Ok(())
    }
}

#[macro_export]
macro_rules! assert_vmo_content {
    ($vmo:expr, $expected:expr) => {{
        use $crate::file::vmo::asynchronous::test_utils::{
            assert_vmo_content, AssertVmoContentError,
        };

        let expected = $expected;
        match assert_vmo_content($vmo, expected) {
            Ok(()) => (),
            Err(AssertVmoContentError::VmoReadFailed(status)) => {
                panic!("`vmo.read(&mut buffer, 0)` failed: {}", status)
            }
            Err(AssertVmoContentError::UnexpectedContent(buffer)) => panic!(
                "Unpexpected content:\n\
                 Expected: {:x?}\n\
                 Actual:   {:x?}",
                expected, &buffer
            ),
        }
    }};
}

pub enum ReportInvalidVmoContentError {
    VmoReadFailed(Status),
}

pub fn report_invalid_vmo_content(
    vmo: &Vmo,
    context: &str,
) -> Result<Void, ReportInvalidVmoContentError> {
    // For debugging purposes we print the first 100 bytes.  This is an arbitrary choice.
    let mut buffer = Vec::with_capacity(100);
    buffer.resize(100, 0);
    vmo.read(&mut buffer, 0).map_err(ReportInvalidVmoContentError::VmoReadFailed)?;
    panic!(
        "{}.  Content:\n\
         {:x?}",
        context, buffer
    );
}

#[macro_export]
macro_rules! report_invalid_vmo_content {
    ($vmo:expr, $context:expr) => {{
        use $crate::file::vmo::asynchronous::test_utils::{
            reexport::unreachable, report_invalid_vmo_content, ReportInvalidVmoContentError,
        };

        match report_invalid_vmo_content($vmo, $context) {
            Err(ReportInvalidVmoContentError::VmoReadFailed(status)) => {
                panic!("`vmo.read(&mut buffer, 0)` failed: {}", status)
            }
            Ok(x) => unreachable(x),
        }
    }};
}

pub fn simple_consume_vmo(
    expected: &[u8],
) -> impl Fn(Vmo) -> BoxFuture<'static, ConsumeVmoResult> + Send + Sync + 'static {
    let expected = expected.to_vec();
    move |vmo| {
        let expected = expected.clone();
        Box::pin(async move {
            assert_vmo_content!(&vmo, &expected);
        })
    }
}

pub fn consume_vmo_with_counter(
    expected: &[u8],
    counter: Arc<AtomicUsize>,
    max_count: usize,
    failure_context: &str,
) -> impl Fn(Vmo) -> BoxFuture<'static, ConsumeVmoResult> + Send + Sync + 'static {
    let expected = expected.to_vec();
    let failure_context = failure_context.to_string();
    move |vmo| {
        let expected = expected.clone();
        let counter = counter.clone();
        let failure_context = failure_context.clone();
        Box::pin(async move {
            let write_attempt = counter.fetch_add(1, Ordering::Relaxed);
            if write_attempt < max_count {
                assert_vmo_content!(&vmo, &expected);
            } else {
                let failure_context = format!(
                    "{}.  Called {} time(s).\n\
                     Expected no more than {} time(s).",
                    failure_context, write_attempt, max_count
                );
                report_invalid_vmo_content!(&vmo, &failure_context);
            }
        })
    }
}

pub fn simple_write_only(
    expected: &[u8],
) -> Arc<
    AsyncFile<
        impl Fn() -> BoxFuture<'static, InitVmoResult> + Send + Sync + 'static,
        BoxFuture<'static, InitVmoResult>,
        impl Fn(Vmo) -> BoxFuture<'static, ConsumeVmoResult> + Send + Sync + 'static,
        BoxFuture<'static, ConsumeVmoResult>,
    >,
> {
    write_only(simple_init_vmo_resizable(b""), simple_consume_vmo(expected))
}

pub fn simple_write_only_with_capacity(
    capacity: u64,
    expected: &[u8],
) -> Arc<
    AsyncFile<
        impl Fn() -> BoxFuture<'static, InitVmoResult> + Send + Sync + 'static,
        BoxFuture<'static, InitVmoResult>,
        impl Fn(Vmo) -> BoxFuture<'static, ConsumeVmoResult> + Send + Sync + 'static,
        BoxFuture<'static, ConsumeVmoResult>,
    >,
> {
    write_only(simple_init_vmo_with_capacity(b"", capacity), simple_consume_vmo(expected))
}

pub fn simple_read_write(
    initial_content: &[u8],
    final_content: &[u8],
) -> Arc<
    AsyncFile<
        impl Fn() -> BoxFuture<'static, InitVmoResult> + Send + Sync + 'static,
        BoxFuture<'static, InitVmoResult>,
        impl Fn(Vmo) -> BoxFuture<'static, ConsumeVmoResult> + Send + Sync + 'static,
        BoxFuture<'static, ConsumeVmoResult>,
    >,
> {
    read_write(simple_init_vmo(initial_content), simple_consume_vmo(final_content))
}

pub fn simple_read_write_resizeable(
    initial_content: &[u8],
    final_content: &[u8],
) -> Arc<
    AsyncFile<
        impl Fn() -> BoxFuture<'static, InitVmoResult> + Send + Sync + 'static,
        BoxFuture<'static, InitVmoResult>,
        impl Fn(Vmo) -> BoxFuture<'static, ConsumeVmoResult> + Send + Sync + 'static,
        BoxFuture<'static, ConsumeVmoResult>,
    >,
> {
    read_write(simple_init_vmo_resizable(initial_content), simple_consume_vmo(final_content))
}
