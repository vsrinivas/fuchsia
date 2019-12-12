// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// dummy main. We do not copy this binary to fuchsia, only tests.
fn main() {}

#[cfg(test)]
mod tests {
    use failure::ResultExt;
    use fidl::endpoints::create_proxy;
    use fidl_fuchsia_diagnostics;
    use fuchsia_async as fasync;
    use fuchsia_component::client::connect_to_service;

    #[fasync::run_singlethreaded(test)]
    async fn test_all_reader_endpoints() {
        let archive_accessor =
            connect_to_service::<fidl_fuchsia_diagnostics::ArchiveMarker>().unwrap();

        let (archive_consumer, reader_server) = create_proxy().unwrap();
        let empty_iter = vec![];

        archive_accessor
            .read_inspect(reader_server, &mut empty_iter.into_iter())
            .await
            .context("setting up a reader")
            .expect("fidl should be fine")
            .expect("reader should start serving without issue");

        let (result_consumer, batch_iterator_server) = create_proxy().unwrap();
        let format = fidl_fuchsia_diagnostics::Format::Json;
        let format_response =
            archive_consumer.get_snapshot(format, batch_iterator_server).await.unwrap();
        let initial_batch_result = result_consumer.get_next().await.unwrap();

        // No point reading the returned data yet since this test is hermetic and
        // doesn't generate any /hub information for the inspect service to read.
        assert!(format_response.is_ok());
        assert!(initial_batch_result.is_ok());
    }
}
