// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl::endpoints::*;
use fidl_fuchsia_media::*;
use fidl_fuchsia_mediacodec::*;
use fuchsia_component::client;
use futures::{
    future::{self, BoxFuture},
    FutureExt,
};
use stream_processor_test::*;

pub struct EncoderFactory;

impl StreamProcessorFactory for EncoderFactory {
    fn connect_to_stream_processor(
        &self,
        stream: &dyn ElementaryStream,
        format_details_version_ordinal: u64,
    ) -> BoxFuture<'_, Result<StreamProcessorProxy>> {
        let get_encoder = || {
            let factory = client::connect_to_service::<CodecFactoryMarker>()?;
            let (encoder_client_end, encoder_request) = create_endpoints()?;
            let encoder = encoder_client_end.into_proxy()?;
            factory.create_encoder(
                CreateEncoderParams {
                    input_details: Some(stream.format_details(format_details_version_ordinal)),
                    require_hw: Some(false),
                },
                encoder_request,
            )?;
            Ok(encoder)
        };
        future::ready(get_encoder()).boxed()
    }
}
