// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Helper traits and methods shared between the clipboard service's unit tests and integration
//! tests.

use {
    anyhow::{Context, Error},
    async_trait::async_trait,
    clipboard_shared::ViewRefPrinter,
    fidl::endpoints::create_endpoints,
    fidl_fuchsia_ui_clipboard::{self as fclip},
    fidl_fuchsia_ui_clipboard_ext::FidlClipboardError,
    fidl_fuchsia_ui_views::ViewRef,
    fuchsia_async as fasync,
    fuchsia_scenic::{self as scenic},
    std::{future::Future, task::Poll},
};

#[async_trait(?Send)]
pub trait FocusedWriterRegistryProxyExt: fclip::FocusedWriterRegistryProxyInterface {
    /// Connects to this `FocusedWriterRegistryProxy` and requests a `Writer`.
    async fn get_writer(&self, view_ref: &ViewRef) -> Result<fclip::WriterProxy, Error>;
}

#[async_trait(?Send)]
impl FocusedWriterRegistryProxyExt for fclip::FocusedWriterRegistryProxy {
    async fn get_writer(&self, view_ref: &ViewRef) -> Result<fclip::WriterProxy, Error> {
        let (request, proxy) = make_writer_request(view_ref)?;
        tracing::info!("Registering for write: {:?}", ViewRefPrinter::from(view_ref));
        let _ = self.request_writer(request).flatten_err().await?;
        Ok(proxy)
    }
}

/// Makes a `FocusedWriterRegistryRequestWriterRequest` for the given `ViewRef`.
pub fn make_writer_request(
    view_ref: &ViewRef,
) -> Result<(fclip::FocusedWriterRegistryRequestWriterRequest, fclip::WriterProxy), Error> {
    let view_ref = scenic::duplicate_view_ref(view_ref)?;

    let (client_end, server_end) = create_endpoints::<fclip::WriterMarker>()?;

    let req = fclip::FocusedWriterRegistryRequestWriterRequest {
        view_ref: Some(view_ref),
        writer_request: Some(server_end),
        ..fclip::FocusedWriterRegistryRequestWriterRequest::EMPTY
    };

    Ok((req, client_end.into_proxy()?))
}

#[async_trait(?Send)]
pub trait FocusedReaderRegistryProxyExt: fclip::FocusedReaderRegistryProxyInterface {
    /// Connects to this `FocusedReaderRegistryProxy` and requests a `Reader`.
    async fn get_reader(&self, view_ref: &ViewRef) -> Result<fclip::ReaderProxy, Error>;
}

#[async_trait(?Send)]
impl FocusedReaderRegistryProxyExt for fclip::FocusedReaderRegistryProxy {
    async fn get_reader(&self, view_ref: &ViewRef) -> Result<fclip::ReaderProxy, Error> {
        let (request, proxy) = make_reader_request(view_ref)?;
        tracing::info!("Registering for read: {:?}", ViewRefPrinter::from(view_ref));
        let _ = self.request_reader(request).flatten_err().await?;
        Ok(proxy)
    }
}

/// Makes a `FocusedReaderRegistryReaderWriterRequest` for the given `ViewRef`.
pub fn make_reader_request(
    view_ref: &ViewRef,
) -> Result<(fclip::FocusedReaderRegistryRequestReaderRequest, fclip::ReaderProxy), Error> {
    let view_ref = scenic::duplicate_view_ref(view_ref)?;

    let (client_end, server_end) = create_endpoints::<fclip::ReaderMarker>()?;

    let req = fclip::FocusedReaderRegistryRequestReaderRequest {
        view_ref: Some(view_ref),
        reader_request: Some(server_end),
        ..fclip::FocusedReaderRegistryRequestReaderRequest::EMPTY
    };

    Ok((req, client_end.into_proxy()?))
}

/// Creates a text clipboard item with the given values. Does _not_ fill in a default
/// `mime_type_hint` if the given one is `None`.
pub fn make_clipboard_item(
    mime_type_hint: impl Into<Option<String>>,
    text: impl Into<Option<String>>,
) -> fclip::ClipboardItem {
    fclip::ClipboardItem {
        mime_type_hint: mime_type_hint.into(),
        payload: text.into().map(|text| fclip::ClipboardItemData::Text(text)),
        ..fclip::ClipboardItem::EMPTY
    }
}

/// Extension trait for `Result<T, FidlClipboardError>`.
pub trait ClipboardErrorResult<T> {
    /// Wraps the raw `fidl_fuchsia_ui_clipboard::ClipboardError` in a `FidlClipboardError`.
    fn map_into_clipboard_error(self) -> Result<T, FidlClipboardError>;
}

impl<T> ClipboardErrorResult<T> for Result<T, fclip::ClipboardError> {
    fn map_into_clipboard_error(self) -> Result<T, FidlClipboardError> {
        self.map_err(Into::<FidlClipboardError>::into)
    }
}

/// Extension trait for working with nested `Result` futures returned from clipboard FIDL
/// methods.
#[async_trait(?Send)]
pub trait NestedClipboardErrorResult<T> {
    /// Flattens the nested `Result`s returned by clipboard FIDL methods and converts raw
    /// `ClipboardError`s into `impl std::error::Error`.
    async fn flatten_err(self) -> Result<T, Error>;
}

#[async_trait(?Send)]
impl<T: Unpin> NestedClipboardErrorResult<T>
    for fidl::client::QueryResponseFut<Result<T, fclip::ClipboardError>>
{
    async fn flatten_err(self) -> Result<T, Error> {
        let response = self.await.context("Error in FIDL call")?.map_into_clipboard_error()?;
        Ok(response)
    }
}

/// Extensions for [`std::task::Poll`].
trait PollExt<T> {
    fn into_option(self) -> Option<T>;
    fn unwrap(self) -> T;
}

impl<T> PollExt<T> for Poll<T> {
    fn into_option(self) -> Option<T> {
        match self {
            Poll::Ready(x) => Some(x),
            Poll::Pending => None,
        }
    }

    fn unwrap(self) -> T {
        self.into_option().unwrap()
    }
}

/// Extensions for [`fuchsia_async::TestExecutor`].
pub trait TestExecutorExt {
    fn pin_and_run_until_stalled<F: Future>(&mut self, main_future: F) -> Option<F::Output>;
}

impl TestExecutorExt for fasync::TestExecutor {
    fn pin_and_run_until_stalled<F: Future>(&mut self, main_future: F) -> Option<F::Output> {
        self.run_until_stalled(&mut Box::pin(main_future)).into_option()
    }
}
