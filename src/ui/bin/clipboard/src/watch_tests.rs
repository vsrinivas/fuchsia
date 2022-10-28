// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Unit tests for [`crate::watch`].

#![cfg(test)]

use {
    crate::{metadata::ClipboardMetadata, watch::WatchServer},
    anyhow::{Error, Result},
    assert_matches::assert_matches,
    fidl_fuchsia_ui_clipboard::{self as fclip, ReaderWatchRequest},
    fidl_fuchsia_ui_views_ext::ViewRefExt,
    fuchsia_async as fasync,
    fuchsia_scenic::ViewRefPair,
    fuchsia_zircon as zx,
    futures::{task::Poll, StreamExt},
    std::rc::Rc,
    tracing::debug,
};

/// `WatchServer` doesn't validate `ViewRef` koids, so we don't care about dropping the
/// `ViewRefControl`. Still, we mint our `Koid`s the standard way for some verisimilitude.
fn make_view_ref_koid() -> Result<zx::Koid, Error> {
    let ViewRefPair { control_ref: _control_ref, view_ref } = ViewRefPair::new()?;
    Ok(view_ref.get_koid()?)
}

struct TestHandles {
    server: Rc<WatchServer>,
    server_task: fasync::Task<()>,
}

impl TestHandles {
    fn new(initial_state: ClipboardMetadata) -> Self {
        let (server, server_task) = WatchServer::new(initial_state);
        Self { server, server_task }
    }

    fn new_reader(&self) -> Result<(fclip::ReaderProxy, zx::Koid), Error> {
        let view_ref_koid = make_view_ref_koid()?;
        Self::new_reader_with_koid(&self, view_ref_koid).map(|reader| (reader, view_ref_koid))
    }

    fn new_reader_with_koid(&self, view_ref_koid: zx::Koid) -> Result<fclip::ReaderProxy, Error> {
        let (reader_proxy, mut reader_request_stream) =
            fidl::endpoints::create_proxy_and_stream::<fclip::ReaderMarker>()?;
        let server_weak = self.server.weak();
        fasync::Task::local(async move {
            // TODO(fxbug.dev/113422): Switch to let chain when `let_chains` feature is stabilized.
            while let Some(server) = server_weak.upgrade() {
                if let Some(Ok(req)) = reader_request_stream.next().await {
                    match req {
                        fclip::ReaderRequest::Watch { payload: _, responder } => {
                            // TODO: Any other error handling needed?
                            server.watch(view_ref_koid, responder).unwrap();
                        }
                        _ => unimplemented!(),
                    }
                } else {
                    break;
                }
            }
        })
        .detach();
        Ok(reader_proxy)
    }

    fn run_server_until_stalled(&mut self, test_exec: &mut fasync::TestExecutor) {
        let _ = test_exec.run_until_stalled(&mut self.server_task);
    }
}

#[fuchsia::test]
fn test_single_unfocused_client() -> Result<()> {
    let mut exec = fasync::TestExecutor::new()?;

    let mut handles = TestHandles::new(ClipboardMetadata::with_last_modified_ns(1));

    let koid_a = make_view_ref_koid()?;
    handles.server.update_focus(koid_a)?;

    handles.run_server_until_stalled(&mut exec);

    let (reader_b, _koid_b) = handles.new_reader()?;

    let mut initial_watch_fut = reader_b.watch(ReaderWatchRequest::EMPTY);
    assert_matches!(
        exec.run_until_stalled(&mut initial_watch_fut),
        Poll::Ready(Ok(Err(fclip::ClipboardError::Unauthorized)))
    );

    let mut watch_fut = reader_b.watch(ReaderWatchRequest::EMPTY);
    assert_matches!(exec.run_until_stalled(&mut watch_fut), Poll::Pending);

    assert!(handles
        .server
        .update_clipboard_metadata(ClipboardMetadata::with_last_modified_ns(2))
        .is_ok());

    assert_matches!(exec.run_until_stalled(&mut watch_fut), Poll::Pending);

    Ok(())
}

#[fuchsia::test(logging_minimum_severity = "debug")]
fn test_single_client_first_watches_while_focused() -> Result<()> {
    let mut exec = fasync::TestExecutor::new()?;

    let mut handles = TestHandles::new(ClipboardMetadata::with_last_modified_ns(1));

    let koid_a = make_view_ref_koid()?;
    handles.server.update_focus(koid_a)?;

    handles.run_server_until_stalled(&mut exec);

    let reader_a = handles.new_reader_with_koid(koid_a)?;

    let mut initial_watch_fut = reader_a.watch(ReaderWatchRequest::EMPTY);
    assert_matches!(
        exec.run_until_stalled(&mut initial_watch_fut),
        Poll::Ready(Ok(Ok(metadata))) if metadata ==
            ClipboardMetadata::with_last_modified_ns(1).into()
    );

    Ok(())
}

#[fuchsia::test(logging_minimum_severity = "debug")]
fn test_single_client_first_watches_then_gains_focus() -> Result<()> {
    let mut exec = fasync::TestExecutor::new()?;

    let mut handles = TestHandles::new(ClipboardMetadata::with_last_modified_ns(1));

    let koid_a = make_view_ref_koid()?;
    debug!("Updating focus to {:?}", koid_a);
    handles.server.update_focus(koid_a)?;

    handles.run_server_until_stalled(&mut exec);

    let (reader_b, koid_b) = handles.new_reader()?;

    debug!("reader_b.watch");
    let mut initial_watch_fut = reader_b.watch(ReaderWatchRequest::EMPTY);
    assert_matches!(
        exec.run_until_stalled(&mut initial_watch_fut),
        Poll::Ready(Ok(Err(fclip::ClipboardError::Unauthorized)))
    );

    debug!("reader_b.watch");
    let mut watch_fut = reader_b.watch(ReaderWatchRequest::EMPTY);
    assert_matches!(exec.run_until_stalled(&mut watch_fut), Poll::Pending);

    debug!("Updating focus to {:?} ", koid_b);
    assert!(handles.server.update_focus(koid_b).is_ok());

    assert_matches!(
        exec.run_until_stalled(&mut watch_fut),
        Poll::Ready(Ok(Ok(metadata))) if metadata ==
            ClipboardMetadata::with_last_modified_ns(1).into()
    );

    Ok(())
}

#[fuchsia::test(logging_minimum_severity = "debug")]
fn test_unchanged_metadata_updates_are_ignored() -> Result<()> {
    let mut exec = fasync::TestExecutor::new()?;

    let mut handles = TestHandles::new(ClipboardMetadata::with_last_modified_ns(1));

    let koid_a = make_view_ref_koid()?;
    handles.server.update_focus(koid_a)?;
    handles.run_server_until_stalled(&mut exec);

    let reader_a = handles.new_reader_with_koid(koid_a)?;

    let mut initial_watch_fut = reader_a.watch(ReaderWatchRequest::EMPTY);
    assert_matches!(
        exec.run_until_stalled(&mut initial_watch_fut),
        Poll::Ready(Ok(Ok(metadata))) if metadata ==
            ClipboardMetadata::with_last_modified_ns(1).into()
    );

    let mut watch_fut = reader_a.watch(ReaderWatchRequest::EMPTY);
    assert_matches!(exec.run_until_stalled(&mut watch_fut), Poll::Pending);

    handles.server.update_clipboard_metadata(ClipboardMetadata::with_last_modified_ns(1))?;
    assert_matches!(exec.run_until_stalled(&mut watch_fut), Poll::Pending);

    handles.server.update_clipboard_metadata(ClipboardMetadata::with_last_modified_ns(2))?;
    assert_matches!(
        exec.run_until_stalled(&mut watch_fut),
        Poll::Ready(Ok(Ok(metadata))) if metadata ==
            ClipboardMetadata::with_last_modified_ns(2).into()
    );

    Ok(())
}

#[fuchsia::test]
fn test_all_clients_are_marked_dirty_when_metadata_changes() -> Result<()> {
    const NUM_CLIENTS: usize = 5;

    let mut exec = fasync::TestExecutor::new()?;
    let mut handles = TestHandles::new(ClipboardMetadata::with_last_modified_ns(1));

    let readers_and_koids =
        (0..NUM_CLIENTS).map(|_| handles.new_reader()).collect::<Result<Vec<_>, _>>()?;

    // Swallow the initial state for each watcher.
    let expected_metadata: fclip::ClipboardMetadata =
        ClipboardMetadata::with_last_modified_ns(1).into();
    for (reader, koid) in &readers_and_koids {
        handles.server.update_focus(*koid)?;
        handles.run_server_until_stalled(&mut exec);
        let mut initial_watch_fut = reader.watch(ReaderWatchRequest::EMPTY);
        assert_matches!(
            exec.run_until_stalled(&mut initial_watch_fut),
            Poll::Ready(Ok(Ok(metadata))) if metadata == expected_metadata
        );
    }

    handles.server.update_focus(None)?;
    handles.run_server_until_stalled(&mut exec);

    let mut watch_futs = readers_and_koids
        .iter()
        .map(|(reader, _)| reader.watch(ReaderWatchRequest::EMPTY))
        .collect::<Vec<_>>();

    // Verify that all the watchers are waiting for a change in the clipboard metadata.
    for i in 0..NUM_CLIENTS {
        let koid = readers_and_koids[i].1;
        handles.server.update_focus(koid)?;
        handles.run_server_until_stalled(&mut exec);
        assert_matches!(exec.run_until_stalled(&mut watch_futs[i]), Poll::Pending);
    }

    handles.server.update_focus(None)?;
    handles.run_server_until_stalled(&mut exec);

    // Update the clipboard metadata.
    handles.server.update_clipboard_metadata(ClipboardMetadata::with_last_modified_ns(2))?;
    handles.run_server_until_stalled(&mut exec);
    for mut fut in &mut watch_futs {
        assert_matches!(exec.run_until_stalled(&mut fut), Poll::Pending);
    }

    // Verify that each watcher gets the new metadata upon gaining focus (but no earlier).
    let expected_metadata: fclip::ClipboardMetadata =
        ClipboardMetadata::with_last_modified_ns(2).into();
    for i in 0..NUM_CLIENTS {
        let koid = readers_and_koids[i].1;
        handles.server.update_focus(koid)?;
        handles.run_server_until_stalled(&mut exec);

        let mut fut = &mut watch_futs[i];
        assert_matches!(
            exec.run_until_stalled(&mut fut),
            Poll::Ready(Ok(Ok(metadata))) if metadata == expected_metadata
        );

        // The rest of the watchers haven't gotten focus yet.
        for j in (i + 1)..NUM_CLIENTS {
            let mut fut = &mut watch_futs[j];
            assert_matches!(exec.run_until_stalled(&mut fut), Poll::Pending)
        }
    }

    Ok(())
}

#[fuchsia::test(logging_minimum_severity = "debug")]
fn test_last_clipboard_state_wins() -> Result<()> {
    let mut executor = fasync::TestExecutor::new()?;

    let mut handles = TestHandles::new(ClipboardMetadata::with_last_modified_ns(1));

    let koid_a = make_view_ref_koid()?;
    handles.server.update_focus(koid_a)?;

    let _ = executor.run_until_stalled(&mut handles.server_task);

    let reader_a = handles.new_reader_with_koid(koid_a)?;

    let mut initial_watch_fut = reader_a.watch(ReaderWatchRequest::EMPTY);
    assert_matches!(
        executor.run_until_stalled(&mut initial_watch_fut),
        Poll::Ready(Ok(Ok(metadata))) if metadata ==
            ClipboardMetadata::with_last_modified_ns(1).into()
    );

    let mut watch_fut = reader_a.watch(ReaderWatchRequest::EMPTY);
    assert_matches!(executor.run_until_stalled(&mut watch_fut), Poll::Pending);

    let koid_b = make_view_ref_koid()?;
    handles.server.update_focus(koid_b)?;

    assert_matches!(executor.run_until_stalled(&mut watch_fut), Poll::Pending);

    handles.server.update_clipboard_metadata(ClipboardMetadata::with_last_modified_ns(2))?;
    handles.server.update_focus(Some(koid_a))?;
    handles.server.update_clipboard_metadata(ClipboardMetadata::with_last_modified_ns(3))?;

    // Although the client regained focus when `last_modified = 2`, by the time `notify` was
    // executed, it was already `last_modified = 3`.
    assert_matches!(
        executor.run_until_stalled(&mut watch_fut),
        Poll::Ready(Ok(Ok(metadata))) if metadata ==
            ClipboardMetadata::with_last_modified_ns(3).into()
    );

    Ok(())
}

#[fuchsia::test(logging_minimum_severity = "debug")]
fn test_last_focus_wins() -> Result<()> {
    let mut executor = fasync::TestExecutor::new()?;

    let mut handles = TestHandles::new(ClipboardMetadata::with_last_modified_ns(1));

    let (reader_a, koid_a) = handles.new_reader()?;
    let (reader_b, koid_b) = handles.new_reader()?;

    handles.server.update_focus(koid_a)?;
    let _ = executor.run_until_stalled(&mut handles.server_task);

    let mut initial_watch_fut_a = reader_a.watch(ReaderWatchRequest::EMPTY);
    assert_matches!(
        executor.run_until_stalled(&mut initial_watch_fut_a),
        Poll::Ready(Ok(Ok(metadata))) if metadata ==
            ClipboardMetadata::with_last_modified_ns(1).into()
    );

    let mut initial_watch_fut_b = reader_b.watch(ReaderWatchRequest::EMPTY);
    assert_matches!(
        executor.run_until_stalled(&mut initial_watch_fut_b),
        Poll::Ready(Ok(Err(fclip::ClipboardError::Unauthorized)))
    );

    let mut watch_fut_a = reader_a.watch(ReaderWatchRequest::EMPTY);
    assert_matches!(executor.run_until_stalled(&mut watch_fut_a), Poll::Pending);

    handles.server.update_focus(koid_b)?;

    assert_matches!(executor.run_until_stalled(&mut watch_fut_a), Poll::Pending);

    handles.server.update_clipboard_metadata(ClipboardMetadata::with_last_modified_ns(2))?;
    handles.server.update_focus(Some(koid_a))?;
    handles.server.update_focus(Some(koid_b))?;

    // Although client A briefly regained focus, it lost focus by the time `notify` was
    // executed.
    assert_matches!(executor.run_until_stalled(&mut watch_fut_a), Poll::Pending);

    let mut watch_fut_b = reader_b.watch(ReaderWatchRequest::EMPTY);
    assert_matches!(
        executor.run_until_stalled(&mut watch_fut_b),
        Poll::Ready(Ok(Ok(metadata))) if metadata ==
            ClipboardMetadata::with_last_modified_ns(2).into()
    );

    Ok(())
}

#[fuchsia::test(logging_minimum_severity = "debug")]
fn test_new_watcher_is_notified_despite_flood_of_focus_changes() -> Result<()> {
    const HERD_SIZE: usize = 10;

    let mut exec = fasync::TestExecutor::new()?;
    let mut handles = TestHandles::new(ClipboardMetadata::with_last_modified_ns(1));

    let herd_readers_and_koids =
        (0..HERD_SIZE).map(|_| handles.new_reader()).collect::<Result<Vec<_>, _>>()?;

    // Swallow the initial state for each watcher in the herd.
    for (reader, koid) in &herd_readers_and_koids {
        handles.server.update_focus(*koid)?;
        handles.run_server_until_stalled(&mut exec);
        let mut initial_watch_fut = reader.watch(ReaderWatchRequest::EMPTY);
        assert_matches!(exec.run_until_stalled(&mut initial_watch_fut), Poll::Ready(Ok(Ok(_))));
    }
    handles.server.update_focus(None)?;
    handles.run_server_until_stalled(&mut exec);

    let (target_reader, target_koid) = handles.new_reader()?;

    let mut herd_watch_futs = vec![];
    // Schedule an initial Watch request for the target client in between a bunch of Focus and
    // subsequent Watch requests, but don't give the server time to process any of them yet.
    for i in 0..(HERD_SIZE / 2) {
        let (reader, koid) = &herd_readers_and_koids[i];
        handles.server.update_focus(*koid)?;
        herd_watch_futs.push(reader.watch(fclip::ReaderWatchRequest::EMPTY));
    }
    handles.server.update_focus(target_koid)?;
    let mut target_initial_fut = target_reader.watch(fclip::ReaderWatchRequest::EMPTY);
    for i in (HERD_SIZE / 2 + 1)..HERD_SIZE {
        let (reader, koid) = &herd_readers_and_koids[i];
        handles.server.update_focus(*koid)?;
        herd_watch_futs.push(reader.watch(fclip::ReaderWatchRequest::EMPTY));
    }

    // Now wait for the target future.
    assert_matches!(
        exec.run_until_stalled(&mut target_initial_fut),
        Poll::Ready(Ok(Err(fclip::ClipboardError::Unauthorized)))
    );

    Ok(())
}

#[fuchsia::test(logging_minimum_severity = "debug")]
fn test_illegal_concurrent_watch() -> Result<()> {
    let mut executor = fasync::TestExecutor::new()?;

    let handles = TestHandles::new(ClipboardMetadata::with_last_modified_ns(1));

    let (reader_a, koid_a) = handles.new_reader()?;
    handles.server.update_focus(koid_a)?;

    {
        let mut initial_watch_fut = reader_a.watch(fclip::ReaderWatchRequest::EMPTY);
        assert_matches!(
            executor.run_until_stalled(&mut initial_watch_fut),
            Poll::Ready(Ok(Ok(metadata))) if metadata ==
                ClipboardMetadata::with_last_modified_ns(1).into()
        );
    }

    {
        let mut watch_fut_1 = reader_a.watch(fclip::ReaderWatchRequest::EMPTY);
        assert_matches!(executor.run_until_stalled(&mut watch_fut_1), Poll::Pending);

        let mut watch_fut_2 = reader_a.watch(fclip::ReaderWatchRequest::EMPTY);
        assert_matches!(
            executor.run_until_stalled(&mut watch_fut_2),
            Poll::Ready(Err(fidl::Error::ClientChannelClosed {
                status: zx::Status::BAD_STATE,
                ..
            }))
        );
        assert_matches!(
            executor.run_until_stalled(&mut watch_fut_1),
            Poll::Ready(Err(fidl::Error::ClientChannelClosed {
                status: zx::Status::BAD_STATE,
                ..
            }))
        );
    }

    // A new reader, which has not committed the sin of multiple concurrent watch requests...
    let reader_a_prime = handles.new_reader_with_koid(koid_a)?;
    let mut initial_watch_fut = reader_a_prime.watch(fclip::ReaderWatchRequest::EMPTY);
    assert_matches!(
        executor.run_until_stalled(&mut initial_watch_fut),
        Poll::Ready(Ok(Ok(metadata))) if metadata ==
            ClipboardMetadata::with_last_modified_ns(1).into()
    );

    Ok(())
}
