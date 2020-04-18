// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fidl_benchmarks::BindingsUnderTestMarker, fidl_fidl_benchmarks::BindingsUnderTestProxy,
    fidl_fuchsia_sys::LauncherProxy, fuchsia_component::client::launch,
    fuchsia_component::client::App, std::sync::Arc,
};

pub struct BindingConfig {
    pub name: &'static str,
    pub url: &'static str,
}

impl BindingConfig {
    pub fn launch(&self, launcher: &LauncherProxy) -> LaunchedBinding {
        println!("Launching {} from {}", self.name, self.url);
        let app = launch(&launcher, self.url.to_string(), None).unwrap();
        let proxy = app.connect_to_service::<BindingsUnderTestMarker>().unwrap();
        LaunchedBinding {
            name: self.name,
            url: self.url,
            proxy: Arc::new(proxy),
            app: Arc::new(app),
        }
    }
}

#[derive(Clone)]
pub struct LaunchedBinding {
    pub name: &'static str,
    pub url: &'static str,
    pub proxy: Arc<BindingsUnderTestProxy>,
    pub app: Arc<App>,
}
