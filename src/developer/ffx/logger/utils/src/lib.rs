// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_developer_remotecontrol::{ArchiveIteratorGetNextResult, ArchiveIteratorProxy},
    futures::stream::{FusedStream, FuturesOrdered, ReadyChunks, Stream, StreamExt},
    std::future::Future,
    std::pin::Pin,
    std::task::Poll,
};

pub mod symbolizer;

pub struct OrderedBatchPipeline<'a, T> {
    max_size: usize,
    pipeline: ReadyChunks<FuturesOrdered<Box<dyn Future<Output = T> + Send + Unpin + 'a>>>,
}

impl<'a, T> OrderedBatchPipeline<'a, T> {
    pub fn new(max_size: usize) -> Self {
        Self { max_size, pipeline: FuturesOrdered::new().ready_chunks(max_size) }
    }

    pub fn full(&self) -> bool {
        self.pipeline.get_ref().len() == self.max_size
    }

    pub fn push<Fut: 'a>(&mut self, fut: Fut)
    where
        Fut: Future<Output = T> + Unpin + Send + 'a,
    {
        self.pipeline.get_mut().push(Box::new(fut));
    }
}
impl<T> Stream for OrderedBatchPipeline<'_, T> {
    type Item = Vec<T>;

    fn poll_next(
        mut self: std::pin::Pin<&mut Self>,
        cx: &mut futures::task::Context<'_>,
    ) -> Poll<Option<Self::Item>> {
        Pin::new(&mut self.pipeline).poll_next(cx)
    }
}

impl<T> FusedStream for OrderedBatchPipeline<'_, T> {
    fn is_terminated(&self) -> bool {
        self.pipeline.is_terminated()
    }
}

/// This function a) fills the logging pipeline with requests and
/// b) pulls ready requests off. It will filter fidl::Error's out of
/// the results from the pipeline, with the exception of ClientChannelClosed
/// errors, which are considered terminal errors and are returned separately
/// so that the calling loop can exit.
pub async fn run_logging_pipeline(
    pipeline: &mut OrderedBatchPipeline<'_, Result<ArchiveIteratorGetNextResult, fidl::Error>>,
    proxy: &ArchiveIteratorProxy,
) -> (Vec<ArchiveIteratorGetNextResult>, Option<fidl::Error>) {
    while !pipeline.full() {
        pipeline.push(proxy.get_next());
    }
    let results = pipeline.select_next_some().await;

    // Check for a terminal error
    let terminal_err = get_peer_closed(&results);

    let ok_logs: Vec<fidl_fuchsia_developer_remotecontrol::ArchiveIteratorGetNextResult> = results
        .into_iter()
        .filter_map(|r| {
            if let Err(ref e) = r {
                log::warn!("got an error running logging pipeline {:?}", e);
            }
            r.ok()
        })
        .collect();
    (ok_logs, terminal_err)
}

pub fn get_peer_closed(
    results: &Vec<Result<ArchiveIteratorGetNextResult, fidl::Error>>,
) -> Option<fidl::Error> {
    for result in results.iter() {
        match result {
            Err(fidl::Error::ClientChannelClosed { status, protocol_name }) => {
                return Some(fidl::Error::ClientChannelClosed {
                    status: *status,
                    protocol_name: *protocol_name,
                })
            }
            _ => {}
        }
    }

    return None;
}
