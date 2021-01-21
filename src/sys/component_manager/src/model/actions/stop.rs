// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{component::ComponentInstance, error::ModelError},
    std::sync::Arc,
};

pub(super) async fn do_stop(component: &Arc<ComponentInstance>) -> Result<(), ModelError> {
    component.stop_instance(false).await
}
