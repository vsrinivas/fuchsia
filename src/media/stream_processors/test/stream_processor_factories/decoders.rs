// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl::encoding::Decodable;
use fidl::endpoints::*;
use fidl_fuchsia_media::*;
use fidl_fuchsia_mediacodec::*;
use fuchsia_component::client;
use futures::{
    future::{self, BoxFuture},
    FutureExt,
};
use stream_processor_test::*;

pub struct DecoderFactory;

impl StreamProcessorFactory for DecoderFactory {
    fn connect_to_stream_processor(
        &self,
        stream: &dyn ElementaryStream,
        format_details_version_ordinal: u64,
    ) -> BoxFuture<'_, Result<StreamProcessorProxy>> {
        let get_decoder = || {
            let factory = client::connect_to_service::<CodecFactoryMarker>()?;
            let (decoder_client_end, decoder_request) = create_endpoints()?;
            let decoder = decoder_client_end.into_proxy()?;
            // TODO(turnage): Account for all error reporting methods in the
            // runner options and output.
            factory.create_decoder(
                CreateDecoderParams {
                    input_details: Some(stream.format_details(format_details_version_ordinal)),
                    promise_separate_access_units_on_input: Some(stream.is_access_units()),
                    permit_lack_of_split_header_handling: Some(true),
                    ..CreateDecoderParams::new_empty()
                },
                decoder_request,
            )?;
            Ok(decoder)
        };
        future::ready(get_decoder()).boxed()
    }
}
