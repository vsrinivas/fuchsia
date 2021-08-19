// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result, async_trait::async_trait, fidl_fuchsia_developer_bridge as bridge,
    futures_lite::stream::StreamExt, services::prelude::*,
};

#[ffx_service]
#[derive(Default)]
pub struct TargetCollectionService {}

#[async_trait(?Send)]
impl FidlService for TargetCollectionService {
    type Service = bridge::TargetCollectionMarker;
    type StreamHandler = FidlStreamHandler<Self>;

    async fn handle(&self, cx: &Context, req: bridge::TargetCollectionRequest) -> Result<()> {
        let target_collection = cx.get_target_collection().await?;
        match req {
            bridge::TargetCollectionRequest::ListTargets { iterator, responder, query } => {
                let mut stream = iterator.into_stream()?;
                let targets = match query.as_ref().map(|s| s.as_str()) {
                    None | Some("") => target_collection
                        .targets()
                        .into_iter()
                        .filter_map(
                            |t| if t.is_connected() { Some(t.as_ref().into()) } else { None },
                        )
                        .collect::<Vec<bridge::Target>>(),
                    q => match target_collection.get_connected(q) {
                        Some(t) => vec![t.as_ref().into()],
                        None => vec![],
                    },
                };
                fuchsia_async::Task::local(async move {
                    // This was chosen arbitrarily. It's possible to determine a
                    // better chunk size using some FIDL constant math.
                    const TARGET_CHUNK_SIZE: usize = 20;
                    let mut iter = targets.into_iter();
                    while let Ok(Some(bridge::TargetCollectionIteratorRequest::GetNext {
                        responder,
                    })) = stream.try_next().await
                    {
                        let _ = responder
                            .send(
                                &mut iter
                                    .by_ref()
                                    .take(TARGET_CHUNK_SIZE)
                                    .collect::<Vec<_>>()
                                    .into_iter(),
                            )
                            .map_err(|e| {
                                log::warn!("responding to target collection iterator: {:?}", e)
                            });
                    }
                })
                .detach();
                responder.send().map_err(Into::into)
            }
        }
    }
}
