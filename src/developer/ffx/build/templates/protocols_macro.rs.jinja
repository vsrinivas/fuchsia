// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    protocols::FidlProtocol,
    fidl::endpoints::DiscoverableProtocolMarker,
    proc_macro::TokenStream,
    quote::quote,
    std::collections::hash_map::Entry,
    protocols_dependencies::{DependencyGraph, Node, VerifyError},
};

#[proc_macro]
pub fn generate_protocol_map(_item: TokenStream) -> TokenStream {
    let mut graph = DependencyGraph::default();
    {% for dep in deps %}
    let {{ dep.lib }}_node = Node::new(
      // Protocol name.
      <<{{ dep.lib }}::ProtocolType as FidlProtocol>::Protocol as DiscoverableProtocolMarker>::PROTOCOL_NAME,
      // Build target.
      "{{ dep.target }}".to_owned(),

      // Deps.
      {{ dep.lib }}::PROTOCOL_DEPS,
    );
    match graph.nodes.entry({{ dep.lib }}_node.protocol) {
        Entry::Occupied(e) => {
            panic!("Protocol endpoint for {} already registered with library at {}", {{ dep.lib }}_node.protocol, e.get().build_target);
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
          panic!("Dependency cycle detected between the following protocols: {}", errors_string);
        }
        VerifyError::InvalidDependencyError(m) => panic!("{}", m),
      }
    }
    let res = quote! {
        {
            use fidl::endpoints::DiscoverableProtocolMarker;
            use protocols::{FidlStreamHandler, FidlProtocol};
            use protocols::NameToStreamHandlerMap;
            let mut map = NameToStreamHandlerMap::new();
            {% for dep in deps %}
            map.insert(
                <<{{ dep.lib }}::ProtocolType as FidlProtocol>::Protocol as DiscoverableProtocolMarker>::PROTOCOL_NAME.to_owned(),
                Box::new(<{{dep.lib }}::ProtocolType as FidlProtocol>::StreamHandler::default()),
            );
            {% endfor %}
            map
        }
    };

    TokenStream::from(res)
}
