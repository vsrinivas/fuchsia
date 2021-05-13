// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
extern crate proc_macro;

use {
    services::FidlService,
    fidl::endpoints::DiscoverableService,
    proc_macro::TokenStream,
    quote::quote,
    std::collections::HashMap,
    std::collections::hash_map::Entry
};

#[proc_macro]
pub fn generate_service_map(_item: TokenStream) -> TokenStream {
    let mut map = HashMap::<&str, &str>::new();
    {% for dep in deps %}
    let {{ dep.lib }}_name = <<{{ dep.lib }}::ServiceType as FidlService>::Service as DiscoverableService>::SERVICE_NAME;
    match map.entry({{ dep.lib }}_name) {
        Entry::Occupied(e) => {
            panic!("Service endpoint for {} already registered with library at {}", {{ dep.lib }}_name, e.get());
        }
        Entry::Vacant(i) => i.insert("{{ dep.target }}"),
    };
    {% endfor %}
    let res = quote! {
        {
            use fidl::endpoints::DiscoverableService;
            use services::{FidlStreamHandler, FidlService};
            use services::NameToStreamHandlerMap;
            let mut map = NameToStreamHandlerMap::new();
            {% for dep in deps %}
            map.insert(
                <<{{ dep.lib }}::ServiceType as FidlService>::Service as DiscoverableService>::SERVICE_NAME.to_owned(),
                Box::new(<{{dep.lib }}::ServiceType as FidlService>::StreamHandler::default()),
            );
            {% endfor %}
            map
        }
    };

    TokenStream::from(res)
}
