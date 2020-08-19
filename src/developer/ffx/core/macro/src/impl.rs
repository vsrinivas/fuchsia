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

const EXPECTED_SIGNATURE: &str = "ffx_plugin expects at least the command created in the args.rs \
                                  file and will accept FIDL proxies if mapped in the ffx_plugin \
                                  annotation.";

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

fn replace_proxy(path: &syn::Path, name: String, replace: &str) -> Punctuated<Ident, Token!(::)> {
    let mut result = format!("{}", name);
    let _ = result.split_off(result.len() - 5);
    result.push_str(replace);
    qualified(path, result)
}

fn generate_fake_test_proxy_method(
    proxy_name: Ident,
    qualified_proxy_type: &syn::Path,
    qualified_proxy_marker_type: Punctuated<Ident, Token!(::)>,
    qualified_proxy_request_type: Punctuated<Ident, Token!(::)>,
) -> TokenStream {
    let method_name = Ident::new(&format!("setup_fake_{}", proxy_name), Span::call_site());
    // Oneshot method is needed only for the 'component run' unit tests that leaks memory
    // everywhere unless shut down from the server side.
    let oneshot_method_name =
        Ident::new(&format!("setup_oneshot_fake_{}", proxy_name), Span::call_site());
    quote! {
        #[cfg(test)]
        fn #method_name<R:'static>(handle_request: R) -> #qualified_proxy_type
            where R: FnOnce(#qualified_proxy_request_type) + std::marker::Send + Copy
        {
            use futures::TryStreamExt;
            let (proxy, mut stream) =
                fidl::endpoints::create_proxy_and_stream::<#qualified_proxy_marker_type>().unwrap();
            fuchsia_async::Task::spawn(async move {
                while let Ok(Some(req)) = stream.try_next().await {
                    handle_request(req);
                }
            })
            .detach();
            proxy
        }

        #[cfg(test)]
        fn #oneshot_method_name<R:'static>(handle_request: R) -> #qualified_proxy_type
            where R: FnOnce(#qualified_proxy_request_type) + std::marker::Send + Copy
        {
            use futures::TryStreamExt;
            let (proxy, mut stream) =
                fidl::endpoints::create_proxy_and_stream::<#qualified_proxy_marker_type>().unwrap();
            fuchsia_async::Task::spawn(async move {
                if let Ok(Some(req)) = stream.try_next().await {
                    handle_request(req);
                }
            })
            .detach();
            proxy
        }
    }
}

pub fn ffx_plugin(input: ItemFn, proxies: ProxyMap) -> Result<TokenStream, Error> {
    let mut uses_daemon = false;
    let mut uses_remote = false;
    let mut uses_map = false;
    let mut args: Punctuated<Ident, Token!(,)> = Punctuated::new();
    let mut futures: Punctuated<Ident, Token!(,)> = Punctuated::new();
    let mut proxies_to_generate = Vec::new();
    let mut test_fake_methods_to_generate = Vec::<TokenStream>::new();
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
                                if let Pat::Ident(pat_ident) = pat.as_ref() {
                                    test_fake_methods_to_generate.push(
                                        generate_fake_test_proxy_method(
                                            pat_ident.ident.clone(),
                                            path,
                                            replace_proxy(path, t.ident.to_string(), "Marker"),
                                            replace_proxy(path, t.ident.to_string(), "Request"),
                                        ),
                                    );
                                }
                            } else if t.ident == Ident::new("DaemonProxy", Span::call_site()) {
                                args.push(Ident::new("daemon_proxy", Span::call_site()));
                                uses_daemon = true;
                                if let Pat::Ident(pat_ident) = pat.as_ref() {
                                    test_fake_methods_to_generate.push(
                                        generate_fake_test_proxy_method(
                                            pat_ident.ident.clone(),
                                            path,
                                            replace_proxy(path, t.ident.to_string(), "Marker"),
                                            replace_proxy(path, t.ident.to_string(), "Request"),
                                        ),
                                    );
                                }
                            } else {
                                // try to find ident in the proxy map
                                let qualified_proxy_name = qualified_name(path);
                                match proxies.map.get(&qualified_proxy_name) {
                                    Some(mapping) => {
                                        uses_map = true;
                                        let qualified_marker =
                                            replace_proxy(path, t.ident.to_string(), "Marker");
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
                                            test_fake_methods_to_generate.push(
                                                generate_fake_test_proxy_method(
                                                    pat_ident.ident.clone(),
                                                    path,
                                                    replace_proxy(
                                                        path,
                                                        t.ident.to_string(),
                                                        "Marker",
                                                    ),
                                                    replace_proxy(
                                                        path,
                                                        t.ident.to_string(),
                                                        "Request",
                                                    ),
                                                ),
                                            );
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

    if let Some(_) = proxies.experiment_key {
        outer_args.push(quote! {is_experiment: E});
    } else {
        outer_args.push(quote! {_is_experiment: E});
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
                    anyhow::bail!("There was an error getting proxies from the Remote Control Service: {}", e)
                }
            }
        }
    } else {
        quote! {
            #preamble
            #method(#args).await
        }
    };

    let gated_impl = if let Some(key) = proxies.experiment_key {
        quote! {
            if is_experiment(#key).await {
                #implementation
            } else {
                println!("This is an experimental subcommand.  To enable this subcommand run 'ffx config set {} true'", #key);
                Ok(())
            }
        }
    } else {
        implementation
    };

    Ok(quote! {
        #input
        pub async fn ffx_plugin_impl<D, R, DFut, RFut, E, EFut>(
            #outer_args
        ) -> anyhow::Result<()>
            where
            D: FnOnce() -> DFut,
            DFut: std::future::Future<
                Output = anyhow::Result<fidl_fuchsia_developer_bridge::DaemonProxy>,
            >,
            R: FnOnce() -> RFut,
            RFut: std::future::Future<
                Output = anyhow::Result<
                    fidl_fuchsia_developer_remotecontrol::RemoteControlProxy
                >,
            >,
            E: FnOnce(&'static str) -> EFut,
            EFut: std::future::Future<Output = bool>,
        {
            #gated_impl
        }

        #(#test_fake_methods_to_generate)*
    })
}

#[derive(Debug)]
pub struct ProxyMap {
    experiment_key: Option<String>,
    map: HashMap<String, String>,
}

impl Default for ProxyMap {
    fn default() -> Self {
        Self { experiment_key: None, map: HashMap::new() }
    }
}

impl Parse for ProxyMap {
    fn parse(input: ParseStream<'_>) -> Result<Self, Error> {
        let mut experiment_key = None;
        let mut map = HashMap::new();
        while !input.is_empty() {
            if input.peek(Ident) {
                // Dump the next parse since we got it via the peek
                if let Path(TypePath { path, .. }) = input.parse()? {
                    let _: Punct = input.parse()?;
                    if let Lit::Str(selection) = input.parse()? {
                        if input.peek(Token!(,)) {
                            // Parse the trailing comma
                            let _: Punct = input.parse()?;
                        }
                        let _ = selectors::parse_selector(&selection.value()).map_err(|e| {
                            Error::new(selection.span(), format!("Invalid selection string: {}", e))
                        })?;
                        map.insert(qualified_name(&path), selection.value());
                    }
                }
            } else if input.peek(Lit) {
                if let Lit::Str(found_key) = input.parse()? {
                    // This must be the experiment key.
                    if let Some(key) = experiment_key {
                        // experiment_key was already found
                        return Err(Error::new(
                            found_key.span(),
                            format!(
                                "Experiment key set twice.  First found: {}, Second found: {}",
                                key,
                                found_key.value()
                            ),
                        ));
                    } else {
                        experiment_key = Some(format!("{}", found_key.value()));
                        if input.peek(Token!(,)) {
                            // Parse the trailing comma
                            let _: Punct = input.parse()?;
                        }
                    }
                }
            } else {
                return Err(Error::new(Span::call_site(), "Invalid plugin inputs"));
            }
        }
        Ok(Self { map, experiment_key })
    }
}

////////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use {
        super::*,
        std::default::Default,
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
        fake_tests: Vec<ItemFn>,
    }

    impl Parse for WrappedFunction {
        fn parse(input: ParseStream<'_>) -> Result<Self, Error> {
            let original = input.parse()?;
            let plugin = input.parse()?;
            let mut fake_tests = Vec::new();
            while !input.is_empty() {
                fake_tests.push(input.parse()?);
            }
            Ok(WrappedFunction { original, plugin, fake_tests })
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
        let proxies = Default::default();
        let original: ItemFn = parse_quote! {
            pub async fn echo(_cmd: EchoCommand) -> anyhow::Result<()> { Ok(()) }
        };
        let plugin: ItemFn = parse_quote! {
            pub async fn ffx_plugin_impl<D, R, DFut, RFut, E, EFut>(
                _daemon_factory: D,
                _remote_factory: R,
                _is_experiment: E,
                _cmd: EchoCommand
            ) -> anyhow::Result<()>
                where
                D: FnOnce() -> DFut,
                DFut: std::future::Future<
                    Output = anyhow::Result<fidl_fuchsia_developer_bridge::DaemonProxy>,
                >,
                R: FnOnce() -> RFut,
                RFut: std::future::Future<
                    Output = anyhow::Result<
                        fidl_fuchsia_developer_remotecontrol::RemoteControlProxy
                    >,
                >,
                E: FnOnce(&'static str) -> EFut,
                EFut: std::future::Future<Output = bool>,
            {
                echo(_cmd).await
            }
        };
        let result: WrappedFunction = parse2(ffx_plugin(original.clone(), proxies)?)?;
        assert_eq!(original, result.original);
        assert_eq!(plugin, result.plugin);
        assert_eq!(0, result.fake_tests.len());
        Ok(())
    }

    #[test]
    fn test_ffx_plugin_with_a_daemon_proxy_and_command() -> Result<(), Error> {
        let proxies = Default::default();
        let original: ItemFn = parse_quote! {
            pub async fn echo(
                daemon: DaemonProxy,
                _cmd: EchoCommand) -> anyhow::Result<()> { Ok(()) }
        };
        let plugin: ItemFn = parse_quote! {
            pub async fn ffx_plugin_impl<D, R, DFut, RFut, E, EFut>(
                daemon_factory: D,
                _remote_factory: R,
                _is_experiment: E,
                _cmd: EchoCommand
            ) -> anyhow::Result<()>
                where
                D: FnOnce() -> DFut,
                DFut: std::future::Future<
                    Output = anyhow::Result<fidl_fuchsia_developer_bridge::DaemonProxy>,
                >,
                R: FnOnce() -> RFut,
                RFut: std::future::Future<
                    Output = anyhow::Result<
                        fidl_fuchsia_developer_remotecontrol::RemoteControlProxy
                    >,
                >,
                E: FnOnce(&'static str) -> EFut,
                EFut: std::future::Future<Output = bool>,
            {
                let daemon_proxy = daemon_factory().await?;
                echo(daemon_proxy, _cmd).await
            }
        };
        let fake_test: ItemFn = parse_quote! {
            #[cfg(test)]
            fn setup_fake_daemon<R:'static>(handle_request: R) -> DaemonProxy
                where R: FnOnce(DaemonRequest) + std::marker::Send + Copy
            {
                use futures::TryStreamExt;
                let (proxy, mut stream) =
                    fidl::endpoints::create_proxy_and_stream::<DaemonMarker>().unwrap();
                fuchsia_async::Task::spawn(async move {
                    while let Ok(Some(req)) = stream.try_next().await {
                        handle_request(req);
                    }
                })
                .detach();
                proxy
            }
        };
        let oneshot_fake_test: ItemFn = parse_quote! {
            #[cfg(test)]
            fn setup_oneshot_fake_daemon<R:'static>(handle_request: R) -> DaemonProxy
                where R: FnOnce(DaemonRequest) + std::marker::Send + Copy
            {
                use futures::TryStreamExt;
                let (proxy, mut stream) =
                    fidl::endpoints::create_proxy_and_stream::<DaemonMarker>().unwrap();
                fuchsia_async::Task::spawn(async move {
                    if let Ok(Some(req)) = stream.try_next().await {
                        handle_request(req);
                    }
                })
                .detach();
                proxy
            }
        };
        let result: WrappedFunction = parse2(ffx_plugin(original.clone(), proxies)?)?;
        assert_eq!(original, result.original);
        assert_eq!(plugin, result.plugin);
        assert_eq!(2, result.fake_tests.len());
        assert_eq!(fake_test, result.fake_tests[0]);
        assert_eq!(oneshot_fake_test, result.fake_tests[1]);
        Ok(())
    }

    #[test]
    fn test_ffx_plugin_with_a_remote_proxy_and_command() -> Result<(), Error> {
        let proxies = Default::default();
        let original: ItemFn = parse_quote! {
            pub async fn echo(
                remote: RemoteControlProxy,
                _cmd: EchoCommand) -> anyhow::Result<()> { Ok(()) }
        };
        let plugin: ItemFn = parse_quote! {
            pub async fn ffx_plugin_impl<D, R, DFut, RFut, E, EFut>(
                _daemon_factory: D,
                remote_factory: R,
                _is_experiment: E,
                _cmd: EchoCommand
            ) -> anyhow::Result<()>
                where
                D: FnOnce() -> DFut,
                DFut: std::future::Future<
                    Output = anyhow::Result<fidl_fuchsia_developer_bridge::DaemonProxy>,
                >,
                R: FnOnce() -> RFut,
                RFut: std::future::Future<
                    Output = anyhow::Result<
                        fidl_fuchsia_developer_remotecontrol::RemoteControlProxy
                    >,
                >,
                E: FnOnce(&'static str) -> EFut,
                EFut: std::future::Future<Output = bool>,
            {
                let remote_proxy = remote_factory().await?;
                echo(remote_proxy, _cmd).await
            }
        };
        let fake_test: ItemFn = parse_quote! {
            #[cfg(test)]
            fn setup_fake_remote<R:'static>(handle_request: R) -> RemoteControlProxy
                where R: FnOnce(RemoteControlRequest) + std::marker::Send + Copy
            {
                use futures::TryStreamExt;
                let (proxy, mut stream) =
                    fidl::endpoints::create_proxy_and_stream::<RemoteControlMarker>().unwrap();
                fuchsia_async::Task::spawn(async move {
                    while let Ok(Some(req)) = stream.try_next().await {
                        handle_request(req);
                    }
                })
                .detach();
                proxy
            }
        };
        let result: WrappedFunction = parse2(ffx_plugin(original.clone(), proxies)?)?;
        assert_eq!(original, result.original);
        assert_eq!(plugin, result.plugin, "{:?}", result.plugin);
        assert_eq!(2, result.fake_tests.len());
        assert_eq!(fake_test, result.fake_tests[0]);
        Ok(())
    }

    #[test]
    fn test_ffx_plugin_with_a_remote_proxy_and_daemon_proxy_and_command() -> Result<(), Error> {
        let proxies = Default::default();
        let original: ItemFn = parse_quote! {
            pub async fn echo(
                daemon: DaemonProxy,
                remote: RemoteControlProxy,
                _cmd: EchoCommand) -> anyhow::Result<()> { Ok(()) }
        };
        let plugin: ItemFn = parse_quote! {
            pub async fn ffx_plugin_impl<D, R, DFut, RFut, E, EFut>(
                daemon_factory: D,
                remote_factory: R,
                _is_experiment: E,
                _cmd: EchoCommand
            ) -> anyhow::Result<()>
                where
                D: FnOnce() -> DFut,
                DFut: std::future::Future<
                    Output = anyhow::Result<fidl_fuchsia_developer_bridge::DaemonProxy>,
                >,
                R: FnOnce() -> RFut,
                RFut: std::future::Future<
                    Output = anyhow::Result<
                        fidl_fuchsia_developer_remotecontrol::RemoteControlProxy
                    >,
                >,
                E: FnOnce(&'static str) -> EFut,
                EFut: std::future::Future<Output = bool>,
            {
                let daemon_proxy = daemon_factory().await?;
                let remote_proxy = remote_factory().await?;
                echo(daemon_proxy, remote_proxy, _cmd).await
            }
        };
        let fake_daemon_test: ItemFn = parse_quote! {
            #[cfg(test)]
            fn setup_fake_daemon<R:'static>(handle_request: R) -> DaemonProxy
                where R: FnOnce(DaemonRequest) + std::marker::Send + Copy
            {
                use futures::TryStreamExt;
                let (proxy, mut stream) =
                    fidl::endpoints::create_proxy_and_stream::<DaemonMarker>().unwrap();
                fuchsia_async::Task::spawn(async move {
                    while let Ok(Some(req)) = stream.try_next().await {
                        handle_request(req);
                    }
                })
                .detach();
                proxy
            }
        };
        let fake_remote_test: ItemFn = parse_quote! {
            #[cfg(test)]
            fn setup_fake_remote<R:'static>(handle_request: R) -> RemoteControlProxy
                where R: FnOnce(RemoteControlRequest) + std::marker::Send + Copy
            {
                use futures::TryStreamExt;
                let (proxy, mut stream) =
                    fidl::endpoints::create_proxy_and_stream::<RemoteControlMarker>().unwrap();
                fuchsia_async::Task::spawn(async move {
                    while let Ok(Some(req)) = stream.try_next().await {
                        handle_request(req);
                    }
                })
                .detach();
                proxy
            }
        };
        let result: WrappedFunction = parse2(ffx_plugin(original.clone(), proxies)?)?;
        assert_eq!(original, result.original);
        assert_eq!(plugin, result.plugin);
        assert_eq!(4, result.fake_tests.len());
        assert_eq!(fake_daemon_test, result.fake_tests[0]);
        assert_eq!(fake_remote_test, result.fake_tests[2]);
        Ok(())
    }

    #[test]
    fn test_ffx_plugin_with_a_remote_proxy_and_daemon_proxy_and_command_out_of_order(
    ) -> Result<(), Error> {
        let proxies = Default::default();
        let original: ItemFn = parse_quote! {
            pub async fn echo(
                remote: RemoteControlProxy,
                _cmd: EchoCommand,
                daemon: DaemonProxy) -> anyhow::Result<()> { Ok(()) }
        };
        let plugin: ItemFn = parse_quote! {
            pub async fn ffx_plugin_impl<D, R, DFut, RFut, E, EFut>(
                daemon_factory: D,
                remote_factory: R,
                _is_experiment: E,
                _cmd: EchoCommand
            ) -> anyhow::Result<()>
                where
                D: FnOnce() -> DFut,
                DFut: std::future::Future<
                    Output = anyhow::Result<fidl_fuchsia_developer_bridge::DaemonProxy>,
                >,
                R: FnOnce() -> RFut,
                RFut: std::future::Future<
                    Output = anyhow::Result<
                        fidl_fuchsia_developer_remotecontrol::RemoteControlProxy
                    >,
                >,
                E: FnOnce(&'static str) -> EFut,
                EFut: std::future::Future<Output = bool>,
            {
                let daemon_proxy = daemon_factory().await?;
                let remote_proxy = remote_factory().await?;
                echo(remote_proxy, _cmd, daemon_proxy).await
            }
        };
        let fake_daemon_test: ItemFn = parse_quote! {
            #[cfg(test)]
            fn setup_fake_daemon<R:'static>(handle_request: R) -> DaemonProxy
                where R: FnOnce(DaemonRequest) + std::marker::Send + Copy
            {
                use futures::TryStreamExt;
                let (proxy, mut stream) =
                    fidl::endpoints::create_proxy_and_stream::<DaemonMarker>().unwrap();
                fuchsia_async::Task::spawn(async move {
                    while let Ok(Some(req)) = stream.try_next().await {
                        handle_request(req);
                    }
                })
                .detach();
                proxy
            }
        };
        let fake_remote_test: ItemFn = parse_quote! {
            #[cfg(test)]
            fn setup_fake_remote<R:'static>(handle_request: R) -> RemoteControlProxy
                where R: FnOnce(RemoteControlRequest) + std::marker::Send + Copy
            {
                use futures::TryStreamExt;
                let (proxy, mut stream) =
                    fidl::endpoints::create_proxy_and_stream::<RemoteControlMarker>().unwrap();
                fuchsia_async::Task::spawn(async move {
                    while let Ok(Some(req)) = stream.try_next().await {
                        handle_request(req);
                    }
                })
                .detach();
                proxy
            }
        };
        let result: WrappedFunction = parse2(ffx_plugin(original.clone(), proxies)?)?;
        assert_eq!(original, result.original);
        assert_eq!(plugin, result.plugin);
        assert_eq!(4, result.fake_tests.len());
        assert_eq!(fake_remote_test, result.fake_tests[0]);
        assert_eq!(fake_daemon_test, result.fake_tests[2]);
        Ok(())
    }

    #[test]
    fn test_ffx_plugin_with_proxy_map_and_command() -> Result<(), Error> {
        let mut map = HashMap::new();
        map.insert("TestProxy".to_string(), "test".to_string());
        let proxies = ProxyMap { map, ..Default::default() };
        let original: ItemFn = parse_quote! {
            pub async fn echo(
                test: TestProxy,
                cmd: WhateverCommand,
                ) -> anyhow::Result<()> { Ok(()) }
        };
        let plugin: ItemFn = parse_quote! {
            pub async fn ffx_plugin_impl<D, R, DFut, RFut, E, EFut>(
                _daemon_factory: D,
                remote_factory: R,
                _is_experiment: E,
                cmd: WhateverCommand
            ) -> anyhow::Result<()>
                where
                D: FnOnce() -> DFut,
                DFut: std::future::Future<
                    Output = anyhow::Result<fidl_fuchsia_developer_bridge::DaemonProxy>,
                >,
                R: FnOnce() -> RFut,
                RFut: std::future::Future<
                    Output = anyhow::Result<
                        fidl_fuchsia_developer_remotecontrol::RemoteControlProxy
                    >,
                >,
                E: FnOnce(&'static str) -> EFut,
                EFut: std::future::Future<Output = bool>,
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
                        anyhow::bail!("There was an error getting proxies from the Remote Control Service: {}", e)
                    }
                }
            }
        };
        let fake_test: ItemFn = parse_quote! {
            #[cfg(test)]
            fn setup_fake_test<R:'static>(handle_request: R) -> TestProxy
                where R: FnOnce(TestRequest) + std::marker::Send + Copy
            {
                use futures::TryStreamExt;
                let (proxy, mut stream) =
                    fidl::endpoints::create_proxy_and_stream::<TestMarker>().unwrap();
                fuchsia_async::Task::spawn(async move {
                    while let Ok(Some(req)) = stream.try_next().await {
                        handle_request(req);
                    }
                })
                .detach();
                proxy
            }
        };
        let result: WrappedFunction = parse2(ffx_plugin(original.clone(), proxies)?)?;
        assert_eq!(original, result.original);
        assert_eq!(plugin, result.plugin);
        assert_eq!(2, result.fake_tests.len());
        assert_eq!(fake_test, result.fake_tests[0]);
        Ok(())
    }

    #[test]
    fn test_ffx_plugin_with_multiple_proxy_map_and_command() -> Result<(), Error> {
        let mut map = HashMap::new();
        map.insert("TestProxy".to_string(), "test".to_string());
        map.insert("FooProxy".to_string(), "foo".to_string());
        let proxies = ProxyMap { map, ..Default::default() };
        let original: ItemFn = parse_quote! {
            pub async fn echo(
                foo: FooProxy,
                cmd: WhateverCommand,
                test: TestProxy,
                ) -> anyhow::Result<()> { Ok(()) }
        };
        let plugin: ItemFn = parse_quote! {
            pub async fn ffx_plugin_impl<D, R, DFut, RFut, E, EFut>(
                _daemon_factory: D,
                remote_factory: R,
                _is_experiment: E,
                cmd: WhateverCommand
            ) -> anyhow::Result<()>
                where
                D: FnOnce() -> DFut,
                DFut: std::future::Future<
                    Output = anyhow::Result<fidl_fuchsia_developer_bridge::DaemonProxy>,
                >,
                R: FnOnce() -> RFut,
                RFut: std::future::Future<
                    Output = anyhow::Result<
                        fidl_fuchsia_developer_remotecontrol::RemoteControlProxy
                    >,
                >,
                E: FnOnce(&'static str) -> EFut,
                EFut: std::future::Future<Output = bool>,
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
                        anyhow::bail!("There was an error getting proxies from the Remote Control Service: {}", e)
                    }
                }
            }
        };
        let fake_foo_test: ItemFn = parse_quote! {
            #[cfg(test)]
            fn setup_fake_foo<R:'static>(handle_request: R) -> FooProxy
                where R: FnOnce(FooRequest) + std::marker::Send + Copy
            {
                use futures::TryStreamExt;
                let (proxy, mut stream) =
                    fidl::endpoints::create_proxy_and_stream::<FooMarker>().unwrap();
                fuchsia_async::Task::spawn(async move {
                    while let Ok(Some(req)) = stream.try_next().await {
                        handle_request(req);
                    }
                })
                .detach();
                proxy
            }
        };
        let fake_test_test: ItemFn = parse_quote! {
            #[cfg(test)]
            fn setup_fake_test<R:'static>(handle_request: R) -> TestProxy
                where R: FnOnce(TestRequest) + std::marker::Send + Copy
            {
                use futures::TryStreamExt;
                let (proxy, mut stream) =
                    fidl::endpoints::create_proxy_and_stream::<TestMarker>().unwrap();
                fuchsia_async::Task::spawn(async move {
                    while let Ok(Some(req)) = stream.try_next().await {
                        handle_request(req);
                    }
                })
                .detach();
                proxy
            }
        };
        let result: WrappedFunction = parse2(ffx_plugin(original.clone(), proxies)?)?;
        assert_eq!(original, result.original);
        assert_eq!(plugin, result.plugin);
        assert_eq!(4, result.fake_tests.len());
        assert_eq!(fake_foo_test, result.fake_tests[0]);
        assert_eq!(fake_test_test, result.fake_tests[2]);
        Ok(())
    }

    #[test]
    fn test_ffx_plugin_with_multiple_proxy_map_and_command_and_experiment_key() -> Result<(), Error>
    {
        let mut map = HashMap::new();
        map.insert("TestProxy".to_string(), "test".to_string());
        map.insert("FooProxy".to_string(), "foo".to_string());
        let experiment_key = Some("foo_key".to_string());
        let proxies = ProxyMap { map, experiment_key };
        let original: ItemFn = parse_quote! {
            pub async fn echo(
                foo: FooProxy,
                cmd: WhateverCommand,
                test: TestProxy,
                ) -> anyhow::Result<()> { Ok(()) }
        };
        let plugin: ItemFn = parse_quote! {
            pub async fn ffx_plugin_impl<D, R, DFut, RFut, E, EFut>(
                _daemon_factory: D,
                remote_factory: R,
                is_experiment: E,
                cmd: WhateverCommand
            ) -> anyhow::Result<()>
                where
                D: FnOnce() -> DFut,
                DFut: std::future::Future<
                    Output = anyhow::Result<fidl_fuchsia_developer_bridge::DaemonProxy>,
                >,
                R: FnOnce() -> RFut,
                RFut: std::future::Future<
                    Output = anyhow::Result<
                        fidl_fuchsia_developer_remotecontrol::RemoteControlProxy
                    >,
                >,
                E: FnOnce(&'static str) -> EFut,
                EFut: std::future::Future<Output = bool>,
            {
                if is_experiment("foo_key").await {
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
                            anyhow::bail!("There was an error getting proxies from the Remote Control Service: {}", e)
                        }
                    }
                } else {
                    println!("This is an experimental subcommand.  To enable this subcommand run 'ffx config set {} true'", "foo_key");
                    Ok(())
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
        let proxies = Default::default();
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
        let proxies = Default::default();
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
        let proxies = Default::default();
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

    #[test]
    fn test_experiment_key_is_none_when_empty() -> Result<(), Error> {
        let result: ProxyMap = parse2(quote! {})?;
        assert_eq!(result.experiment_key, None);
        Ok(())
    }

    #[test]
    fn test_experiment_key_literal() -> Result<(), Error> {
        let result: ProxyMap = parse2(quote! {"test"})?;
        assert_eq!(result.experiment_key, Some("test".to_string()));
        Ok(())
    }

    #[test]
    fn test_experiment_key_literal_with_trailing_comma() -> Result<(), Error> {
        let result: ProxyMap = parse2(quote! {"test",})?;
        assert_eq!(result.experiment_key, Some("test".to_string()));
        Ok(())
    }

    #[test]
    fn test_experiment_key_before_mapping() -> Result<(), Error> {
        let (test1_key, test1_value, test1_proxy_mapping) =
            proxy_map_test_value("test1".to_string());
        let (test2_key, test2_value, test2_proxy_mapping) =
            proxy_map_test_value("test2".to_string());
        let ex_key = "test_experimental_key".to_string();
        let proxy_map: ProxyMap = parse_quote! {
            #ex_key,
            #test1_proxy_mapping,
            #test2_proxy_mapping
        };
        assert_eq!(proxy_map.map.get(&test1_key), Some(&test1_value));
        assert_eq!(proxy_map.map.get(&test2_key), Some(&test2_value));
        assert_eq!(proxy_map.experiment_key, Some(ex_key));
        Ok(())
    }

    #[test]
    fn test_experiment_key_after_mapping() -> Result<(), Error> {
        let (test1_key, test1_value, test1_proxy_mapping) =
            proxy_map_test_value("test1".to_string());
        let (test2_key, test2_value, test2_proxy_mapping) =
            proxy_map_test_value("test2".to_string());
        let ex_key = "test_experimental_key".to_string();
        let proxy_map: ProxyMap = parse_quote! {
            #test1_proxy_mapping,
            #test2_proxy_mapping,
            #ex_key,
        };
        assert_eq!(proxy_map.map.get(&test1_key), Some(&test1_value));
        assert_eq!(proxy_map.map.get(&test2_key), Some(&test2_value));
        assert_eq!(proxy_map.experiment_key, Some(ex_key));
        Ok(())
    }

    #[test]
    fn test_experiment_key_in_the_middle_of_the_mapping() -> Result<(), Error> {
        let (test1_key, test1_value, test1_proxy_mapping) =
            proxy_map_test_value("test1".to_string());
        let (test2_key, test2_value, test2_proxy_mapping) =
            proxy_map_test_value("test2".to_string());
        let ex_key = "test_experimental_key".to_string();
        let proxy_map: ProxyMap = parse_quote! {
            #test1_proxy_mapping,
            #ex_key,
            #test2_proxy_mapping,
        };
        assert_eq!(proxy_map.map.get(&test1_key), Some(&test1_value));
        assert_eq!(proxy_map.map.get(&test2_key), Some(&test2_value));
        assert_eq!(proxy_map.experiment_key, Some(ex_key));
        Ok(())
    }

    #[test]
    fn test_multiple_experiment_keys_should_err() -> Result<(), Error> {
        let ex_key = "test_experimental_key".to_string();
        let ex_key_2 = "test_experimental_key_2".to_string();
        let result: Result<ProxyMap, Error> = parse2(quote! {
            #ex_key,
            #ex_key_2,
        });
        assert!(result.is_err());
        Ok(())
    }
}
