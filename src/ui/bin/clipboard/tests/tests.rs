// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Basic integration tests for the Clipboard service.

use {
    anyhow::{ensure, format_err, Error},
    async_utils::hanging_get::client::HangingGetStream,
    clipboard_test_helpers::*,
    diagnostics_reader::{
        assert_data_tree, tree_assertion, ArchiveReader, DiagnosticsHierarchy, Inspect,
        TreeAssertion,
    },
    fidl_fuchsia_ui_clipboard as fclip, fidl_fuchsia_ui_focus as focus,
    fidl_fuchsia_ui_views::ViewRef,
    focus_chain_provider::{FocusChainProviderPublisher, FocusChainProviderRequestStreamHandler},
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_component_test::{
        Capability, ChildOptions, LocalComponentHandles, RealmBuilder, RealmInstance, Ref, Route,
    },
    fuchsia_scenic::{self as scenic, ViewRefPair},
    futures::StreamExt,
    std::time::Duration,
    tracing::*,
};

const CLIPBOARD_SERVICE_PATH: &str = "#meta/clipboard.cm";
const CLIPBOARD_SERVICE_MONIKER: &str = "clipboard";
const FOCUS_SERVER_MONIKER: &str = "focus";

type FocusKoidChainFut = fidl::client::QueryResponseFut<focus::FocusKoidChain>;
type GetFocusKoidChainFn = fn(&focus::FocusChainProviderProxy) -> FocusKoidChainFut;

struct TestHandles {
    realm: RealmInstance,
    focus_chain_publisher: FocusChainProviderPublisher,
    /// See [`TestHandles::next_focus_update`] for explanation.
    focus_stream: HangingGetStream<
        focus::FocusChainProviderProxy,
        focus::FocusKoidChain,
        GetFocusKoidChainFn,
    >,
    focus_chain_stream_handler: FocusChainProviderRequestStreamHandler,
}

fn get_focus_koid_chain(proxy: &focus::FocusChainProviderProxy) -> FocusKoidChainFut {
    proxy.watch_focus_koid_chain(focus::FocusChainProviderWatchFocusKoidChainRequest::EMPTY)
}

impl TestHandles {
    pub async fn new() -> Result<Self, Error> {
        let (focus_chain_publisher, focus_chain_stream_handler) =
            focus_chain_provider::make_publisher_and_stream_handler();

        let builder = RealmBuilder::new().await?;

        let clipboard_server = builder
            .add_child(
                CLIPBOARD_SERVICE_MONIKER,
                CLIPBOARD_SERVICE_PATH,
                ChildOptions::new().eager(),
            )
            .await?;

        let focus_chain_stream_handler_ = focus_chain_stream_handler.clone();
        let focus_chain_server = builder
            .add_local_child(
                FOCUS_SERVER_MONIKER,
                move |handles: LocalComponentHandles| {
                    info!("Started local focus chain server");
                    Box::pin(handle_focus_server(handles, focus_chain_stream_handler_.clone()))
                },
                ChildOptions::new().eager(),
            )
            .await?;

        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol::<focus::FocusChainProviderMarker>())
                    .from(&focus_chain_server)
                    .to(&clipboard_server)
                    .to(Ref::parent()),
            )
            .await?;

        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol::<fclip::FocusedWriterRegistryMarker>())
                    .capability(Capability::protocol::<fclip::FocusedReaderRegistryMarker>())
                    .from(&clipboard_server)
                    .to(Ref::parent()),
            )
            .await?;

        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                    .from(Ref::parent())
                    .to(&focus_chain_server)
                    .to(&clipboard_server),
            )
            .await?;

        let realm = builder.build().await?;

        let focus_proxy =
            realm.root.connect_to_protocol_at_exposed_dir::<focus::FocusChainProviderMarker>()?;

        {
            let expected_count = 0;
            let actual_count = focus_chain_stream_handler.subscriber_count();
            ensure!(
                actual_count == expected_count,
                "focus subscriber count: actual {}, expected {}",
                actual_count,
                expected_count
            );
        }

        let q: GetFocusKoidChainFn = get_focus_koid_chain;
        let mut focus_stream = HangingGetStream::new(focus_proxy, q);
        // Flush the initial value.
        let _ = focus_stream.next().await.transpose()?;

        {
            let expected_count = 1;
            let actual_count = focus_chain_stream_handler.subscriber_count();
            ensure!(
                actual_count == expected_count,
                "focus subscriber count: actual {}, expected {}",
                actual_count,
                expected_count
            );
        }

        let handles =
            TestHandles { realm, focus_chain_publisher, focus_stream, focus_chain_stream_handler };

        Ok(handles)
    }

    fn clipboard_moniker_for_selectors(&self) -> String {
        let child_name = self.realm.root.child_name();
        format!("realm_builder\\:{child_name}/{CLIPBOARD_SERVICE_MONIKER}")
    }

    pub async fn get_inspect_hierarchy(&self) -> Result<DiagnosticsHierarchy, Error> {
        let clipboard_moniker = self.clipboard_moniker_for_selectors();
        // This boilerplate is from
        // https://fuchsia.dev/fuchsia-src/development/diagnostics/inspect/codelab/codelab.
        ArchiveReader::new()
            .add_selector(format!("{clipboard_moniker}:root"))
            .snapshot::<Inspect>()
            .await?
            .into_iter()
            .next()
            .and_then(|result| result.payload)
            .ok_or(format_err!("expected one inspect hierarchy"))
    }

    pub async fn wait_for_inspect_state(
        &self,
        desired_state: TreeAssertion<String>,
    ) -> Result<(), Error> {
        while desired_state.run(&self.get_inspect_hierarchy().await?).is_err() {
            fasync::Timer::new(Duration::from_millis(10)).await
        }
        Ok(())
    }

    pub fn get_writer_registry(&self) -> Result<fclip::FocusedWriterRegistryProxy, Error> {
        Ok(self
            .realm
            .root
            .connect_to_protocol_at_exposed_dir::<fclip::FocusedWriterRegistryMarker>()?)
    }

    pub fn get_reader_registry(&self) -> Result<fclip::FocusedReaderRegistryProxy, Error> {
        Ok(self
            .realm
            .root
            .connect_to_protocol_at_exposed_dir::<fclip::FocusedReaderRegistryMarker>()?)
    }

    /// Sets the focus chain and waits for it to be received by a client (which is local to the
    /// test process).
    ///
    /// **Note**: See caveat under [`next_focus_update`].
    pub async fn set_focus_chain(&mut self, chain: Vec<&ViewRef>) -> Result<(), Error> {
        let chain = chain
            .into_iter()
            .map(|vr| scenic::duplicate_view_ref(vr))
            .collect::<Result<Vec<_>, _>>()?;
        let focus_chain =
            focus::FocusChain { focus_chain: Some(chain), ..focus::FocusChain::EMPTY };
        self.focus_chain_publisher.set_state_and_notify_if_changed(&focus_chain)?;
        self.next_focus_update().await?;
        Ok(())
    }

    /// Waits for the test's in-process focus chain watcher to receive its next update from calls to
    /// [`set_focus_chain`].
    ///
    /// The clipboard service is subscribed to the same `FocusChainProviderPublisher` as the
    /// `focus_stream` is.
    ///
    /// Therefore, after `next_focus_update().await` completes, there is a very high likelihood that
    /// the clipboard service's focus chain snapshot has also been updated; however, due to the lack
    /// of guaranteed ordering of messages across multiple Zircon channels, this is *not strictly
    /// guaranteed*.
    async fn next_focus_update(&mut self) -> Result<Option<focus::FocusKoidChain>, Error> {
        Ok(self.focus_stream.next().await.transpose()?)
    }

    /// Ensures that the test realm is torn down in the proper order, avoiding spurious error
    /// log messages from the clipboard service. This method should be called at the end of a
    /// successful test.
    async fn destroy(self) -> Result<(), Error> {
        Ok(self.realm.destroy().await?)
    }

    fn focus_subscriber_count(&self) -> usize {
        self.focus_chain_stream_handler.subscriber_count()
    }
}

async fn handle_focus_server(
    handles: LocalComponentHandles,
    focus_chain_stream_handler: FocusChainProviderRequestStreamHandler,
) -> Result<(), Error> {
    let mut fs = ServiceFs::new();
    fs.dir("svc").add_fidl_service(move |stream| {
        debug!("Got a new focus chain request stream");
        focus_chain_stream_handler.handle_request_stream(stream).detach();
    });
    fs.serve_connection(handles.outgoing_dir)?;
    fs.collect::<()>().await;
    debug!("Exiting handle_focus_server");
    Ok(())
}

#[fuchsia::test]
async fn test_basic_copy_paste_across_different_view_refs() -> Result<(), Error> {
    async fn inner(handles: &mut TestHandles) -> Result<(), Error> {
        let ViewRefPair { control_ref: _control_ref_a, view_ref: view_ref_a } = ViewRefPair::new()?;
        let ViewRefPair { control_ref: _control_ref_b, view_ref: view_ref_b } = ViewRefPair::new()?;

        handles.set_focus_chain(vec![&view_ref_a]).await?;

        // Wait for the clipboard service to be healthy
        handles
            .wait_for_inspect_state(tree_assertion!(
                root: contains {
                    "fuchsia.inspect.Health": contains {
                        status: "OK"
                    }
                }
            ))
            .await?;

        let writer_registry = handles.get_writer_registry()?;
        let writer_a = writer_registry.get_writer(&view_ref_a).await?;

        let reader_registry = handles.get_reader_registry()?;
        let reader_b = reader_registry.get_reader(&view_ref_b).await?;

        {
            // There should be one subscriber from the test code, and one from the clipboard
            // service.
            let expected_focus_subscriber_count = 2;
            let actual_focus_subscriber_count = handles.focus_subscriber_count();
            ensure!(
                actual_focus_subscriber_count == expected_focus_subscriber_count,
                "focus subscriber count: actual {}, expected {}",
                actual_focus_subscriber_count,
                expected_focus_subscriber_count
            );
        }

        // Just confirming that some minimal Inspect hierarchy is present.
        let inspect_hierarchy = handles.get_inspect_hierarchy().await?;
        assert_data_tree!(inspect_hierarchy, root: contains {
            clipboard: contains {
                reader_registry_client_count: 1u64,
                writer_registry_client_count: 1u64,
                reader_count: 1u64,
                writer_count: 1u64,
            }
        });

        // The scope of this test is limited to verifying that the clipboard service properly
        // exposes its FIDL services, and properly connects to the services it needs. (Semantic
        // correctness is verified by unit tests.)
        //
        // Given that scope, receiving _some_ response to the `set_item()`, `get_item()`, and
        // 'watch()` FIDL calls is sufficient.

        let _ = writer_a
            .set_item(make_clipboard_item("text/json".to_string(), "{}".to_string()))
            .await?;

        handles.set_focus_chain(vec![&view_ref_b]).await?;

        let _ = reader_b.get_item(fclip::ReaderGetItemRequest::EMPTY).await?;
        let _ = reader_b.watch(fclip::ReaderWatchRequest::EMPTY).await?;

        Ok(())
    }

    let mut handles = TestHandles::new().await?;
    let result = inner(&mut handles).await;
    handles.destroy().await?;

    result
}
