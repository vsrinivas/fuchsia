// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {component_hub::io::Directory, component_hub::list::Component, std::path::PathBuf};

#[fuchsia_async::run_singlethreaded(test)]
async fn list() {
    let hub_path = PathBuf::from("/hub");
    let hub_dir = Directory::from_namespace(hub_path).unwrap();

    let component = Component::parse("test".to_string(), hub_dir).await.unwrap();

    assert!(!component.is_cmx);
    assert!(component.is_running);
    assert_eq!(component.name, "test");
    assert_eq!(component.children.len(), 1);

    let child = component.children.get(0).unwrap();
    assert_eq!(child.name, "foo");
    assert!(!child.is_running);
    assert!(!child.is_cmx);
    assert!(child.children.is_empty());
}
