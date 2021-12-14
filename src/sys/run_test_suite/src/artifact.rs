// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use futures::{channel::mpsc, SinkExt};

pub enum Artifact {
    SuiteLogMessage(String),
}

impl Artifact {
    fn suite_log_message<S: Into<String>>(s: S) -> Self {
        Self::SuiteLogMessage(s.into())
    }
}

#[derive(Clone)]
pub struct ArtifactSender(mpsc::Sender<Artifact>);

impl ArtifactSender {
    pub async fn send_suite_log_msg<S: Into<String>>(
        &mut self,
        s: S,
    ) -> Result<(), mpsc::SendError> {
        self.0.send(Artifact::suite_log_message(s)).await
    }
}

impl From<mpsc::Sender<Artifact>> for ArtifactSender {
    fn from(s: mpsc::Sender<Artifact>) -> Self {
        Self(s)
    }
}
