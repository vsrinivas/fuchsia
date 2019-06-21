// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This module provides methods for controlling and responding to events from
//! a web frame.

use crate::error::{AuthProviderError, ResultExt};
use fidl::endpoints::{create_proxy, create_request_stream};
use fidl_fuchsia_auth::AuthProviderStatus;
use fidl_fuchsia_ui_views::ViewToken;
use fidl_fuchsia_web::{
    ContextProxy, FrameProxy, LoadUrlParams, NavigationControllerMarker,
    NavigationEventListenerMarker, NavigationEventListenerRequest,
    NavigationEventListenerRequestStream,
};
use futures::prelude::*;
use log::warn;
use url::Url;

type AuthProviderResult<T> = Result<T, AuthProviderError>;

/// A representation of a web frame that is the only frame in a web context.
pub struct StandaloneWebFrame {
    /// Connection to the web context.  Needs to be kept in scope to
    /// keep context alive.
    _context: ContextProxy,
    /// Connection to the web frame within the context.
    frame: FrameProxy,
}

// TODO(satsukiu): return resource errors instead of UnknownError once a
// distinct errortype exists
impl StandaloneWebFrame {
    /// Create a new `StandaloneWebFrame`.  The context and frame passed
    /// in should not be reused.
    pub fn new(context: ContextProxy, frame: FrameProxy) -> Self {
        StandaloneWebFrame { _context: context, frame }
    }

    /// Creates a new scenic view using the given |view_token| and loads the
    /// given |url| as a webpage in the view.  The view must be attached to
    /// the global Scenic graph using the ViewHolderToken paired with
    /// |view_token|.  This method should be called prior to attaching to the
    /// Scenic graph to ensure that loading is successful prior to displaying
    /// the page to the user.
    pub async fn display_url(
        &mut self,
        mut view_token: ViewToken,
        url: Url,
    ) -> AuthProviderResult<()> {
        let (navigation_controller_proxy, navigation_controller_server_end) =
            create_proxy::<NavigationControllerMarker>()
                .auth_provider_status(AuthProviderStatus::UnknownError)?;
        self.frame
            .get_navigation_controller(navigation_controller_server_end)
            .auth_provider_status(AuthProviderStatus::UnknownError)?;

        await!(navigation_controller_proxy.load_url(
            url.as_str(),
            LoadUrlParams {
                type_: None,
                referrer_url: None,
                was_user_activated: None,
                headers: None
            }
        ))
        .auth_provider_status(AuthProviderStatus::UnknownError)??;
        self.frame
            .create_view(&mut view_token)
            .auth_provider_status(AuthProviderStatus::UnknownError)?;
        // TODO(satsukiu): This method currently returns immediately after
        // requesting to load a URL, before anything is loaded.  It should
        // verify that the page is loaded before returning, possibly by
        // listening for loading events.
        Ok(())
    }

    /// Waits until the frame redirects to a URL matching the scheme,
    /// domain, and path of |redirect_target|. Returns the matching URL,
    /// including any query parameters.
    pub async fn wait_for_redirect(&mut self, redirect_target: Url) -> AuthProviderResult<Url> {
        let (navigation_event_client, navigation_event_stream) =
            create_request_stream::<NavigationEventListenerMarker>()
                .auth_provider_status(AuthProviderStatus::UnknownError)?;
        self.frame
            .set_navigation_event_listener(Some(navigation_event_client))
            .auth_provider_status(AuthProviderStatus::UnknownError)?;

        // pull redirect URL out from events.
        await!(Self::poll_for_url_navigation_event(navigation_event_stream, |url| {
            (url.scheme(), url.domain(), url.path())
                == (redirect_target.scheme(), redirect_target.domain(), redirect_target.path())
        }))
    }

    /// Polls for events on the given request stream until a event with a
    /// matching url is found.
    async fn poll_for_url_navigation_event<F>(
        mut request_stream: NavigationEventListenerRequestStream,
        url_match_fn: F,
    ) -> AuthProviderResult<Url>
    where
        F: Fn(&Url) -> bool,
    {
        // Any errors encountered with the stream here may be a result of the
        // overlay being canceled externally.
        while let Some(request) = await!(request_stream.try_next())
            .auth_provider_status(AuthProviderStatus::UnknownError)?
        {
            let NavigationEventListenerRequest::OnNavigationStateChanged { change, responder } =
                request;
            responder.send().auth_provider_status(AuthProviderStatus::UnknownError)?;
            match change.url.map(|raw_url| Url::parse(raw_url.as_str())) {
                Some(Ok(url)) => {
                    if url_match_fn(&url) {
                        return Ok(url);
                    }
                }
                Some(Err(err)) => {
                    warn!("Browser redirected to malformed URL: {:?}", &err);
                    return Err(
                        AuthProviderError::new(AuthProviderStatus::UnknownError).with_cause(err)
                    );
                }
                None => (),
            }
        }
        Err(AuthProviderError::new(AuthProviderStatus::UnknownError))
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use failure::Error;
    use fidl::endpoints::ClientEnd;
    use fidl_fuchsia_web::{
        ContextMarker, FrameMarker, FrameRequest, FrameRequestStream, NavigationState,
    };
    use fuchsia_async as fasync;
    use log::error;

    fn create_frame_with_events(events: Vec<NavigationState>) -> Result<StandaloneWebFrame, Error> {
        let (context, _) = create_proxy::<ContextMarker>()?;
        let (frame, frame_server_end) = create_request_stream::<FrameMarker>()?;
        fasync::spawn(async move {
            await!(handle_frame_stream(frame_server_end, events))
                .unwrap_or_else(|e| error!("Error running frame stream: {:?}", e));
        });
        Ok(StandaloneWebFrame::new(context, frame.into_proxy()?))
    }

    async fn handle_frame_stream(
        mut stream: FrameRequestStream,
        events: Vec<NavigationState>,
    ) -> Result<(), Error> {
        if let Some(request) = await!(stream.try_next())? {
            match request {
                FrameRequest::SetNavigationEventListener { listener, .. } => {
                    fasync::spawn(async move {
                        await!(feed_event_requests(listener.unwrap(), events))
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
            await!(client_end.on_navigation_state_changed(event))?;
        }
        Ok(())
    }

    fn create_navigation_state(url: Option<&str>) -> NavigationState {
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

    #[fasync::run_until_stalled(test)]
    async fn test_wait_for_redirect() -> Result<(), Error> {
        let events = vec![
            create_navigation_state(None),
            create_navigation_state(None),
            create_navigation_state(Some("http://test/path/")),
            create_navigation_state(Some("http://test/?key=val")),
            create_navigation_state(None),
        ];

        let mut web_frame = create_frame_with_events(events)?;
        let target_url = Url::parse("http://test/")?;
        let matched_url = await!(web_frame.wait_for_redirect(target_url))?;
        assert_eq!(matched_url, Url::parse("http://test/?key=val").unwrap());
        Ok(())
    }

    #[fasync::run_until_stalled(test)]
    async fn test_no_matching_found() -> Result<(), Error> {
        let events =
            vec![create_navigation_state(None), create_navigation_state(Some("http://domain/"))];

        let mut web_frame = create_frame_with_events(events)?;
        let target_url = Url::parse("http://test/")?;
        let result = await!(web_frame.wait_for_redirect(target_url));
        assert!(result.is_err());
        Ok(())
    }
}
