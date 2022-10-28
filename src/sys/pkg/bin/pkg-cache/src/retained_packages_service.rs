// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{index::set_retained_index, PackageIndex},
    anyhow::Error,
    fidl::endpoints::ClientEnd,
    fidl_fuchsia_pkg::{
        BlobIdIteratorMarker, RetainedPackagesRequest, RetainedPackagesRequestStream,
    },
    futures::TryStreamExt,
    std::{collections::HashSet, sync::Arc},
};

pub async fn serve(
    package_index: Arc<async_lock::RwLock<PackageIndex>>,
    blobfs: blobfs::Client,
    stream: RetainedPackagesRequestStream,
) -> Result<(), Error> {
    stream
        .map_err(anyhow::Error::new)
        .try_for_each_concurrent(None, |event| async {
            match event {
                RetainedPackagesRequest::Replace { iterator, responder } => {
                    set_retained_index(&package_index, &blobfs, &collect_blob_ids(iterator).await?)
                        .await;

                    responder.send()?;
                }
                RetainedPackagesRequest::Clear { responder } => {
                    set_retained_index(&package_index, &blobfs, &[]).await;
                    responder.send()?;
                }
            };
            Ok(())
        })
        .await
}

async fn collect_blob_ids(
    iterator: ClientEnd<BlobIdIteratorMarker>,
) -> Result<Vec<fuchsia_hash::Hash>, Error> {
    let iterator_proxy = iterator.into_proxy()?;
    let mut ids = HashSet::new();
    loop {
        let chunk = iterator_proxy.next().await?;
        if chunk.is_empty() {
            break;
        }
        ids.extend(chunk);
    }

    Ok(ids
        .into_iter()
        .map(fidl_fuchsia_pkg_ext::BlobId::from)
        .map(fuchsia_hash::Hash::from)
        .collect::<Vec<fuchsia_hash::Hash>>())
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        assert_matches::assert_matches,
        fidl_fuchsia_pkg_ext::{serve_fidl_iterator_from_slice, BlobId},
        fuchsia_hash::Hash,
        futures::Future,
    };

    const ZEROES_HASH: &str = "0000000000000000000000000000000000000000000000000000000000000000";
    const ONES_HASH: &str = "1111111111111111111111111111111111111111111111111111111111111111";

    fn serve_iterator(
        packages: Vec<fidl_fuchsia_pkg::BlobId>,
    ) -> Result<(impl Future<Output = ()>, ClientEnd<BlobIdIteratorMarker>), Error> {
        let (iterator_client_end, iterator_stream) =
            fidl::endpoints::create_request_stream::<BlobIdIteratorMarker>()?;
        Ok((
            async { serve_fidl_iterator_from_slice(iterator_stream, packages).await.unwrap() },
            iterator_client_end,
        ))
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn collect_blob_ids_test() -> Result<(), Error> {
        let blob_ids = vec![BlobId::parse(ZEROES_HASH)?, BlobId::parse(ONES_HASH)?]
            .into_iter()
            .map(Into::into)
            .collect();
        let (iterator_fut, iterator_client_end) = serve_iterator(blob_ids)?;

        let (hashes, serve_iterator_result) =
            futures::join!(collect_blob_ids(iterator_client_end), iterator_fut);
        assert_matches!(serve_iterator_result, ());

        let mut hashes = hashes?;
        hashes.sort();

        assert_eq!(
            hashes,
            [Hash::from(BlobId::parse(ZEROES_HASH)?), Hash::from(BlobId::parse(ONES_HASH)?)]
        );
        Ok(())
    }
}
