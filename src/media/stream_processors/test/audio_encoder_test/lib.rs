// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro)]
#![cfg(test)]

mod pcm_audio;

use std::rc::Rc;
use stream_processor_encoder_factory::*;
use stream_processor_test::*;

#[::fuchsia_async::run_singlethreaded]
#[test]
async fn test_suite() -> std::result::Result<(), ::failure::Error> {
    // TODO(turnage): Add AAC encoder test
    let spec = TestSpec {
        cases: vec![],
        relation: CaseRelation::Concurrent,
        stream_processor_factory: Rc::new(EncoderFactory),
    };

    await!(spec.run())
}
