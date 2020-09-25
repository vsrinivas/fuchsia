// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::Result;
use async_helpers::stream::StreamMap;
use fidl::endpoints::create_request_stream;
use fidl_fuchsia_media::*;
use fuchsia_syslog::fx_log_warn;
use futures::stream::{FusedStream, Stream};
use std::{
    pin::Pin,
    task::{Context, Poll},
};

const LOG_TAG: &str = "interrupter";

/// Reports interruptions of audio usages.
pub struct Interrupter {
    usage_reporter: UsageReporterProxy,
    usage_watcher_requests: StreamMap<AudioRenderUsage, UsageWatcherRequestStream>,
}

impl Interrupter {
    pub fn new(usage_reporter: UsageReporterProxy) -> Self {
        Self { usage_reporter, usage_watcher_requests: StreamMap::empty() }
    }

    pub async fn watch_usage(&mut self, usage: AudioRenderUsage) -> Result<()> {
        if self.usage_watcher_requests.contains_key(&usage) {
            return Ok(());
        }

        let (usage_watcher, usage_watcher_requests) = create_request_stream()?;
        self.usage_reporter.watch(&mut Usage::RenderUsage(usage), usage_watcher)?;

        self.usage_watcher_requests.insert(usage, usage_watcher_requests);
        Ok(())
    }
}

#[derive(Debug, PartialEq)]
pub struct Interruption {
    pub usage: AudioRenderUsage,
    pub stage: InterruptionStage,
}

#[derive(Debug, PartialEq)]
pub enum InterruptionStage {
    Begin,
    End,
}

impl Stream for Interrupter {
    type Item = Interruption;
    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        let next_report = Pin::new(&mut self.usage_watcher_requests).poll_next(cx);
        let next_report = match next_report {
            Poll::Ready(Some(report)) => report,
            _ => return Poll::Pending,
        };

        let (usage, state) = match next_report.map_err(anyhow::Error::from).and_then(
            |UsageWatcherRequest::OnStateChanged { usage, state, responder }| {
                responder.send()?;
                Ok((usage, state))
            },
        ) {
            Ok(state_change) => state_change,
            Err(e) => {
                fx_log_warn!(tag: LOG_TAG, "Audio policy service died: {:?}", e);
                return Poll::Pending;
            }
        };

        let usage = match usage {
            Usage::RenderUsage(usage) => usage,
            _ => {
                fx_log_warn!(
                    tag: LOG_TAG,
                    concat!(
                        "Audio policy service sent a capture usage state change; ",
                        "we only subscribe to renderer usage state changes."
                    )
                );
                return Poll::Pending;
            }
        };

        let stage = match state {
            UsageState::Muted(_) | UsageState::Ducked(_) => InterruptionStage::Begin,
            UsageState::Unadjusted(_) => InterruptionStage::End,
            UsageState::__UnknownVariant { .. } => {
                fx_log_warn!(tag: LOG_TAG, "Audio policy service sent unknown UsageState variant");
                return Poll::Pending;
            }
        };

        Poll::Ready(Some(Interruption { usage, stage }))
    }
}

impl FusedStream for Interrupter {
    fn is_terminated(&self) -> bool {
        false
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use fuchsia_async as fasync;
    use futures::{
        future,
        stream::{StreamExt, TryStreamExt},
    };
    use matches::assert_matches;

    fn test_interrupter() -> (Interrupter, UsageReporterRequestStream) {
        let (usage_reporter, usage_reporter_requests) =
            create_request_stream::<UsageReporterMarker>()
                .expect("Creating usage reporter channel");

        let usage_reporter = usage_reporter.into_proxy().expect("Creating usage reporter proxy");
        let interrupter = Interrupter::new(usage_reporter);

        (interrupter, usage_reporter_requests)
    }

    #[fasync::run_singlethreaded]
    #[test]
    async fn pends_when_empty() {
        let (mut interrupter, _usage_reporter_requests) = test_interrupter();

        let next = interrupter.next();
        assert_matches!(futures::poll!(next), Poll::Pending);
    }

    #[fasync::run_singlethreaded]
    #[test]
    async fn reports_interruption_on_multiple_watchers() {
        let (mut interrupter, mut usage_reporter_requests) = test_interrupter();

        interrupter.watch_usage(AudioRenderUsage::Media).await.expect("Watching media usage");
        let (mut usage, media_watcher, _) = usage_reporter_requests
            .try_next()
            .await
            .expect("Reading usage reporter request")
            .expect("Reading Ok variant of request stream element")
            .into_watch()
            .expect("Reading watch request");
        assert_matches!(usage, Usage::RenderUsage(AudioRenderUsage::Media));
        let media_watcher = media_watcher.into_proxy().expect("Creating media watcher proxy");
        let media_send_fut = media_watcher
            .on_state_changed(&mut usage, &mut UsageState::Muted(UsageStateMuted::empty()));

        let (send, interruption) = future::join(media_send_fut, interrupter.next()).await;
        send.expect("Sending mute event to interrupter");
        assert_matches!(
            interruption,
            Some(Interruption { usage: AudioRenderUsage::Media, stage: InterruptionStage::Begin })
        );

        interrupter.watch_usage(AudioRenderUsage::Background).await.expect("Watching media usage");
        let (mut usage, background_watcher, _) = usage_reporter_requests
            .try_next()
            .await
            .expect("Reading usage reporter request")
            .expect("Reading Ok variant of request stream element")
            .into_watch()
            .expect("Reading watch request");
        assert_matches!(usage, Usage::RenderUsage(AudioRenderUsage::Background));
        let background_watcher =
            background_watcher.into_proxy().expect("Creating background watcher proxy");
        let background_send_fut = background_watcher.on_state_changed(
            &mut usage,
            &mut UsageState::Unadjusted(UsageStateUnadjusted::empty()),
        );

        let (send, interruption) = future::join(background_send_fut, interrupter.next()).await;
        send.expect("Sending mute event to interrupter");
        assert_matches!(
            interruption,
            Some(Interruption {
                usage: AudioRenderUsage::Background,
                stage: InterruptionStage::End
            })
        );
    }

    #[fasync::run_singlethreaded]
    #[test]
    async fn reports_interruption() {
        let (mut interrupter, mut usage_reporter_requests) = test_interrupter();

        interrupter.watch_usage(AudioRenderUsage::Media).await.expect("Watching media usage");
        let (mut usage, watcher, _) = usage_reporter_requests
            .try_next()
            .await
            .expect("Reading usage reporter request")
            .expect("Reading Ok variant of request stream element")
            .into_watch()
            .expect("Reading watch request");
        assert_matches!(usage, Usage::RenderUsage(AudioRenderUsage::Media));

        let watcher = watcher.into_proxy().expect("Creating watcher proxy");

        let send_fut =
            watcher.on_state_changed(&mut usage, &mut UsageState::Muted(UsageStateMuted::empty()));

        let (send, interruption) = future::join(send_fut, interrupter.next()).await;
        send.expect("Sending mute event to interrupter");
        assert_matches!(
            interruption,
            Some(Interruption { usage: AudioRenderUsage::Media, stage: InterruptionStage::Begin })
        );

        let send_fut = watcher.on_state_changed(
            &mut usage,
            &mut UsageState::Unadjusted(UsageStateUnadjusted::empty()),
        );

        let (send, interruption) = future::join(send_fut, interrupter.next()).await;
        send.expect("Sending mute event to interrupter");
        assert_matches!(
            interruption,
            Some(Interruption { usage: AudioRenderUsage::Media, stage: InterruptionStage::End })
        );
    }
}
