// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{Error, ResultExt},
    fidl_fuchsia_modular::{
        ExecuteResult, ExecuteStatus, PuppetMasterRequest, PuppetMasterRequestStream, StoryCommand,
        StoryPuppetMasterRequest, StoryPuppetMasterRequestStream,
    },
    fuchsia_async as fasync,
    fuchsia_syslog::macros::*,
    futures::prelude::*,
    std::collections::HashMap,
};

pub struct PuppetMasterFake<F> {
    on_execute_callbacks: HashMap<String, F>,
}

impl<F: 'static> PuppetMasterFake<F>
where
    F: Fn(&Vec<StoryCommand>) + Sync + Send,
{
    pub fn new() -> Self {
        PuppetMasterFake { on_execute_callbacks: HashMap::new() }
    }

    pub fn set_on_execute(&mut self, story_name: &str, callback: F) {
        self.on_execute_callbacks.insert(story_name.to_string(), callback);
    }

    pub fn spawn(mut self, mut stream: PuppetMasterRequestStream) {
        fasync::spawn(
            async move {
                while let Some(request) =
                    stream.try_next().await.context("error running fake puppet master")?
                {
                    match request {
                        PuppetMasterRequest::ControlStory { story_name, request, .. } => {
                            let stream = request.into_stream()?;
                            let callback = self.on_execute_callbacks.remove(&story_name);
                            StoryPuppetMasterFake::new(story_name, callback).spawn(stream);
                        }
                        _ => continue,
                    }
                }
                Ok(())
            }
            .unwrap_or_else(|e: Error| fx_log_err!("error serving fake puppet master: {:?}", e)),
        );
    }
}

pub struct StoryPuppetMasterFake<F> {
    story_name: String,
    on_execute_callback: Option<F>,
    enqueued_commands: Vec<StoryCommand>,
}

impl<F: 'static> StoryPuppetMasterFake<F>
where
    F: Fn(&Vec<StoryCommand>) + Sync + Send,
{
    fn new(story_name: String, on_execute_callback: Option<F>) -> Self {
        StoryPuppetMasterFake { story_name, on_execute_callback, enqueued_commands: vec![] }
    }

    fn spawn(mut self, mut stream: StoryPuppetMasterRequestStream) {
        fasync::spawn(
            async move {
                while let Some(request) =
                    stream.try_next().await.context("error running fake story puppet master")?
                {
                    match request {
                        StoryPuppetMasterRequest::Enqueue { commands, .. } => {
                            self.enqueued_commands = commands;
                        }
                        StoryPuppetMasterRequest::Execute { responder } => {
                            if let Some(ref callback) = self.on_execute_callback {
                                callback(&self.enqueued_commands);
                            }
                            self.enqueued_commands.clear();
                            let mut result = ExecuteResult {
                                status: ExecuteStatus::Ok,
                                story_id: Some(self.story_name.clone()),
                                error_message: None,
                            };
                            responder.send(&mut result)?;
                        }
                        _ => continue,
                    }
                }
                Ok(())
            }
            .unwrap_or_else(|e: Error| {
                fx_log_err!("error serving fake story puppet master: {:?}", e)
            }),
        );
    }
}
