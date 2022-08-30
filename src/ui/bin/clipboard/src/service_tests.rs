// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Unit tests for [`crate::service`].

#![cfg(test)]

use {
    crate::{service::Service, test_helpers::*},
    anyhow::{Context, Error},
    assert_matches::assert_matches,
    fclip::{
        FocusedReaderRegistryMarker, FocusedReaderRegistryProxy, FocusedWriterRegistryMarker,
        FocusedWriterRegistryProxy,
    },
    fidl_fuchsia_ui_clipboard::{self as fclip},
    fidl_fuchsia_ui_focus::{self as focus, FocusChainProviderMarker},
    fidl_fuchsia_ui_views::ViewRef,
    fidl_fuchsia_ui_views_ext::ViewRefExt,
    focus_chain_provider::FocusChainProviderPublisher,
    fuchsia_async::Task,
    fuchsia_async::{self as fasync, DurationExt},
    fuchsia_scenic::{self as scenic, ViewRefPair},
    fuchsia_zircon::DurationNum,
    std::rc::Rc,
};

/// Holds on to structs instances that are needed for testing, including a copy of the `Service`
/// itself.
struct TestHandles {
    service: Rc<Service>,
    focus_chain_publisher: FocusChainProviderPublisher,
    _focus_chain_provider_task: Task<()>,
}

impl TestHandles {
    pub fn new() -> Result<Self, Error> {
        let (focus_chain_publisher, focus_chain_stream_handler) =
            focus_chain_provider::make_publisher_and_stream_handler();
        let (focus_chain_provider_proxy, focus_chain_provider_stream) =
            fidl::endpoints::create_proxy_and_stream::<FocusChainProviderMarker>()?;
        let focus_chain_provider_task =
            focus_chain_stream_handler.handle_request_stream(focus_chain_provider_stream);

        let service = Service::new(focus_chain_provider_proxy);

        let handles = TestHandles {
            service: service.clone(),
            focus_chain_publisher,
            _focus_chain_provider_task: focus_chain_provider_task,
        };

        Ok(handles)
    }

    fn get_writer_registry(&self) -> Result<FocusedWriterRegistryProxy, Error> {
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<FocusedWriterRegistryMarker>()?;
        self.service.spawn_focused_writer_registry(stream);
        Ok(proxy)
    }

    fn get_reader_registry(&self) -> Result<FocusedReaderRegistryProxy, Error> {
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<FocusedReaderRegistryMarker>()?;
        self.service.spawn_focused_reader_registry(stream);
        Ok(proxy)
    }

    async fn set_focus_chain(&self, chain: Vec<&ViewRef>) -> Result<(), Error> {
        let expected_koid = chain
            .last()
            .map(|view_ref| view_ref.get_koid())
            .transpose()
            .context("failed to get koid")?
            .expect("koid is some");

        let chain = chain
            .into_iter()
            .map(|vr| scenic::duplicate_view_ref(vr))
            .collect::<Result<Vec<_>, _>>()?;

        let focus_chain =
            focus::FocusChain { focus_chain: Some(chain), ..focus::FocusChain::EMPTY };
        self.focus_chain_publisher.set_state_and_notify_if_changed(&focus_chain)?;

        // Wait for focus update to reach service.
        loop {
            if Some(expected_koid) == self.service.read_focused_view_ref_koid() {
                break;
            }
            fasync::Timer::new(5_i64.millis().after_now()).await;
        }

        Ok(())
    }
}

#[fuchsia::test]
async fn test_basic_copy_paste_across_different_view_refs() -> Result<(), Error> {
    let handles = TestHandles::new()?;

    let ViewRefPair { control_ref: _control_ref_a, view_ref: view_ref_a } = ViewRefPair::new()?;
    let ViewRefPair { control_ref: _control_ref_b, view_ref: view_ref_b } = ViewRefPair::new()?;

    handles.set_focus_chain(vec![&view_ref_a]).await?;

    let writer_registry = handles.get_writer_registry()?;
    let writer_a = writer_registry.get_writer(&view_ref_a).await?;

    let reader_registry = handles.get_reader_registry()?;
    let reader_b = reader_registry.get_reader(&view_ref_b).await?;

    let item_to_copy = make_clipboard_item("text/json".to_string(), "{}".to_string());
    let _ = writer_a.set_item(item_to_copy).flatten_err().await?;

    handles.set_focus_chain(vec![&view_ref_b]).await?;

    let pasted_item = reader_b.get_item(fclip::ReaderGetItemRequest::EMPTY).flatten_err().await?;

    let expected_item = make_clipboard_item("text/json".to_string(), "{}".to_string());
    assert_eq!(pasted_item, expected_item);

    Ok(())
}

#[fuchsia::test]
async fn test_basic_copy_paste_in_same_view_ref() -> Result<(), Error> {
    let handles = TestHandles::new()?;

    let ViewRefPair { control_ref: _control_ref, view_ref } = ViewRefPair::new()?;

    handles.set_focus_chain(vec![&view_ref]).await.context("set_focus_chain")?;

    let writer_registry = handles.get_writer_registry()?;
    let writer = writer_registry.get_writer(&view_ref).await?;

    let reader_registry = handles.get_reader_registry()?;
    let reader = reader_registry.get_reader(&view_ref).await?;

    let item_to_copy = make_clipboard_item("text/json".to_string(), "{}".to_string());
    let _ = writer.set_item(item_to_copy).flatten_err().await?;

    let pasted_item = reader.get_item(fclip::ReaderGetItemRequest::EMPTY).flatten_err().await?;

    let expected_item = make_clipboard_item("text/json".to_string(), "{}".to_string());
    assert_eq!(pasted_item, expected_item);

    Ok(())
}

#[fuchsia::test]
async fn test_paste_after_source_view_ref_is_closed() -> Result<(), Error> {
    let handles = TestHandles::new()?;

    let ViewRefPair { control_ref: control_ref_a, view_ref: view_ref_a } = ViewRefPair::new()?;
    let ViewRefPair { control_ref: _control_ref_b, view_ref: view_ref_b } = ViewRefPair::new()?;

    handles.set_focus_chain(vec![&view_ref_a]).await.context("set_focus_chain")?;

    let writer_registry = handles.get_writer_registry()?;
    let writer_a = writer_registry.get_writer(&view_ref_a).await?;

    let reader_registry = handles.get_reader_registry()?;
    let reader_b = reader_registry.get_reader(&view_ref_b).await?;

    let item_to_copy = make_clipboard_item("text/json".to_string(), "{}".to_string());
    let _ = writer_a.set_item(item_to_copy).flatten_err().await?;

    handles.set_focus_chain(vec![&view_ref_b]).await?;
    drop(control_ref_a);

    let pasted_item = reader_b.get_item(fclip::ReaderGetItemRequest::EMPTY).flatten_err().await?;

    let expected_item = make_clipboard_item("text/json".to_string(), "{}".to_string());
    assert_eq!(pasted_item, expected_item);

    Ok(())
}

#[fuchsia::test]
async fn test_copy_and_paste_empty_string() -> Result<(), Error> {
    let handles = TestHandles::new()?;

    let ViewRefPair { control_ref: _control_ref, view_ref } = ViewRefPair::new()?;

    handles.set_focus_chain(vec![&view_ref]).await.context("set_focus_chain")?;

    let writer_registry = handles.get_writer_registry()?;
    let writer = writer_registry.get_writer(&view_ref).await?;

    let reader_registry = handles.get_reader_registry()?;
    let reader = reader_registry.get_reader(&view_ref).await?;

    let item_to_copy = make_clipboard_item("text/plain".to_string(), "".to_string());
    let _ = writer.set_item(item_to_copy).flatten_err().await?;

    let pasted_item = reader.get_item(fclip::ReaderGetItemRequest::EMPTY).flatten_err().await?;

    let expected_item = make_clipboard_item("text/plain".to_string(), "".to_string());
    assert_eq!(pasted_item, expected_item);

    Ok(())
}

#[fuchsia::test]
async fn test_clear_clipboard() -> Result<(), Error> {
    let handles = TestHandles::new()?;

    let ViewRefPair { control_ref: _control_ref_a, view_ref: view_ref_a } = ViewRefPair::new()?;
    let ViewRefPair { control_ref: _control_ref_b, view_ref: view_ref_b } = ViewRefPair::new()?;

    handles.set_focus_chain(vec![&view_ref_a]).await.context("set_focus_chain")?;

    let writer_registry = handles.get_writer_registry()?;
    let writer_a = writer_registry.get_writer(&view_ref_a).await?;

    let reader_registry = handles.get_reader_registry()?;
    let reader_b = reader_registry.get_reader(&view_ref_b).await?;

    let item_to_copy = make_clipboard_item("text/json".to_string(), "{}".to_string());
    let _ = writer_a.set_item(item_to_copy).flatten_err().await?;

    handles.set_focus_chain(vec![&view_ref_b]).await?;
    let pasted = reader_b.get_item(fclip::ReaderGetItemRequest::EMPTY).flatten_err().await?;
    let expected_item = make_clipboard_item("text/json".to_string(), "{}".to_string());
    assert_eq!(pasted, expected_item);

    handles.set_focus_chain(vec![&view_ref_a]).await?;
    let _ = writer_a.clear(fclip::WriterClearRequest::EMPTY).flatten_err().await?;

    handles.set_focus_chain(vec![&view_ref_b]).await?;
    let result = reader_b.get_item(fclip::ReaderGetItemRequest::EMPTY).await?;
    assert_matches!(result, Err(fclip::ClipboardError::Empty));

    Ok(())
}

#[fuchsia::test]
async fn test_duplicate_registration_writer() -> Result<(), Error> {
    let handles = TestHandles::new()?;

    let ViewRefPair { control_ref: _control_ref, view_ref } = ViewRefPair::new()?;
    let view_ref_1 = scenic::duplicate_view_ref(&view_ref)?;

    let registry = handles.get_writer_registry()?;
    let (writer_request, _writer) = make_writer_request(&view_ref)?;
    let _ = registry.request_writer(writer_request).flatten_err().await?;

    let (registration_request, _writer) = make_writer_request(&view_ref_1)?;
    let result = registry.request_writer(registration_request).await?;
    assert_matches!(result, Err(fclip::ClipboardError::InvalidViewRef));

    Ok(())
}

#[fuchsia::test]
async fn test_duplicate_registration_reader() -> Result<(), Error> {
    let handles = TestHandles::new()?;

    let ViewRefPair { control_ref: _control_ref, view_ref } = ViewRefPair::new()?;
    let view_ref_1 = scenic::duplicate_view_ref(&view_ref)?;

    let registry = handles.get_reader_registry()?;
    let (reader_request, _reader) = make_reader_request(&view_ref)?;
    let _ = registry.request_reader(reader_request).flatten_err().await?;

    let (registration_request, _reader) = make_reader_request(&view_ref_1)?;
    let result = registry.request_reader(registration_request).await?;
    assert_matches!(result, Err(fclip::ClipboardError::InvalidViewRef));

    Ok(())
}

#[fuchsia::test]
async fn test_register_closed_view_ref() -> Result<(), Error> {
    let handles = TestHandles::new()?;

    let ViewRefPair { control_ref, view_ref } = ViewRefPair::new()?;
    drop(control_ref);

    let registry = handles.get_writer_registry()?;
    let (registration_request, _writer_client) = make_writer_request(&view_ref)?;

    let result = registry.request_writer(registration_request).await?;
    assert_matches!(result, Err(fclip::ClipboardError::InvalidViewRef));

    Ok(())
}

#[fuchsia::test]
async fn test_register_then_close_view_ref() -> Result<(), Error> {
    let handles = TestHandles::new()?;

    let ViewRefPair { control_ref, view_ref } = ViewRefPair::new()?;

    handles.set_focus_chain(vec![&view_ref]).await?;

    let registry = handles.get_writer_registry()?;
    let writer = registry.get_writer(&view_ref).await?;

    drop(control_ref);

    let item_to_copy = make_clipboard_item("text/json".to_string(), "{}".to_string());
    let result = writer.set_item(item_to_copy).await;
    assert_matches!(result, Err(fidl::Error::ClientChannelClosed { .. }));

    Ok(())
}

#[fuchsia::test]
async fn test_copy_before_focus_chain_is_available() -> Result<(), Error> {
    let handles = TestHandles::new()?;

    let ViewRefPair { control_ref: _control_ref, view_ref } = ViewRefPair::new()?;
    let registry = handles.get_writer_registry()?;
    let writer = registry.get_writer(&view_ref).await?;

    let item_to_copy = make_clipboard_item("text/json".to_string(), "{}".to_string());
    let result = writer.set_item(item_to_copy).await?;
    assert_matches!(result, Err(fclip::ClipboardError::Internal));

    Ok(())
}

#[fuchsia::test]
async fn test_copy_while_unfocused() -> Result<(), Error> {
    let handles = TestHandles::new()?;

    let ViewRefPair { control_ref: _control_ref, view_ref } = ViewRefPair::new()?;
    let ViewRefPair { control_ref: _other_control_ref, view_ref: other_view_ref } =
        ViewRefPair::new()?;

    handles.set_focus_chain(vec![&other_view_ref]).await?;

    let registry = handles.get_writer_registry()?;
    let writer = registry.get_writer(&view_ref).await?;

    let item_to_copy = make_clipboard_item("text/json".to_string(), "{}".to_string());
    let result = writer.set_item(item_to_copy).await?;
    assert_matches!(result, Err(fclip::ClipboardError::Unauthorized));

    Ok(())
}

#[fuchsia::test]
async fn test_copy_missing_payload() -> Result<(), Error> {
    let handles = TestHandles::new()?;

    let ViewRefPair { control_ref: _control_ref, view_ref } = ViewRefPair::new()?;

    handles.set_focus_chain(vec![&view_ref]).await?;

    let registry = handles.get_writer_registry()?;
    let writer = registry.get_writer(&view_ref).await?;

    let item_to_copy = make_clipboard_item("text/json".to_string(), None);
    let result = writer.set_item(item_to_copy).await?;
    assert_matches!(result, Err(fclip::ClipboardError::InvalidRequest));

    Ok(())
}

#[fuchsia::test]
async fn test_copy_and_paste_default_mime_type() -> Result<(), Error> {
    let handles = TestHandles::new()?;

    let ViewRefPair { control_ref: _control_ref, view_ref } = ViewRefPair::new()?;

    handles.set_focus_chain(vec![&view_ref]).await.context("set_focus_chain")?;

    let writer_registry = handles.get_writer_registry()?;
    let writer = writer_registry.get_writer(&view_ref).await?;

    let reader_registry = handles.get_reader_registry()?;
    let reader = reader_registry.get_reader(&view_ref).await?;

    let item_to_copy = make_clipboard_item(None, "abc".to_string());
    let _ = writer.set_item(item_to_copy).flatten_err().await?;

    let pasted_item = reader.get_item(fclip::ReaderGetItemRequest::EMPTY).flatten_err().await?;

    let expected_item =
        make_clipboard_item("text/plain;charset=utf-8".to_string(), "abc".to_string());
    assert_eq!(pasted_item, expected_item);

    Ok(())
}
