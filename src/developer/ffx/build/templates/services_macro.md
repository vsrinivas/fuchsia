// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
extern crate proc_macro;

use {
    services::FidlService,
    fidl::endpoints::DiscoverableProtocolMarker,
    proc_macro::TokenStream,
    quote::quote,
    std::collections::hash_map::Entry,
    services_dependencies::{DependencyGraph, Node, VerifyError},
};

#[proc_macro]
pub fn generate_service_map(_item: TokenStream) -> TokenStream {
    let mut graph = DependencyGraph::default();
    {% for dep in deps %}
    let {{ dep.lib }}_node = Node::new(
      // Protocol name.
      <<{{ dep.lib }}::ServiceType as FidlService>::Service as DiscoverableProtocolMarker>::PROTOCOL_NAME,
      // Build target.
      "{{ dep.target }}".to_owned(),

      // Deps.
      {{ dep.lib }}::SERVICE_DEPS,
    );
    match graph.nodes.entry({{ dep.lib }}_node.protocol) {
        Entry::Occupied(e) => {
            panic!("Service endpoint for {} already registered with library at {}", {{ dep.lib }}_node.protocol, e.get().build_target);
        }
        Entry::Vacant(i) => {
          i.insert({{ dep.lib }}_node)
        }
    };
    {% endfor %}
    if let Err(e) = graph.verify() {
      match e {
        VerifyError::CycleFound(path) => {
          let errors_string = path
            .into_iter()
            .map(|n| format!("\n\t{}, implemented in {}", n.protocol, n.build_target))
            .collect::<Vec<_>>()
            .join("");
          panic!("Dependency cycle detected between the following service protocols: {}", errors_string);
        }
        VerifyError::InvalidDependencyError(m) => panic!("{}", m),
      }
    }
    let res = quote! {
        {
            use fidl::endpoints::DiscoverableProtocolMarker;
            use services::{FidlStreamHandler, FidlService};
            use services::NameToStreamHandlerMap;
            let mut map = NameToStreamHandlerMap::new();
            {% for dep in deps %}
            map.insert(
                <<{{ dep.lib }}::ServiceType as FidlService>::Service as DiscoverableProtocolMarker>::PROTOCOL_NAME.to_owned(),
                Box::new(<{{dep.lib }}::ServiceType as FidlService>::StreamHandler::default()),
            );
            {% endfor %}
            map
        }
    };

    TokenStream::from(res)
}
