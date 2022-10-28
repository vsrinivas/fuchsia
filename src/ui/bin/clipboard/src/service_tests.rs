// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Unit tests for [`crate::service`].

#![cfg(test)]

use {
    crate::{
        metadata::ClipboardMetadata,
        service::{Clock, Service, ServiceDependencies},
        test_helpers::*,
    },
    anyhow::{Context, Error},
    assert_matches::assert_matches,
    async_utils::hanging_get::client::HangingGetStream,
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
    fuchsia_inspect::{assert_data_tree, Inspector},
    fuchsia_scenic::{self as scenic, ViewRefPair},
    fuchsia_zircon::{self as zx, DurationNum},
    futures::StreamExt,
    std::{
        cell::{Cell, RefCell},
        rc::Rc,
        task::Poll,
    },
};

#[derive(Debug)]
enum TestServiceDependencies {/* not instantiable */}

impl ServiceDependencies for TestServiceDependencies {
    type Clock = FakeClock;
}

/// Can be cloned so that one instance can be passed into the service and the other manipulated
/// in the test code.
#[derive(Debug, Clone)]
struct FakeClock {
    now: Rc<Cell<zx::Time>>,
}

#[allow(dead_code)]
impl FakeClock {
    fn new(now: zx::Time) -> Self {
        Self { now: Rc::new(Cell::new(now)) }
    }

    fn set_time(&self, time: zx::Time) {
        self.now.set(time)
    }
}

impl Clock for FakeClock {
    fn now(&self) -> zx::Time {
        self.now.get()
    }
}

/// Holds on to structs instances that are needed for testing, including a copy of the `Service`
/// itself.
#[allow(dead_code)]
struct TestHandles {
    service: Rc<Service<TestServiceDependencies>>,
    clock: FakeClock,
    focus_chain_publisher: FocusChainProviderPublisher,
    /// Mutable so that it can be removed to simulate error conditions.
    focus_chain_provider_task: RefCell<Option<Task<()>>>,
    inspector: Inspector,
}

impl TestHandles {
    pub fn new() -> Result<Self, Error> {
        let (focus_chain_publisher, focus_chain_stream_handler) =
            focus_chain_provider::make_publisher_and_stream_handler();
        let (focus_chain_provider_proxy, focus_chain_provider_stream) =
            fidl::endpoints::create_proxy_and_stream::<FocusChainProviderMarker>()?;
        let focus_chain_provider_task =
            focus_chain_stream_handler.handle_request_stream(focus_chain_provider_stream);

        let inspector = Inspector::new();
        let clock = FakeClock::new(zx::Time::from_nanos(0));

        let service = Service::<TestServiceDependencies>::new(
            clock.clone(),
            focus_chain_provider_proxy,
            inspector.root(),
        );

        let handles = TestHandles {
            service: service.clone(),
            clock,
            focus_chain_publisher,
            focus_chain_provider_task: RefCell::new(Some(focus_chain_provider_task)),
            inspector,
        };

        Ok(handles)
    }

    fn set_time(&self, time: zx::Time) {
        self.clock.set_time(time);
    }

    fn set_time_ns(&self, nanos: i64) {
        self.set_time(zx::Time::from_nanos(nanos))
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

    fn internal_set_focus_chain(&self, chain: Vec<&ViewRef>) -> Result<zx::Koid, Error> {
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
        Ok(expected_koid)
    }

    async fn set_focus_chain(&self, chain: Vec<&ViewRef>) -> Result<(), Error> {
        let expected_koid = self.internal_set_focus_chain(chain)?;
        // Wait for focus update to reach service.
        loop {
            if Some(expected_koid) == self.service.read_focused_view_ref_koid() {
                break;
            }
            fasync::Timer::new(5_i64.millis().after_now()).await;
        }
        Ok(())
    }

    fn set_focus_chain_with_test_exec(
        &self,
        chain: Vec<&ViewRef>,
        test_exec: &mut fasync::TestExecutor,
    ) -> Result<(), Error> {
        let expected_koid = self.internal_set_focus_chain(chain)?;
        self.run_focus_chain_provider_until_stalled(test_exec);
        assert_eq!(self.service.read_focused_view_ref_koid(), Some(expected_koid));
        Ok(())
    }

    fn run_focus_chain_provider_until_stalled(&self, test_exec: &mut fasync::TestExecutor) {
        let _ = test_exec
            .run_until_stalled(&mut self.focus_chain_provider_task.borrow_mut().as_mut().unwrap());
    }

    async fn kill_focus_chain_provider(&self) {
        let task = self.focus_chain_provider_task.borrow_mut().take();
        drop(task);

        // The timer duration doesn't matter here; we just need to give the executor a chance to
        // clean up the dropped task, which doesn't happen until the task is polled again.
        fasync::Timer::new(1_i64.nanos().after_now()).await;
    }
}

#[fuchsia::test]
async fn test_basic_copy_paste_across_different_view_refs() -> Result<(), Error> {
    let handles = TestHandles::new()?;

    let ViewRefPair { control_ref: _control_ref_a, view_ref: view_ref_a } = ViewRefPair::new()?;
    let ViewRefPair { control_ref: _control_ref_b, view_ref: view_ref_b } = ViewRefPair::new()?;

    handles.set_time_ns(10);
    handles.set_focus_chain(vec![&view_ref_a]).await?;

    let writer_registry = handles.get_writer_registry()?;
    let writer_a = writer_registry.get_writer(&view_ref_a).await?;

    let reader_registry = handles.get_reader_registry()?;
    let reader_b = reader_registry.get_reader(&view_ref_b).await?;

    handles.set_time_ns(20);
    let item_to_copy = make_clipboard_item("text/json".to_string(), "{}".to_string());
    let _ = writer_a.set_item(item_to_copy).flatten_err().await?;

    handles.set_time_ns(30);
    handles.set_focus_chain(vec![&view_ref_b]).await?;

    handles.set_time_ns(40);
    let pasted_item = reader_b.get_item(fclip::ReaderGetItemRequest::EMPTY).flatten_err().await?;

    let expected_item = make_clipboard_item("text/json".to_string(), "{}".to_string());
    assert_eq!(pasted_item, expected_item);

    assert_data_tree!(handles.inspector, root: contains {
        clipboard: {
            events: contains {
                focus_updated: {
                    event_count: 2u64,
                    last_seen_ns: 30i64,
                },
                write: {
                    event_count: 1u64,
                    last_seen_ns: 20i64,
                },
                read: {
                    event_count: 1u64,
                    last_seen_ns: 40i64,
                },
            },
            reader_registry_client_count: 1u64,
            writer_registry_client_count: 1u64,
            reader_count: 1u64,
            writer_count: 1u64,
            last_modified_ns: 20i64,
            items: {
                "text/json": {
                    size_bytes: 2u64, // "{}"
                },
            }
        }
    });

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

#[fuchsia::test]
fn test_writer_and_watcher() -> Result<(), Error> {
    let mut exec = fasync::TestExecutor::new()?;

    let handles = TestHandles::new()?;

    let ViewRefPair { control_ref: _control_ref_a, view_ref: view_ref_a } = ViewRefPair::new()?;
    let ViewRefPair { control_ref: _control_ref_b, view_ref: view_ref_b } = ViewRefPair::new()?;

    handles.set_time_ns(5);
    handles.set_focus_chain_with_test_exec(vec![&view_ref_a], &mut exec)?;
    let writer_registry = handles.get_writer_registry()?;
    let reader_registry = handles.get_reader_registry()?;

    let (writer_a, reader_b): (fclip::WriterProxy, fclip::ReaderProxy) = exec
        .pin_and_run_until_stalled(async {
            let writer_a = writer_registry.get_writer(&view_ref_a).await.unwrap();
            let reader_b = reader_registry.get_reader(&view_ref_b).await.unwrap();
            (writer_a, reader_b)
        })
        .unwrap();

    {
        let mut initial_watch_fut = reader_b.watch(fclip::ReaderWatchRequest::EMPTY);
        assert_matches!(
            exec.run_until_stalled(&mut initial_watch_fut),
            Poll::Ready(Ok(Err(fclip::ClipboardError::Unauthorized)))
        );
    }

    handles.set_time_ns(10);

    let mut watch_fut = reader_b.watch(fclip::ReaderWatchRequest::EMPTY);
    assert_matches!(exec.run_until_stalled(&mut watch_fut), Poll::Pending);

    {
        let mut set_fut = writer_a.set_item(fclip::ClipboardItem {
            mime_type_hint: Some("text/json".to_string()),
            payload: Some(fclip::ClipboardItemData::Text("{}".to_string())),
            ..fclip::ClipboardItem::EMPTY
        });
        assert_matches!(exec.run_until_stalled(&mut set_fut), Poll::Ready(Ok(Ok(_))));
    }

    handles.set_time_ns(15);

    handles.set_focus_chain_with_test_exec(vec![&view_ref_b], &mut exec)?;

    assert_matches!(
        exec.run_until_stalled(&mut watch_fut),
        Poll::Ready(Ok(Ok(metadata))) if metadata ==
            ClipboardMetadata::with_last_modified_ns(10).into()
    );

    assert_data_tree!(handles.inspector, root: contains {
        clipboard: contains {
            events: contains {
                focus_updated: {
                    event_count: 2u64,
                    last_seen_ns: 15i64,
                },
                write: {
                    event_count: 1u64,
                    last_seen_ns: 10i64,
                },
                watch: {
                    event_count: 2u64,
                    last_seen_ns: 10i64,
                }
            },
            last_modified_ns: 10i64,
        }
    });

    Ok(())
}

/// Similar to above, but demonstrates compatibility with
/// `async_utils::hanging_get::HangingGetStream`.
#[fuchsia::test]
fn test_writer_and_watcher_hanging_get_stream() -> Result<(), Error> {
    let mut exec = fasync::TestExecutor::new()?;

    let handles = TestHandles::new()?;

    let ViewRefPair { control_ref: _control_ref_a, view_ref: view_ref_a } = ViewRefPair::new()?;
    let ViewRefPair { control_ref: _control_ref_b, view_ref: view_ref_b } = ViewRefPair::new()?;

    handles.set_time_ns(5);
    handles.set_focus_chain_with_test_exec(vec![&view_ref_a], &mut exec)?;
    let writer_registry = handles.get_writer_registry()?;
    let reader_registry = handles.get_reader_registry()?;

    let (writer_a, reader_b): (fclip::WriterProxy, fclip::ReaderProxy) = exec
        .pin_and_run_until_stalled(async {
            let writer_a = writer_registry.get_writer(&view_ref_a).await.unwrap();
            let reader_b = reader_registry.get_reader(&view_ref_b).await.unwrap();
            (writer_a, reader_b)
        })
        .unwrap();

    let mut watch_stream = HangingGetStream::new(reader_b.clone(), |reader| {
        reader.watch(fclip::ReaderWatchRequest::EMPTY)
    });

    let mut stream_fut = watch_stream.next();
    assert_matches!(
        exec.run_until_stalled(&mut stream_fut),
        Poll::Ready(Some(Ok(Err(fclip::ClipboardError::Unauthorized))))
    );

    handles.set_time_ns(10);

    let mut stream_fut = watch_stream.next();
    assert_matches!(exec.run_until_stalled(&mut stream_fut), Poll::Pending);

    {
        let mut set_fut = writer_a.set_item(fclip::ClipboardItem {
            mime_type_hint: Some("text/json".to_string()),
            payload: Some(fclip::ClipboardItemData::Text("{}".to_string())),
            ..fclip::ClipboardItem::EMPTY
        });
        assert_matches!(exec.run_until_stalled(&mut set_fut), Poll::Ready(Ok(Ok(_))));
    }

    assert_matches!(exec.run_until_stalled(&mut stream_fut), Poll::Pending);

    handles.set_time_ns(15);
    handles.set_focus_chain_with_test_exec(vec![&view_ref_b], &mut exec)?;

    assert_matches!(
        exec.run_until_stalled(&mut stream_fut),
        Poll::Ready(Some(Ok(Ok(metadata)))) if metadata ==
            ClipboardMetadata::with_last_modified_ns(10).into()
    );
    Ok(())
}

#[fuchsia::test]
async fn test_inspect_health_and_focus() -> Result<(), Error> {
    let handles = TestHandles::new()?;

    assert_data_tree!(handles.inspector, root: contains {
        "fuchsia.inspect.Health": contains {
            status: "STARTING_UP"
        }
    });

    let ViewRefPair { control_ref: _control_ref, view_ref } = ViewRefPair::new()?;
    handles.set_focus_chain(vec![&view_ref]).await.context("set_focus_chain")?;

    assert_data_tree!(handles.inspector, root: contains {
        "fuchsia.inspect.Health": contains {
            status: "OK"
        }
    });

    Ok(())
}

#[fuchsia::test]
async fn test_inspect_health_and_focus_error() -> Result<(), Error> {
    let handles = TestHandles::new()?;

    assert_data_tree!(handles.inspector, root: contains {
        "fuchsia.inspect.Health": contains {
            status: "STARTING_UP"
        }
    });

    handles.kill_focus_chain_provider().await;

    assert_data_tree!(handles.inspector, root: contains {
        "fuchsia.inspect.Health": contains {
            status: "UNHEALTHY"
        }
    });

    Ok(())
}
