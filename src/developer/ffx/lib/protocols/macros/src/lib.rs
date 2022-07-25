// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {heck::SnakeCase, proc_macro::TokenStream, quote::format_ident, quote::quote};

/// Generates the necessary glue code to use your protocol within the daemon.
///
/// When using this, if you depend on other protocols within the daemon, make
/// sure to include the FIDL protocol endpoint markers inside a parenthetical
/// list so that dependency checking can be done at build time.
///
/// Declaring dependencies will create functions that you can used to open
/// proxies on the daemon safely. The names are generated based on the name
/// of the protocol endpoint.
///
/// Example:
/// ```rust
/// use fidl_library_of_some_kind::DependentProtocolMarker;
///
/// #[ffx_protocol(DependentProtocolMarker)]
/// pub struct FooProtocol {}
///
/// impl FooProtocol {
///
///   async fn example_func(&self, cx: &Context) -> Result<()> {
///     let proxy = self.open_dependent_protocol_proxy(cx).await?;
///     proxy.do_some_things().await?;
///     Ok(())
///   }
/// }
///
/// ```
#[proc_macro_attribute]
pub fn ffx_protocol(attr: TokenStream, item: TokenStream) -> TokenStream {
    let item: syn::ItemStruct = syn::parse(item.into()).expect("expected struct");
    let attr: syn::AttributeArgs = syn::parse_macro_input!(attr as syn::AttributeArgs);
    let name = item.ident.clone();
    let mut deps = vec![];
    for meta in attr.iter() {
        if let syn::NestedMeta::Meta(syn::Meta::Path(p)) = meta {
            deps.push(p);
        } else {
            panic!("unrecognized meta format");
        }
    }

    let mut q: proc_macro2::TokenStream = quote! {
        #item

        pub type ProtocolType = #name;
        pub const PROTOCOL_DEPS: &'static [&'static str]
            = &[#(<#deps as fidl::endpoints::DiscoverableProtocolMarker>::PROTOCOL_NAME),*];
    }
    .into();

    if !deps.is_empty() {
        let func_names = deps
            .iter()
            .map(|p| {
                let ident =
                    &p.segments.last().expect("must have at least one segment in path").ident;
                let string = ident.to_string().to_snake_case();
                let func_name_string = &string[0..string.len() - "_marker".len()];
                let func_name =
                    format_ident!("open_{}_proxy", func_name_string, span = ident.span());
                func_name
            })
            .collect::<Vec<_>>();
        let proxies = deps
            .iter()
            .map(|&p| {
                let mut path = p.clone();
                // Will have panicked above already.
                let ident = &mut path.segments.last_mut().unwrap().ident;
                let ident_name = ident.to_string();
                let ident_name_shortened = &ident_name[0..ident_name.len() - "Marker".len()];
                *ident = format_ident!("{}Proxy", ident_name_shortened, span = ident.span());
                path
            })
            .collect::<Vec<_>>();
        let struct_name = &item.ident;
        // TODO(awdavies): Include things like generics and lifetime params
        // for the struct containing the attribute.
        q.extend(quote! {
            impl #struct_name {
                #(
                    pub async fn #func_names(&self, cx: &Context) -> Result<#proxies>
                    {
                        cx.open_protocol::<#deps>().await
                    }
                )*
            }
        });
    }
    TokenStream::from(q)
}
