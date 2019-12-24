// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This module provides methods for controlling and responding to events from
//! a web frame.

use crate::error::{ResultExt, TokenProviderError};
use async_trait::async_trait;
use fidl::endpoints::{create_proxy, create_request_stream};
use fidl_fuchsia_identity_external::Error as ApiError;
use fidl_fuchsia_ui_views::ViewToken;
use fidl_fuchsia_web::{
    ContextProxy, FrameProxy, LoadUrlParams, NavigationControllerMarker,
    NavigationEventListenerMarker, NavigationEventListenerRequest,
    NavigationEventListenerRequestStream, PageType,
};
use futures::prelude::*;
use log::warn;
use url::Url;

type TokenProviderResult<T> = Result<T, TokenProviderError>;

/// A trait for representations of a web frame that is the only frame in a web context.
#[async_trait]
pub trait StandaloneWebFrame {
    /// Creates a new scenic view using the given |view_token| and loads the
    /// given |url| as a webpage in the view.  The view must be attached to
    /// the global Scenic graph using the ViewHolderToken paired with
    /// |view_token|.  This method should be called prior to attaching to the
    /// Scenic graph to ensure that loading is successful prior to displaying
    /// the page to the user.
    async fn display_url(&mut self, view_token: ViewToken, url: Url) -> TokenProviderResult<()>;

    /// Waits until the frame redirects to a URL matching the scheme,
    /// domain, and path of |redirect_target|. Returns the matching URL,
    /// including any query parameters.
    async fn wait_for_redirect(&mut self, redirect_target: Url) -> TokenProviderResult<Url>;
}

/// A `StandaloneWebFrame` implementation that uses the default fuchsia.web
/// implementation to display a web frame.
pub struct DefaultStandaloneWebFrame {
    /// Connection to the web context.  Needs to be kept in scope to
    /// keep context alive.
    _context: ContextProxy,
    /// Connection to the web frame within the context.
    frame: FrameProxy,
}

#[async_trait]
impl StandaloneWebFrame for DefaultStandaloneWebFrame {
    async fn display_url(
        &mut self,
        mut view_token: ViewToken,
        url: Url,
    ) -> TokenProviderResult<()> {
        let (navigation_controller_proxy, navigation_controller_server_end) =
            create_proxy::<NavigationControllerMarker>()
                .token_provider_error(ApiError::Resource)?;
        self.frame
            .get_navigation_controller(navigation_controller_server_end)
            .token_provider_error(ApiError::Resource)?;

        navigation_controller_proxy
            .load_url(
                url.as_str(),
                LoadUrlParams {
                    type_: None,
                    referrer_url: None,
                    was_user_activated: None,
                    headers: None,
                },
            )
            .await
            .token_provider_error(ApiError::Resource)??;
        self.frame.create_view(&mut view_token).token_provider_error(ApiError::Resource)?;

        let navigation_event_stream = self.get_navigation_event_stream()?;
        Self::poll_until_loaded(navigation_event_stream).await
    }

    async fn wait_for_redirect(&mut self, redirect_target: Url) -> TokenProviderResult<Url> {
        let navigation_event_stream = self.get_navigation_event_stream()?;

        // pull redirect URL out from events.
        Self::poll_for_url_navigation_event(navigation_event_stream, |url| {
            (url.scheme(), url.domain(), url.path())
                == (redirect_target.scheme(), redirect_target.domain(), redirect_target.path())
        })
        .await
    }
}

impl DefaultStandaloneWebFrame {
    /// Create a new `StandaloneWebFrame`.  The context and frame passed
    /// in should not be reused.
    pub fn new(context: ContextProxy, frame: FrameProxy) -> Self {
        DefaultStandaloneWebFrame { _context: context, frame }
    }

    /// Registers a navigation listener with the web frame and returns the created event
    /// stream.
    fn get_navigation_event_stream(
        &self,
    ) -> TokenProviderResult<NavigationEventListenerRequestStream> {
        let (navigation_event_client, navigation_event_stream) =
            create_request_stream::<NavigationEventListenerMarker>()
                .token_provider_error(ApiError::Resource)?;
        self.frame
            .set_navigation_event_listener(Some(navigation_event_client))
            .token_provider_error(ApiError::Resource)?;
        Ok(navigation_event_stream)
    }

    /// Polls for events on the given request stream until a event with a
    /// matching url is found.
    async fn poll_for_url_navigation_event<F>(
        mut request_stream: NavigationEventListenerRequestStream,
        url_match_fn: F,
    ) -> TokenProviderResult<Url>
    where
        F: Fn(&Url) -> bool,
    {
        // Any errors encountered with the stream here may be a result of the
        // overlay being canceled externally.
        while let Some(request) =
            request_stream.try_next().await.token_provider_error(ApiError::Resource)?
        {
            let NavigationEventListenerRequest::OnNavigationStateChanged { change, responder } =
                request;
            responder.send().token_provider_error(ApiError::Resource)?;
            match change.url.map(|raw_url| Url::parse(raw_url.as_str())) {
                Some(Ok(url)) => {
                    if url_match_fn(&url) {
                        return Ok(url);
                    }
                }
                Some(Err(err)) => {
                    warn!("Browser redirected to malformed URL: {:?}", &err);
                    return Err(TokenProviderError::new(ApiError::Unknown).with_cause(err));
                }
                None => (),
            }
        }
        Err(TokenProviderError::new(ApiError::Unknown))
    }

    /// Completes when the frame has finished loading or some error has occurred.
    async fn poll_until_loaded(
        mut request_stream: NavigationEventListenerRequestStream,
    ) -> TokenProviderResult<()> {
        // Verify that the page has loaded and is not an error page.  Since
        // this information may be delivered through two different events,
        // we need to keep track of the known state and search through events
        // until both points are found.
        let mut known_page_type: Option<PageType> = None;
        let mut main_document_loaded = false;
        while let Some(request) =
            request_stream.try_next().await.token_provider_error(ApiError::Resource)?
        {
            // update known state.
            let NavigationEventListenerRequest::OnNavigationStateChanged { change, responder } =
                request;
            responder.send().token_provider_error(ApiError::Resource)?;

            if let Some(is_main_document_loaded) = change.is_main_document_loaded {
                main_document_loaded = is_main_document_loaded;
            }
            if let Some(page_type) = change.page_type {
                known_page_type.replace(page_type);
            }

            // check if state is terminal
            match (known_page_type, main_document_loaded) {
                (Some(PageType::Normal), true) => return Ok(()),
                (Some(PageType::Normal), false) => (),
                (Some(PageType::Error), _) => {
                    return Err(TokenProviderError::new(ApiError::Network))
                }
                (None, _) => (),
            }
        }
        Err(TokenProviderError::new(ApiError::Unknown))
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use anyhow::Error;
    use fidl::endpoints::ClientEnd;
    use fidl_fuchsia_web::{
        ContextMarker, FrameMarker, FrameRequest, FrameRequestStream, NavigationState,
    };
    use fuchsia_async as fasync;
    use log::error;

    fn create_frame_with_events(
        events: Vec<NavigationState>,
    ) -> Result<DefaultStandaloneWebFrame, Error> {
        let (context, _) = create_proxy::<ContextMarker>()?;
        let (frame, frame_server_end) = create_request_stream::<FrameMarker>()?;
        fasync::spawn(async move {
            handle_frame_stream(frame_server_end, events)
                .await
                .unwrap_or_else(|e| error!("Error running frame stream: {:?}", e));
        });
        Ok(DefaultStandaloneWebFrame::new(context, frame.into_proxy()?))
    }

    async fn handle_frame_stream(
        mut stream: FrameRequestStream,
        events: Vec<NavigationState>,
    ) -> Result<(), Error> {
        if let Some(request) = stream.try_next().await? {
            match request {
                FrameRequest::SetNavigationEventListener { listener, .. } => {
                    fasync::spawn(async move {
                        feed_event_requests(listener.unwrap(), events)
                            .await
                            .unwrap_or_else(|e| error!("Error in event sender: {:?}", e));
                    });
                }
                req => panic!("Unimplemented method {:?} called in test stub", req),
            }
        }
        Ok(())
    }

    async fn feed_event_requests(
        client_end: ClientEnd<NavigationEventListenerMarker>,
        events: Vec<NavigationState>,
    ) -> Result<(), Error> {
        let client_end = client_end.into_proxy()?;
        for event in events.into_iter() {
            client_end.on_navigation_state_changed(event).await?;
        }
        Ok(())
    }

    fn create_navigate_to_url_event(url: Option<&str>) -> NavigationState {
        let parsed_url = url.map(|url| String::from(url));
        NavigationState {
            url: parsed_url,
            title: None,
            page_type: None,
            can_go_forward: None,
            can_go_back: None,
            is_main_document_loaded: None,
        }
    }

    fn create_navigate_to_page_event(
        page_type: Option<PageType>,
        is_main_document_loaded: Option<bool>,
    ) -> NavigationState {
        NavigationState {
            url: None,
            title: None,
            page_type,
            can_go_forward: None,
            can_go_back: None,
            is_main_document_loaded,
        }
    }

    #[fasync::run_until_stalled(test)]
    async fn test_wait_for_redirect() -> Result<(), Error> {
        let events = vec![
            create_navigate_to_url_event(None),
            create_navigate_to_url_event(None),
            create_navigate_to_url_event(Some("http://test/path/")),
            create_navigate_to_url_event(Some("http://test/?key=val")),
            create_navigate_to_url_event(None),
        ];

        let mut web_frame = create_frame_with_events(events)?;
        let target_url = Url::parse("http://test/")?;
        let matched_url = web_frame.wait_for_redirect(target_url).await?;
        assert_eq!(matched_url, Url::parse("http://test/?key=val").unwrap());
        Ok(())
    }

    #[fasync::run_until_stalled(test)]
    async fn test_no_matching_redirect_found() -> Result<(), Error> {
        let events = vec![
            create_navigate_to_url_event(None),
            create_navigate_to_url_event(Some("http://domain/")),
        ];

        let mut web_frame = create_frame_with_events(events)?;
        let target_url = Url::parse("http://test/")?;
        let result = web_frame.wait_for_redirect(target_url).await;
        assert!(result.is_err());
        Ok(())
    }

    #[fasync::run_until_stalled(test)]
    async fn test_poll_until_loaded() -> Result<(), Error> {
        let events = vec![
            create_navigate_to_page_event(None, None),
            create_navigate_to_page_event(Some(PageType::Normal), Some(true)),
        ];

        let web_frame = create_frame_with_events(events)?;
        let stream = web_frame.get_navigation_event_stream()?;
        assert!(DefaultStandaloneWebFrame::poll_until_loaded(stream).await.is_ok());

        // Verify functionality when pagetype and document_loaded events sent
        // separately
        let events = vec![
            create_navigate_to_page_event(None, None),
            create_navigate_to_page_event(Some(PageType::Normal), None),
            create_navigate_to_page_event(None, Some(true)),
        ];

        let web_frame = create_frame_with_events(events)?;
        let stream = web_frame.get_navigation_event_stream()?;
        assert!(DefaultStandaloneWebFrame::poll_until_loaded(stream).await.is_ok());
        Ok(())
    }

    #[fasync::run_until_stalled(test)]
    async fn test_poll_until_loaded_network_error() -> Result<(), Error> {
        let events = vec![
            create_navigate_to_page_event(None, None),
            create_navigate_to_page_event(None, Some(true)),
            create_navigate_to_page_event(Some(PageType::Error), None),
        ];

        let web_frame = create_frame_with_events(events)?;
        let stream = web_frame.get_navigation_event_stream()?;
        assert_eq!(
            DefaultStandaloneWebFrame::poll_until_loaded(stream).await.unwrap_err().api_error,
            ApiError::Network
        );
        Ok(())
    }

    #[fasync::run_until_stalled(test)]
    async fn test_poll_until_loaded_stream_closed() -> Result<(), Error> {
        let events = vec![
            create_navigate_to_page_event(None, None),
            create_navigate_to_page_event(Some(PageType::Normal), None),
        ];

        let web_frame = create_frame_with_events(events)?;
        let stream = web_frame.get_navigation_event_stream()?;
        assert_eq!(
            DefaultStandaloneWebFrame::poll_until_loaded(stream).await.unwrap_err().api_error,
            ApiError::Unknown
        );
        Ok(())
    }
}

#[cfg(test)]
pub mod mock {
    use super::*;

    /// A mock implementation of StandaloneWebFrame that always returns the responses
    /// specified during its creation.
    pub struct TestWebFrame {
        display_url_response: TokenProviderResult<()>,
        wait_for_redirect_response: TokenProviderResult<Url>,
    }

    impl TestWebFrame {
        pub fn new(
            display_url_response: TokenProviderResult<()>,
            wait_for_redirect_response: TokenProviderResult<Url>,
        ) -> Self {
            TestWebFrame { display_url_response, wait_for_redirect_response }
        }
    }

    #[async_trait]
    impl StandaloneWebFrame for TestWebFrame {
        async fn display_url(
            &mut self,
            _view_token: ViewToken,
            _url: Url,
        ) -> TokenProviderResult<()> {
            // manual clone since TokenProviderError.cause is !Clone
            match &self.display_url_response {
                Ok(()) => Ok(()),
                Err(err) => Err(TokenProviderError::new(err.api_error)),
            }
        }

        async fn wait_for_redirect(&mut self, _redirect_target: Url) -> TokenProviderResult<Url> {
            // manual clone since TokenProviderError.cause is !Clone
            match &self.wait_for_redirect_response {
                Ok(url) => Ok(url.clone()),
                Err(err) => Err(TokenProviderError::new(err.api_error)),
            }
        }
    }

    mod test {
        use super::*;
        use fuchsia_async as fasync;
        use fuchsia_scenic::ViewTokenPair;

        #[fasync::run_until_stalled(test)]
        async fn test_web_frame_test() {
            let mut test_web_frame =
                TestWebFrame::new(Ok(()), Err(TokenProviderError::new(ApiError::Internal)));
            let ViewTokenPair { view_token, view_holder_token: _ } = ViewTokenPair::new().unwrap();
            assert!(test_web_frame
                .display_url(view_token, Url::parse("http://example.com").unwrap())
                .await
                .is_ok());

            assert_eq!(
                test_web_frame
                    .wait_for_redirect(Url::parse("http://example.com").unwrap())
                    .await
                    .unwrap_err()
                    .api_error,
                ApiError::Internal
            );
        }
    }
}
