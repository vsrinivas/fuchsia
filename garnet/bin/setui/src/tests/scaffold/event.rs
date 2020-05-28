// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod subscriber {
    use crate::internal::event;
    use futures::future::BoxFuture;
    use std::sync::Arc;

    type Generate = Arc<dyn Fn(event::message::Factory) -> BoxFuture<'static, ()> + Send + Sync>;

    /// This blueprint provides a way for tests to specify an asynchronous
    /// closure as the create function for an event subscriber.
    pub struct Blueprint {
        generate: Generate,
    }

    impl Blueprint {
        pub fn create(generate: Generate) -> event::subscriber::BlueprintHandle {
            Arc::new(Self { generate: generate })
        }
    }

    impl event::subscriber::Blueprint for Blueprint {
        fn create(&self, message_factory: event::message::Factory) -> BoxFuture<'static, ()> {
            (self.generate)(message_factory)
        }
    }
}
