// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    proc_macro2::{Punct, Span, TokenStream},
    quote::quote,
    std::collections::HashMap,
    syn::{
        parse::{Parse, ParseStream},
        punctuated::Punctuated,
        spanned::Spanned,
        Error, FnArg, Ident, ItemFn, ItemStruct, Lit, LitStr, Pat, PatType, Token,
        Type::Path,
        TypePath,
    },
};

const EXPECTED_SIGNATURE: &str = "ffx_plugin expects one of the following signatures:\n\
(RemoteControlProxy, <YourArghCommand>) -> Result<(), anyhow::Error>\n\
OR\n\
(DaemonProxy, <YourArghCommand>) -> Result<(), anyhow::Error>\n\
OR\n\
(<YourArghCommand>) -> Result<(), anyhow::Error>";

const UNRECOGNIZED_PARAMETER: &str = "If this is a proxy, make sure the parameter's type matches \
the mapping passed into the ffx_plugin attribute.";

pub fn ffx_command(input: ItemStruct) -> TokenStream {
    let cmd = input.ident.clone();
    quote! {
        #input
        pub type FfxPluginCommand = #cmd;
    }
}

fn qualified(path: &syn::Path, name: String) -> Punctuated<Ident, Token!(::)> {
    let mut result: Punctuated<Ident, Token!(::)> = Punctuated::new();
    path.segments.pairs().for_each(|pair| {
        if let Some(_) = pair.punct() {
            result.push(pair.value().ident.clone());
        } else {
            // last ident won't have a punctuation
            result.push(Ident::new(&name, Span::call_site()));
        }
    });
    result
}

fn qualified_name(path: &syn::Path) -> String {
    path.segments
        .pairs()
        .map(|pair| {
            if let Some(_) = pair.punct() {
                format!("{}::", pair.value().ident.to_string())
            } else {
                // last ident won't have a punctuation
                pair.value().ident.to_string()
            }
        })
        .fold(String::new(), |accum, elem| format!("{}{}", accum, elem))
}

pub fn ffx_plugin(input: ItemFn, proxies: ProxyMap) -> Result<TokenStream, Error> {
    let mut uses_daemon = false;
    let mut uses_remote = false;
    let mut uses_map = false;
    let mut args: Punctuated<Ident, Token!(,)> = Punctuated::new();
    let mut futures: Punctuated<Ident, Token!(,)> = Punctuated::new();
    let mut proxies_to_generate = Vec::new();
    let mut cmd_arg = None;
    let method = input.sig.ident.clone();
    for arg in input.sig.inputs.clone() {
        match arg.clone() {
            FnArg::Receiver(_) => {
                return Err(Error::new(
                    arg.span(),
                    "ffx plugin method signature cannot contain self",
                ))
            }
            FnArg::Typed(PatType { ty, pat, .. }) => {
                match ty.as_ref() {
                    Path(TypePath { path, .. }) => match path.segments.last() {
                        Some(t) => {
                            if t.ident == Ident::new("RemoteControlProxy", Span::call_site()) {
                                args.push(Ident::new("remote_proxy", Span::call_site()));
                                uses_remote = true;
                            } else if t.ident == Ident::new("DaemonProxy", Span::call_site()) {
                                args.push(Ident::new("daemon_proxy", Span::call_site()));
                                uses_daemon = true;
                            } else {
                                // try to find ident in the proxy map
                                let qualified_name = qualified_name(path);
                                match proxies.map.get(&qualified_name) {
                                    Some(mapping) => {
                                        uses_map = true;
                                        let mut marker_name = t.ident.to_string();
                                        let _ = marker_name.split_off(marker_name.len() - 5);
                                        marker_name.push_str("Marker");
                                        let qualified_marker = qualified(path, marker_name);
                                        let mapping_lit = LitStr::new(mapping, Span::call_site());
                                        if let Pat::Ident(pat_ident) = pat.as_ref() {
                                            let output = pat_ident.ident.clone();
                                            let output_fut = Ident::new(
                                                &format!("{}_fut", output),
                                                Span::call_site(),
                                            );
                                            let server_end = Ident::new(
                                                &format!("{}_server_end", output),
                                                Span::call_site(),
                                            );
                                            let selector = Ident::new(
                                                &format!("{}_selector", output),
                                                Span::call_site(),
                                            );
                                            proxies_to_generate.push(quote!{
                                                let (#output, #server_end) =
                                                    fidl::endpoints::create_proxy::<#qualified_marker>()?;
                                                let #selector =
                                                    selectors::parse_selector(#mapping_lit)?;
                                                let #output_fut = remote_proxy
                                                    .connect(#selector, #server_end.into_channel());
                                            });
                                            args.push(output);
                                            futures.push(output_fut);
                                        }
                                    }
                                    None => {
                                        // This SHOULD be the argh command.
                                        if let Pat::Ident(pat_ident) = pat.as_ref() {
                                            if let None = cmd_arg {
                                                args.push(pat_ident.ident.clone());
                                                cmd_arg = Some(arg);
                                            } else {
                                                return Err(Error::new(
                                                    arg.span(),
                                                    format!(
                                                        "ffx_plugin could not recognize the\
                                                               parameter: {}\n{}",
                                                        pat_ident.ident.clone(),
                                                        UNRECOGNIZED_PARAMETER
                                                    ),
                                                ));
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        _ => return Err(Error::new(arg.span(), EXPECTED_SIGNATURE)),
                    },
                    _ => return Err(Error::new(arg.span(), EXPECTED_SIGNATURE)),
                };
            }
        }
    }

    let mut preamble = quote! {};
    let mut outer_args: Punctuated<_, Token!(,)> = Punctuated::new();
    if uses_daemon {
        outer_args.push(quote! {daemon_factory: D});
        preamble = quote! {
            #preamble
            let daemon_proxy = daemon_factory().await?;
        };
    } else {
        outer_args.push(quote! {_daemon_factory: D});
    }
    if uses_remote || uses_map {
        outer_args.push(quote! {remote_factory: R});
        preamble = quote! {
            #preamble
            let remote_proxy = remote_factory().await?;
            #(#proxies_to_generate)*
        };
    } else {
        outer_args.push(quote! {_remote_factory: R});
    }

    if let Some(c) = cmd_arg {
        outer_args.push(quote! {#c});
    } else {
        return Err(Error::new(Span::call_site(), EXPECTED_SIGNATURE));
    }

    let implementation = if proxies_to_generate.len() > 0 {
        quote! {
            #preamble
            match futures::try_join!(#futures) {
                Ok(_) => {
                    #method(#args).await
                },
                Err(e) => {
                    log::error!("There was an error getting proxies from the Remote Control Service: {}", e);
                    Err(anyhow::anyhow!("There was an error getting proxies from the Remote Control Service: {}", e))
                }
            }
        }
    } else {
        quote! {
            #preamble
            #method(#args).await
        }
    };

    Ok(quote! {
        #input
        pub async fn ffx_plugin_impl<D, R, DFut, RFut>(
            #outer_args
        ) -> std::result::Result<(), anyhow::Error>
            where
            D: FnOnce() -> DFut,
            DFut: std::future::Future<
                Output = std::result::Result<
                    fidl_fuchsia_developer_bridge::DaemonProxy,
                    anyhow::Error
                >,
            >,
            R: FnOnce() -> RFut,
            RFut: std::future::Future<
                Output = std::result::Result<
                    fidl_fuchsia_developer_remotecontrol::RemoteControlProxy,
                    anyhow::Error,
                >,
            >,
        {
            #implementation
        }
    })
}

#[derive(Debug)]
pub struct ProxyMap {
    map: HashMap<String, String>,
}

impl Parse for ProxyMap {
    fn parse(input: ParseStream<'_>) -> Result<Self, Error> {
        let mut map = HashMap::new();
        let first_lookahead = input.lookahead1();
        if !first_lookahead.peek(Ident) {
            return Ok(Self { map });
        }
        while let Path(TypePath { path, .. }) = input.parse()? {
            let _: Punct = input.parse()?;
            if let Lit::Str(selection) = input.parse()? {
                let lookahead = input.lookahead1();
                // check for trailing comma
                if lookahead.peek(Token!(,)) {
                    let _: Punct = input.parse()?;
                }
                let _ = selectors::parse_selector(&selection.value()).map_err(|e| {
                    Error::new(selection.span(), format!("Invalid selection string: {}", e))
                })?;
                map.insert(qualified_name(&path), selection.value());
                let finish_lookahead = input.lookahead1();
                if !finish_lookahead.peek(Ident) {
                    return Ok(Self { map });
                }
            }
        }
        // Fix span to properly highlight the incorrect service string
        Err(Error::new(Span::call_site(), "Invalid proxy mapping"))
    }
}

////////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use {
        super::*,
        syn::{
            parse::{Parse, ParseStream},
            parse2, parse_quote, ItemType,
        },
    };

    struct WrappedCommand {
        original: ItemStruct,
        plugin: ItemType,
    }

    impl Parse for WrappedCommand {
        fn parse(input: ParseStream<'_>) -> Result<Self, Error> {
            Ok(WrappedCommand { original: input.parse()?, plugin: input.parse()? })
        }
    }

    struct WrappedFunction {
        original: ItemFn,
        plugin: ItemFn,
    }

    impl Parse for WrappedFunction {
        fn parse(input: ParseStream<'_>) -> Result<Self, Error> {
            Ok(WrappedFunction { original: input.parse()?, plugin: input.parse()? })
        }
    }

    #[test]
    fn test_ffx_command() -> Result<(), Error> {
        let item: ItemStruct = parse_quote! {pub struct EchoCommand {}};
        let plugin: ItemType = parse_quote! {pub type FfxPluginCommand = EchoCommand;};
        let result: WrappedCommand = parse2(ffx_command(item.clone()))?;
        assert_eq!(item, result.original);
        assert_eq!(plugin, result.plugin);
        Ok(())
    }

    #[test]
    fn test_ffx_plugin_with_just_a_command() -> Result<(), Error> {
        let proxies = ProxyMap { map: HashMap::new() };
        let original: ItemFn = parse_quote! {
            pub async fn echo(_cmd: EchoCommand) -> Result<(), Error> { Ok(()) }
        };
        let plugin: ItemFn = parse_quote! {
            pub async fn ffx_plugin_impl<D, R, DFut, RFut>(
                _daemon_factory: D,
                _remote_factory: R,
                _cmd: EchoCommand
            ) -> std::result::Result<(), anyhow::Error>
                where
                D: FnOnce() -> DFut,
                DFut: std::future::Future<
                    Output = std::result::Result<
                        fidl_fuchsia_developer_bridge::DaemonProxy,
                        anyhow::Error
                    >,
                >,
                R: FnOnce() -> RFut,
                RFut: std::future::Future<
                    Output = std::result::Result<
                        fidl_fuchsia_developer_remotecontrol::RemoteControlProxy,
                        anyhow::Error,
                    >,
                >,
            {
                echo(_cmd).await
            }
        };
        let result: WrappedFunction = parse2(ffx_plugin(original.clone(), proxies)?)?;
        assert_eq!(original, result.original);
        assert_eq!(plugin, result.plugin);
        Ok(())
    }

    #[test]
    fn test_ffx_plugin_with_a_daemon_proxy_and_command() -> Result<(), Error> {
        let proxies = ProxyMap { map: HashMap::new() };
        let original: ItemFn = parse_quote! {
            pub async fn echo(
                daemon: DaemonProxy,
                _cmd: EchoCommand) -> Result<(), Error> { Ok(()) }
        };
        let plugin: ItemFn = parse_quote! {
            pub async fn ffx_plugin_impl<D, R, DFut, RFut>(
                daemon_factory: D,
                _remote_factory: R,
                _cmd: EchoCommand
            ) -> std::result::Result<(), anyhow::Error>
                where
                D: FnOnce() -> DFut,
                DFut: std::future::Future<
                    Output = std::result::Result<
                        fidl_fuchsia_developer_bridge::DaemonProxy,
                        anyhow::Error
                    >,
                >,
                R: FnOnce() -> RFut,
                RFut: std::future::Future<
                    Output = std::result::Result<
                        fidl_fuchsia_developer_remotecontrol::RemoteControlProxy,
                        anyhow::Error,
                    >,
                >,
            {
                let daemon_proxy = daemon_factory().await?;
                echo(daemon_proxy, _cmd).await
            }
        };
        let result: WrappedFunction = parse2(ffx_plugin(original.clone(), proxies)?)?;
        assert_eq!(original, result.original);
        assert_eq!(plugin, result.plugin);
        Ok(())
    }

    #[test]
    fn test_ffx_plugin_with_a_remote_proxy_and_command() -> Result<(), Error> {
        let proxies = ProxyMap { map: HashMap::new() };
        let original: ItemFn = parse_quote! {
            pub async fn echo(
                remote: RemoteControlProxy,
                _cmd: EchoCommand) -> Result<(), Error> { Ok(()) }
        };
        let plugin: ItemFn = parse_quote! {
            pub async fn ffx_plugin_impl<D, R, DFut, RFut>(
                _daemon_factory: D,
                remote_factory: R,
                _cmd: EchoCommand
            ) -> std::result::Result<(), anyhow::Error>
                where
                D: FnOnce() -> DFut,
                DFut: std::future::Future<
                    Output = std::result::Result<
                        fidl_fuchsia_developer_bridge::DaemonProxy,
                        anyhow::Error
                    >,
                >,
                R: FnOnce() -> RFut,
                RFut: std::future::Future<
                    Output = std::result::Result<
                        fidl_fuchsia_developer_remotecontrol::RemoteControlProxy,
                        anyhow::Error,
                    >,
                >,
            {
                let remote_proxy = remote_factory().await?;
                echo(remote_proxy, _cmd).await
            }
        };
        let result: WrappedFunction = parse2(ffx_plugin(original.clone(), proxies)?)?;
        assert_eq!(original, result.original);
        assert_eq!(plugin, result.plugin);
        Ok(())
    }

    #[test]
    fn test_ffx_plugin_with_a_remote_proxy_and_daemon_proxy_and_command() -> Result<(), Error> {
        let proxies = ProxyMap { map: HashMap::new() };
        let original: ItemFn = parse_quote! {
            pub async fn echo(
                daemon: DaemonProxy,
                remote: RemoteControlProxy,
                _cmd: EchoCommand) -> Result<(), Error> { Ok(()) }
        };
        let plugin: ItemFn = parse_quote! {
            pub async fn ffx_plugin_impl<D, R, DFut, RFut>(
                daemon_factory: D,
                remote_factory: R,
                _cmd: EchoCommand
            ) -> std::result::Result<(), anyhow::Error>
                where
                D: FnOnce() -> DFut,
                DFut: std::future::Future<
                    Output = std::result::Result<
                        fidl_fuchsia_developer_bridge::DaemonProxy,
                        anyhow::Error
                    >,
                >,
                R: FnOnce() -> RFut,
                RFut: std::future::Future<
                    Output = std::result::Result<
                        fidl_fuchsia_developer_remotecontrol::RemoteControlProxy,
                        anyhow::Error,
                    >,
                >,
            {
                let daemon_proxy = daemon_factory().await?;
                let remote_proxy = remote_factory().await?;
                echo(daemon_proxy, remote_proxy, _cmd).await
            }
        };
        let result: WrappedFunction = parse2(ffx_plugin(original.clone(), proxies)?)?;
        assert_eq!(original, result.original);
        assert_eq!(plugin, result.plugin);
        Ok(())
    }

    #[test]
    fn test_ffx_plugin_with_a_remote_proxy_and_daemon_proxy_and_command_out_of_order(
    ) -> Result<(), Error> {
        let proxies = ProxyMap { map: HashMap::new() };
        let original: ItemFn = parse_quote! {
            pub async fn echo(
                remote: RemoteControlProxy,
                _cmd: EchoCommand,
                daemon: DaemonProxy) -> Result<(), Error> { Ok(()) }
        };
        let plugin: ItemFn = parse_quote! {
            pub async fn ffx_plugin_impl<D, R, DFut, RFut>(
                daemon_factory: D,
                remote_factory: R,
                _cmd: EchoCommand
            ) -> std::result::Result<(), anyhow::Error>
                where
                D: FnOnce() -> DFut,
                DFut: std::future::Future<
                    Output = std::result::Result<
                        fidl_fuchsia_developer_bridge::DaemonProxy,
                        anyhow::Error
                    >,
                >,
                R: FnOnce() -> RFut,
                RFut: std::future::Future<
                    Output = std::result::Result<
                        fidl_fuchsia_developer_remotecontrol::RemoteControlProxy,
                        anyhow::Error,
                    >,
                >,
            {
                let daemon_proxy = daemon_factory().await?;
                let remote_proxy = remote_factory().await?;
                echo(remote_proxy, _cmd, daemon_proxy).await
            }
        };
        let result: WrappedFunction = parse2(ffx_plugin(original.clone(), proxies)?)?;
        assert_eq!(original, result.original);
        assert_eq!(plugin, result.plugin);
        Ok(())
    }

    #[test]
    fn test_ffx_plugin_with_proxy_map_and_command() -> Result<(), Error> {
        let mut map = HashMap::new();
        map.insert("TestProxy".to_string(), "test".to_string());
        let proxies = ProxyMap { map };
        let original: ItemFn = parse_quote! {
            pub async fn echo(
                test: TestProxy,
                cmd: WhateverCommand,
                ) -> Result<(), Error> { Ok(()) }
        };
        let plugin: ItemFn = parse_quote! {
            pub async fn ffx_plugin_impl<D, R, DFut, RFut>(
                _daemon_factory: D,
                remote_factory: R,
                cmd: WhateverCommand
            ) -> std::result::Result<(), anyhow::Error>
                where
                D: FnOnce() -> DFut,
                DFut: std::future::Future<
                    Output = std::result::Result<
                        fidl_fuchsia_developer_bridge::DaemonProxy,
                        anyhow::Error
                    >,
                >,
                R: FnOnce() -> RFut,
                RFut: std::future::Future<
                    Output = std::result::Result<
                        fidl_fuchsia_developer_remotecontrol::RemoteControlProxy,
                        anyhow::Error,
                    >,
                >,
            {
                let remote_proxy = remote_factory().await?;
                let (test, test_server_end) =
                    fidl::endpoints::create_proxy::<TestMarker>()?;
                let test_selector = selectors::parse_selector("test")?;
                let test_fut = remote_proxy
                    .connect(test_selector, test_server_end.into_channel());
                match futures::try_join!(test_fut) {
                    Ok(_) => {
                        echo(test, cmd).await
                    },
                    Err(e) => {
                        log::error!("There was an error getting proxies from the Remote Control Service: {}", e);
                        Err(anyhow::anyhow!("There was an error getting proxies from the Remote Control Service: {}", e))
                    }
                }
            }
        };
        let result: WrappedFunction = parse2(ffx_plugin(original.clone(), proxies)?)?;
        assert_eq!(original, result.original);
        assert_eq!(plugin, result.plugin);
        Ok(())
    }

    #[test]
    fn test_ffx_plugin_with_multiple_proxy_map_and_command() -> Result<(), Error> {
        let mut map = HashMap::new();
        map.insert("TestProxy".to_string(), "test".to_string());
        map.insert("FooProxy".to_string(), "foo".to_string());
        let proxies = ProxyMap { map };
        let original: ItemFn = parse_quote! {
            pub async fn echo(
                foo: FooProxy,
                cmd: WhateverCommand,
                test: TestProxy,
                ) -> Result<(), Error> { Ok(()) }
        };
        let plugin: ItemFn = parse_quote! {
            pub async fn ffx_plugin_impl<D, R, DFut, RFut>(
                _daemon_factory: D,
                remote_factory: R,
                cmd: WhateverCommand
            ) -> std::result::Result<(), anyhow::Error>
                where
                D: FnOnce() -> DFut,
                DFut: std::future::Future<
                    Output = std::result::Result<
                        fidl_fuchsia_developer_bridge::DaemonProxy,
                        anyhow::Error
                    >,
                >,
                R: FnOnce() -> RFut,
                RFut: std::future::Future<
                    Output = std::result::Result<
                        fidl_fuchsia_developer_remotecontrol::RemoteControlProxy,
                        anyhow::Error,
                    >,
                >,
            {
                let remote_proxy = remote_factory().await?;
                let (foo, foo_server_end) =
                    fidl::endpoints::create_proxy::<FooMarker>()?;
                let foo_selector = selectors::parse_selector("foo")?;
                let foo_fut = remote_proxy
                    .connect(foo_selector, foo_server_end.into_channel());
                let (test, test_server_end) =
                    fidl::endpoints::create_proxy::<TestMarker>()?;
                let test_selector = selectors::parse_selector("test")?;
                let test_fut = remote_proxy
                    .connect(test_selector, test_server_end.into_channel());
                match futures::try_join!(foo_fut, test_fut) {
                    Ok(_) => {
                        echo(foo, cmd, test).await
                    },
                    Err(e) => {
                        log::error!("There was an error getting proxies from the Remote Control Service: {}", e);
                        Err(anyhow::anyhow!("There was an error getting proxies from the Remote Control Service: {}", e))
                    }
                }
            }
        };
        let result: WrappedFunction = parse2(ffx_plugin(original.clone(), proxies)?)?;
        assert_eq!(original, result.original);
        assert_eq!(plugin, result.plugin);
        Ok(())
    }

    #[test]
    fn test_ffx_plugin_with_no_parameters_should_err() -> Result<(), Error> {
        let proxies = ProxyMap { map: HashMap::new() };
        let original: ItemFn = parse_quote! {
            pub async fn echo() -> Result<(), Error> { Ok(()) }
        };
        if let Ok(_) = ffx_plugin(original.clone(), proxies) {
            assert!(false, "A method with no parameters should throw an error");
        }
        Ok(())
    }

    #[test]
    fn test_ffx_plugin_with_self_receiver_should_err() -> Result<(), Error> {
        let proxies = ProxyMap { map: HashMap::new() };
        let original: ItemFn = parse_quote! {
            pub async fn echo(self, cmd: EchoCommand) -> Result<(), Error> { Ok(()) }
        };
        if let Ok(_) = ffx_plugin(original.clone(), proxies) {
            assert!(false, "A method with a receiver should throw an error");
        }
        Ok(())
    }

    #[test]
    fn test_ffx_plugin_with_referenced_param_should_err() -> Result<(), Error> {
        let proxies = ProxyMap { map: HashMap::new() };
        let original: ItemFn = parse_quote! {
            pub async fn echo(proxy: &TestProxy, cmd: EchoCommand) -> Result<(), Error> { Ok(()) }
        };
        if let Ok(_) = ffx_plugin(original.clone(), proxies) {
            assert!(false, "A method with references should throw an error");
        }
        Ok(())
    }

    #[test]
    fn test_empty_proxy_map_should_not_err() -> Result<(), Error> {
        let _proxy_map: ProxyMap = parse_quote! {};
        Ok(())
    }

    #[test]
    fn test_simple_proxy_map_should_not_err() -> Result<(), Error> {
        let _proxy_map: ProxyMap = parse_quote! {test = "test:test"};
        Ok(())
    }

    fn proxy_map_test_value(test: String) -> (String, String, TokenStream) {
        let test_value = format!("{}:{}", test, test);
        let test_ident = Ident::new(&test, Span::call_site());
        let mapping_lit = LitStr::new(&test_value, Span::call_site());
        (test.to_string(), test_value.clone(), quote! {#test_ident = #mapping_lit})
    }

    fn test_populating_proxy_map_times(num_of_mappings: usize) {
        let mut proxy_mapping = quote! {};
        let mut key_values = Vec::<(String, String)>::with_capacity(num_of_mappings);
        for x in 0..num_of_mappings {
            let (test_key, test_value, test_proxy_mapping) =
                proxy_map_test_value(format!("test{}", x));
            key_values.push((test_key, test_value));
            proxy_mapping = quote! {
                #test_proxy_mapping,
                #proxy_mapping
            };
        }
        let proxy_map: ProxyMap = parse_quote! { #proxy_mapping };
        for (key, value) in key_values {
            assert_eq!(proxy_map.map.get(&key), Some(&value));
        }
    }

    #[test]
    fn test_populating_proxy_map() -> Result<(), Error> {
        test_populating_proxy_map_times(20);
        Ok(())
    }

    #[test]
    fn test_populating_proxy_map_without_trailing_comma() -> Result<(), Error> {
        let (test1_key, test1_value, test1_proxy_mapping) =
            proxy_map_test_value("test1".to_string());
        let (test2_key, test2_value, test2_proxy_mapping) =
            proxy_map_test_value("test2".to_string());
        let proxy_map: ProxyMap = parse_quote! {
            #test1_proxy_mapping,
            #test2_proxy_mapping
        };
        assert_eq!(proxy_map.map.get(&test1_key), Some(&test1_value));
        assert_eq!(proxy_map.map.get(&test2_key), Some(&test2_value));
        Ok(())
    }

    #[test]
    fn test_invalid_selection_should_err() -> Result<(), Error> {
        let result: Result<ProxyMap, Error> = parse2(quote! {
            test = "test"
        });
        assert!(result.is_err());
        Ok(())
    }

    #[test]
    fn test_invalid_mapping_should_err() -> Result<(), Error> {
        let result: Result<ProxyMap, Error> = parse2(quote! {
            test, "test"
        });
        assert!(result.is_err());
        Ok(())
    }

    #[test]
    fn test_invalid_input_should_err() -> Result<(), Error> {
        let result: Result<ProxyMap, Error> = parse2(quote! {test});
        assert!(result.is_err());
        Ok(())
    }
}
