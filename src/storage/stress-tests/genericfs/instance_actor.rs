// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    async_trait::async_trait,
    fs_management::{FSConfig, Filesystem},
    storage_stress_test_utils::fvm::FvmInstance,
    stress_test::actor::{Actor, ActorError},
};

/// An actor that kills the fs instance and destroys the ramdisk
pub struct InstanceActor<FSC: FSConfig> {
    pub instance: Option<(Filesystem<FSC>, FvmInstance)>,
}

impl<FSC: FSConfig> InstanceActor<FSC> {
    pub fn new(fvm: FvmInstance, fs: Filesystem<FSC>) -> Self {
        Self { instance: Some((fs, fvm)) }
    }
}

#[async_trait]
impl<FSC: 'static + FSConfig + Send + Sync> Actor for InstanceActor<FSC> {
    async fn perform(&mut self) -> Result<(), ActorError> {
        if let Some((mut fs, _)) = self.instance.take() {
            fs.kill().expect("Could not kill fs instance");
        } else {
            panic!("Instance was already killed!")
        }
        Err(ActorError::ResetEnvironment)
    }
}
