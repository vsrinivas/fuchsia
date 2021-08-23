// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_diagnostics::{ComponentSelector, Selector, StringSelector, TreeSelector},
    lazy_static::lazy_static,
    proc_macro2::{Punct, Span, TokenStream},
    quote::quote,
    std::collections::HashMap,
    syn::{
        parse::{Parse, ParseStream},
        punctuated::Punctuated,
        spanned::Spanned,
        token::Comma,
        AngleBracketedGenericArguments, Error, FnArg, GenericArgument, Ident, ItemFn, ItemStruct,
        Lit, LitStr, Pat, PatIdent, PatType, PathArguments, PathSegment, Token,
        Type::Path,
        TypePath,
    },
};

const UNKNOWN_PROXY_TYPE: &str = "This argument was not recognized. Possible arguments include \
                                  proxy types as well as Result and Option wrappers for proxy \
                                  types.";

const EXPECTED_SIGNATURE: &str = "ffx_plugin expects at least the command created in the args.rs \
                                  file and will accept FIDL proxies if mapped in the ffx_plugin \
                                  annotation.";

const UNRECOGNIZED_PARAMETER: &str = "If this is a proxy, make sure the parameter's type matches \
                                      the mapping passed into the ffx_plugin attribute.";

const DAEMON_SERVICE_IDENT: &str = "daemon::service";

lazy_static! {
    static ref KNOWN_PROXIES: Vec<(&'static str, &'static str, bool)> = vec![
        ("RemoteControlProxy", "remote_factory", true),
        ("DaemonProxy", "daemon_factory", true),
        ("FastbootProxy", "fastboot_factory", true),
        ("TargetControlProxy", "target_factory", true),
        ("VersionInfo", "build_info", false),
    ];
}

pub fn ffx_command(input: ItemStruct) -> TokenStream {
    let cmd = input.ident.clone();
    quote! {
        #input
        pub type FfxPluginCommand = #cmd;
    }
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

fn generate_fake_test_proxy_method(
    proxy_name: Ident,
    qualified_proxy_type: &syn::Path,
) -> TokenStream {
    let method_name = Ident::new(&format!("setup_fake_{}", proxy_name), Span::call_site());
    // Oneshot method is needed only for the 'component run' unit tests that leaks memory
    // everywhere unless shut down from the server side.
    let oneshot_method_name =
        Ident::new(&format!("setup_oneshot_fake_{}", proxy_name), Span::call_site());
    quote! {
        #[cfg(test)]
        fn #method_name<R:'static>(mut handle_request: R) -> #qualified_proxy_type
            where R: FnMut(fidl::endpoints::Request<<#qualified_proxy_type as fidl::endpoints::Proxy>::Protocol>) + std::marker::Send
        {
            use futures::TryStreamExt;
            let (proxy, mut stream) =
                fidl::endpoints::create_proxy_and_stream::<<#qualified_proxy_type as fidl::endpoints::Proxy>::Protocol>().unwrap();
            fuchsia_async::Task::local(async move {
                while let Ok(Some(req)) = stream.try_next().await {
                    handle_request(req);
                }
            })
            .detach();
            proxy
        }

        #[cfg(test)]
        fn #oneshot_method_name<R:'static>(mut handle_request: R) -> #qualified_proxy_type
            where R: FnMut(fidl::endpoints::Request<<#qualified_proxy_type as fidl::endpoints::Proxy>::Protocol>) + std::marker::Send
        {
            use futures::TryStreamExt;
            let (proxy, mut stream) =
                fidl::endpoints::create_proxy_and_stream::<<#qualified_proxy_type as fidl::endpoints::Proxy>::Protocol>().unwrap();
            fuchsia_async::Task::local(async move {
                if let Ok(Some(req)) = stream.try_next().await {
                    handle_request(req);
                }
            })
            .detach();
            proxy
        }
    }
}

struct GeneratedCodeParts {
    args: Punctuated<TokenStream, Token!(,)>,
    futures: Punctuated<Ident, Token!(,)>,
    future_results: Punctuated<Ident, Token!(,)>,
    proxies_to_generate: Vec<TokenStream>,
    test_fake_methods_to_generate: Vec<TokenStream>,
    cmd: FnArg,
}

fn parse_arguments(
    args: Punctuated<FnArg, Comma>,
    proxies: &ProxyMap,
) -> Result<GeneratedCodeParts, Error> {
    let mut inner_args: Punctuated<TokenStream, Token!(,)> = Punctuated::new();
    let mut futures: Punctuated<Ident, Token!(,)> = Punctuated::new();
    let mut future_results: Punctuated<Ident, Token!(,)> = Punctuated::new();
    let mut proxies_to_generate = Vec::new();
    let mut test_fake_methods_to_generate = Vec::<TokenStream>::new();
    let mut cmd: Option<FnArg> = None;
    for arg in &args {
        match arg.clone() {
            FnArg::Receiver(_) => {
                return Err(Error::new(
                    arg.span(),
                    "ffx plugin method signature cannot contain self",
                ))
            }
            FnArg::Typed(PatType { ty, pat, .. }) => match ty.as_ref() {
                Path(TypePath { path, .. }) => {
                    if let Some(GeneratedProxyParts {
                        arg,
                        fut,
                        fut_res,
                        implementation,
                        testing,
                    }) = generate_known_proxy(&pat, path)?
                    {
                        inner_args.push(arg);
                        futures.push(fut);
                        future_results.push(fut_res);
                        proxies_to_generate.push(implementation);
                        test_fake_methods_to_generate.push(testing);
                    } else if let Some(GeneratedProxyParts {
                        arg,
                        fut,
                        fut_res,
                        implementation,
                        testing,
                    }) = generate_mapped_proxy(proxies, &pat, path)?
                    {
                        inner_args.push(arg);
                        futures.push(fut);
                        future_results.push(fut_res);
                        proxies_to_generate.push(implementation);
                        test_fake_methods_to_generate.push(testing);
                    } else if let Some(GeneratedProxyParts {
                        arg,
                        fut,
                        fut_res,
                        implementation,
                        testing,
                    }) = generate_daemon_service_proxy(proxies, &pat, path)?
                    {
                        inner_args.push(arg);
                        futures.push(fut);
                        future_results.push(fut_res);
                        proxies_to_generate.push(implementation);
                        test_fake_methods_to_generate.push(testing);
                    } else if let Some(command) = parse_argh_command(&pat) {
                        // This SHOULD be the argh command - and there should only be one.
                        if let Some(_) = cmd {
                            return Err(Error::new(
                                arg.span(),
                                format!(
                                    "ffx_plugin could not recognize the parameters: {} \n{}",
                                    command.ident.clone(),
                                    UNRECOGNIZED_PARAMETER
                                ),
                            ));
                        } else {
                            let ident = command.ident.clone();
                            if command.mutability.is_some() {
                                if let FnArg::Typed(p) = arg.clone() {
                                    let new_pat = Box::new(Pat::Ident(PatIdent {
                                        mutability: None,
                                        ..command
                                    }));
                                    cmd = Some(FnArg::Typed(PatType { ty, pat: new_pat, ..p }));
                                } else {
                                    cmd = Some(arg.clone());
                                }
                            } else {
                                cmd = Some(arg.clone());
                            }
                            inner_args.push(quote! { #ident });
                        }
                    } else {
                        if let Pat::Ident(pat_ident) = pat.as_ref() {
                            return Err(Error::new(
                                arg.span(),
                                format!(
                                    "ffx_plugin could not recognize the parameter: {}\n{}",
                                    pat_ident.ident.clone(),
                                    UNRECOGNIZED_PARAMETER
                                ),
                            ));
                        } else {
                            return Err(Error::new(arg.span(), EXPECTED_SIGNATURE));
                        }
                    }
                }
                _ => return Err(Error::new(arg.span(), EXPECTED_SIGNATURE)),
            },
        }
    }

    if let Some(cmd) = cmd {
        Ok(GeneratedCodeParts {
            args: inner_args,
            futures,
            future_results,
            proxies_to_generate,
            test_fake_methods_to_generate,
            cmd,
        })
    } else {
        Err(Error::new(args.span(), EXPECTED_SIGNATURE))
    }
}

fn parse_argh_command(pattern_type: &Box<Pat>) -> Option<PatIdent> {
    if let Pat::Ident(pat_ident) = pattern_type.as_ref() {
        Some(pat_ident.clone())
    } else {
        None
    }
}

enum ProxyWrapper<'a> {
    Option(&'a syn::Path),
    Result(&'a syn::Path),
    None(&'a syn::Path),
}

impl ProxyWrapper<'_> {
    pub fn unwrap(&self) -> &syn::Path {
        match *self {
            ProxyWrapper::Option(ref p) => p,
            ProxyWrapper::Result(ref p) => p,
            ProxyWrapper::None(ref p) => p,
        }
    }

    pub fn result_stream(&self, result: &Ident) -> TokenStream {
        match self {
            ProxyWrapper::Option(_) => quote! { #result.ok() },
            ProxyWrapper::Result(_) => quote! { #result },
            ProxyWrapper::None(_) => quote! { #result? },
        }
    }

    pub fn map_result_stream(&self, result: &Ident, mapping: &Ident) -> TokenStream {
        match self {
            ProxyWrapper::Option(_) => quote! { #result.map(|_| #mapping).ok() },
            ProxyWrapper::Result(_) => quote! { #result.map(|_| #mapping) },
            ProxyWrapper::None(_) => quote! { #result.map(|_| #mapping)? },
        }
    }
}

fn extract_proxy_type(proxy_type_path: &syn::Path) -> Result<ProxyWrapper<'_>, Error> {
    if proxy_type_path.segments.last().is_none() {
        return Err(Error::new(proxy_type_path.span(), UNKNOWN_PROXY_TYPE));
    }
    let simple_proxy_type =
        proxy_type_path.segments.last().expect("proxy path should not be empty");
    match &simple_proxy_type.arguments {
        PathArguments::AngleBracketed(AngleBracketedGenericArguments { args, .. }) => {
            match args.first() {
                Some(GenericArgument::Type(Path(TypePath { path, .. }))) => {
                    let option_ident = Ident::new("Option", Span::call_site());
                    let result_ident = Ident::new("Result", Span::call_site());
                    match path.segments.last() {
                        Some(PathSegment { .. }) => {
                            if simple_proxy_type.ident == option_ident {
                                Ok(ProxyWrapper::Option(&path))
                            } else if simple_proxy_type.ident == result_ident {
                                Ok(ProxyWrapper::Result(&path))
                            } else {
                                Err(Error::new(simple_proxy_type.span(), UNKNOWN_PROXY_TYPE))
                            }
                        }
                        _ => Err(Error::new(simple_proxy_type.span(), UNKNOWN_PROXY_TYPE)),
                    }
                }
                _ => Err(Error::new(simple_proxy_type.span(), UNKNOWN_PROXY_TYPE)),
            }
        }
        PathArguments::None => Ok(ProxyWrapper::None(&proxy_type_path)),
        _ => Err(Error::new(simple_proxy_type.span(), UNKNOWN_PROXY_TYPE)),
    }
}

fn generate_known_proxy(
    pattern_type: &Box<Pat>,
    path: &syn::Path,
) -> Result<Option<GeneratedProxyParts>, Error> {
    let proxy_wrapper_type = extract_proxy_type(path)?;
    let proxy_type_path = proxy_wrapper_type.unwrap();
    let proxy_type = match proxy_type_path.segments.last() {
        Some(last) => last,
        None => return Err(Error::new(proxy_type_path.span(), UNKNOWN_PROXY_TYPE)),
    };
    for known_proxy in KNOWN_PROXIES.iter() {
        if proxy_type.ident == Ident::new(known_proxy.0, Span::call_site()) {
            if let Pat::Ident(pat_ident) = pattern_type.as_ref() {
                let factory_name = Ident::new(known_proxy.1, Span::call_site());
                let output_fut = Ident::new(&format!("{}_fut", factory_name), Span::call_site());
                let output_fut_res =
                    Ident::new(&format!("{}_fut_res", factory_name), Span::call_site());
                let implementation = quote! { let #output_fut = injector.#factory_name(); };
                let testing = if known_proxy.2 {
                    generate_fake_test_proxy_method(pat_ident.ident.clone(), proxy_type_path)
                } else {
                    quote! {}
                };

                let arg = proxy_wrapper_type.result_stream(&output_fut_res);
                return Ok(Some(GeneratedProxyParts {
                    arg,
                    fut: output_fut,
                    fut_res: output_fut_res,
                    implementation,
                    testing,
                }));
            }
        }
    }
    Ok(None)
}

fn generate_daemon_service_proxy(
    proxies: &ProxyMap,
    pattern_type: &Box<Pat>,
    path: &syn::Path,
) -> Result<Option<GeneratedProxyParts>, Error> {
    let proxy_wrapper_type = extract_proxy_type(path)?;
    let proxy_type_path = proxy_wrapper_type.unwrap();
    let daemon_service_name = qualified_name(proxy_type_path);
    let res = proxies.map.get(&daemon_service_name).and_then(|mapping| {
        if mapping != "daemon::service" {
            return None;
        }
        if let Pat::Ident(pat_ident) = pattern_type.as_ref() {
            let output = pat_ident.ident.clone();
            let output_fut = Ident::new(&format!("{}_fut", output), Span::call_site());
            let output_fut_res = Ident::new(&format!("{}_fut_res", output), Span::call_site());
            let server_end = Ident::new(&format!("{}_server_end", output), Span::call_site());
            // TODO(awdavies): When there is a component to test if a service exists, add the test
            // command for it in the daemon.
            let implementation = quote! {
                let (#output, #server_end) = fidl::endpoints::create_endpoints::<<#path as fidl::endpoints::Proxy>::Protocol>()?;
                let #output = #output.into_proxy()?;
                let #output_fut;
                {
                    let svc_name = <<#path as fidl::endpoints::Proxy>::Protocol as fidl::endpoints::DiscoverableProtocolMarker>::PROTOCOL_NAME;
                    use futures::TryFutureExt;
                    #output_fut = injector.daemon_factory().await?.connect_to_service(
                        svc_name,
                        #server_end.into_channel(),
                    ).map_ok_or_else(|e| anyhow::Result::<()>::Err(anyhow::anyhow!(e)), move |fidl_result| {
                        fidl_result
                        .map(|_| ())
                        .map_err(|_| errors::ffx_error!(format!(
"The daemon service '{}' did not match any services on the daemon.
If you are not developing this plugin or the service it connects to, then this is a bug.

Please report it at http://fxbug.dev/new?template=ffx+User+Bug.", svc_name)).into())
                    });
                }
            };
            let arg = proxy_wrapper_type.map_result_stream(&output_fut_res, &output);
            let testing = generate_fake_test_proxy_method(pat_ident.ident.clone(), path);
            return Some(GeneratedProxyParts {
                arg,
                fut: output_fut,
                fut_res: output_fut_res,
                implementation,
                testing,
            })
        }
        None
    });
    Ok(res)
}

struct GeneratedProxyParts {
    arg: TokenStream,
    fut: Ident,
    fut_res: Ident,
    implementation: TokenStream,
    testing: TokenStream,
}

fn generate_mapped_proxy(
    proxies: &ProxyMap,
    pattern_type: &Box<Pat>,
    path: &syn::Path,
) -> Result<Option<GeneratedProxyParts>, Error> {
    let proxy_wrapper_type = extract_proxy_type(path)?;
    let proxy_type_path = proxy_wrapper_type.unwrap();
    let qualified_proxy_name = qualified_name(proxy_type_path);
    if let Some(mapping) = proxies.map.get(&qualified_proxy_name) {
        let mapping_lit = LitStr::new(mapping, Span::call_site());
        if mapping_lit.value() == DAEMON_SERVICE_IDENT {
            return Ok(None);
        }
        if let Pat::Ident(pat_ident) = pattern_type.as_ref() {
            let output = pat_ident.ident.clone();
            let output_fut = Ident::new(&format!("{}_fut", output), Span::call_site());
            let output_fut_res = Ident::new(&format!("{}_fut_res", output), Span::call_site());
            let server_end = Ident::new(&format!("{}_server_end", output), Span::call_site());
            let selector = Ident::new(&format!("{}_selector", output), Span::call_site());
            let implementation = generate_proxy_from_selector(
                proxy_type_path,
                mapping,
                mapping_lit,
                &output,
                &output_fut,
                server_end,
                selector,
            );
            let testing = generate_fake_test_proxy_method(pat_ident.ident.clone(), proxy_type_path);
            let arg = proxy_wrapper_type.map_result_stream(&output_fut_res, &output);
            return Ok(Some(GeneratedProxyParts {
                arg,
                fut: output_fut,
                fut_res: output_fut_res,
                implementation,
                testing,
            }));
        }
    }
    Ok(None)
}

fn generate_proxy_from_selector(
    path: &syn::Path,
    mapping: &String,
    mapping_lit: LitStr,
    output: &Ident,
    output_fut: &Ident,
    server_end: Ident,
    selector: Ident,
) -> TokenStream {
    quote! {
        let (#output, #server_end) =
            fidl::endpoints::create_proxy::<<#path as fidl::endpoints::Proxy>::Protocol>()?;
        let #selector =
            selectors::parse_selector(#mapping_lit)?;
        let #output_fut =
        async {
            // This needs to be a block in order for this `use` to avoid conflicting with a plugins own `use` for this trait.
            use futures::TryFutureExt;
            injector.remote_factory().await?
            .connect(#selector, #server_end.into_channel())
            .map_ok_or_else(|e| Result::<(), anyhow::Error>::Err(anyhow::anyhow!(e)), |fidl_result| {
                fidl_result
                .map(|_| ())
                .map_err(|e| {
                    match e {
                        fidl_fuchsia_developer_remotecontrol::ConnectError::NoMatchingServices => {
                            errors::ffx_error!(format!(
"The plugin service selector '{}' did not match any services on the target.
If you are not developing this plugin, then this is a bug. Please report it at http://fxbug.dev/new?template=ffx+User+Bug.

Plugin developers: you can use `ffx component select '<selector>'` to explore the component topology of your target device and fix this selector.",
                            #mapping)).into()
                        }
                        fidl_fuchsia_developer_remotecontrol::ConnectError::MultipleMatchingServices => {
                            errors::ffx_error!(
                                format!(
"Plugin service selectors must match exactly one service, but '{}' matched multiple services on the target.
If you are not developing this plugin, then this is a bug. Please report it at http://fxbug.dev/new?template=ffx+User+Bug.

Plugin developers: you can use `ffx component select '{}'` to see which services matched the provided selector.",
                                    #mapping, #mapping)).into()
                        }
                        _ => {
                            anyhow::anyhow!(
                                format!("This service dependency exists but connecting to it failed with error {:?}. Selector: {}.", e, #mapping)
                            )
                        }
                    }
                })
            }).await
        };
    }
}

pub fn ffx_plugin(input: ItemFn, proxies: ProxyMap) -> Result<TokenStream, Error> {
    let method = input.sig.ident.clone();
    let asyncness = if let Some(_) = input.sig.asyncness {
        quote! {.await}
    } else {
        quote! {}
    };
    let return_type = input.sig.output.clone();

    let GeneratedCodeParts {
        args,
        futures,
        future_results,
        proxies_to_generate,
        test_fake_methods_to_generate,
        cmd,
    } = parse_arguments(input.sig.inputs.clone(), &proxies)?;

    let mut outer_args: Punctuated<_, Token!(,)> = Punctuated::new();
    outer_args.push(quote! {injector: I});
    outer_args.push(quote! {#cmd});

    let implementation = if proxies_to_generate.len() > 0 {
        quote! {
            #(#proxies_to_generate)*
            let (#future_results,) = futures::join!(#futures);
            #method(#args)#asyncness
        }
    } else {
        quote! {
            #(#proxies_to_generate)*
            #method(#args)#asyncness
        }
    };

    let gated_impl = if let Some(key) = proxies.experiment_key {
        quote! {
            if injector.is_experiment(#key).await {
                #implementation
            } else {
                // The user did not opt-in for the feature. This will exit with a non-zero status
                // after displaying the error message.
                errors::ffx_bail!(
                    "This is an experimental subcommand.  To enable this subcommand run 'ffx config set {} true'",
                    #key
                )
            }
        }
    } else {
        implementation
    };

    let res = quote! {
        #input
        pub async fn ffx_plugin_impl<I: ffx_core::Injector>(#outer_args) #return_type {
            #gated_impl
        }

        #(#test_fake_methods_to_generate)*
    };
    Ok(res)
}

fn cmp_string_selector(selector: &StringSelector, expected: &str) -> bool {
    match selector {
        StringSelector::ExactMatch(s) => s == expected,
        StringSelector::StringPattern(s) => s == expected,
        _ => false,
    }
}

fn is_v1_moniker(span: Span, selector: ComponentSelector) -> Result<bool, Error> {
    let segments = selector
        .moniker_segments
        .as_ref()
        .ok_or(Error::new(span, format!("Got an invalid component selector. {:?}", selector)))?;
    return Ok(segments.len() == 2
        && cmp_string_selector(segments.get(0).unwrap(), "core")
        && cmp_string_selector(segments.get(1).unwrap(), "appmgr"));
}

fn is_namespace(span: Span, selector: &TreeSelector, namespace: &str) -> Result<bool, Error> {
    match selector {
        TreeSelector::SubtreeSelector(ref subtree) => {
            if subtree.node_path.is_empty() {
                return Err(Error::new(
                    span,
                    format!("Got an invalid tree selector. {:?}", selector),
                ));
            }
            Ok(cmp_string_selector(subtree.node_path.get(0).unwrap(), namespace))
        }
        TreeSelector::PropertySelector(ref selector) => {
            if selector.node_path.is_empty() {
                return Err(Error::new(
                    span,
                    format!("Got an invalid tree selector. {:?}", selector),
                ));
            }
            Ok(cmp_string_selector(selector.node_path.get(0).unwrap(), namespace))
        }
        _ => Err(Error::new(span, "Compiled with an unexpected TreeSelector variant.")),
    }
}

fn has_wildcard(span: Span, selector: &StringSelector) -> Result<bool, Error> {
    match selector {
        StringSelector::ExactMatch(_) => Ok(false),
        StringSelector::StringPattern(s) => Ok(s.contains("*")),
        _ => Err(Error::new(span, "Compiled with an unexpected StringSelector variant.")),
    }
}

fn any_wildcards(span: Span, selectors: &Vec<StringSelector>) -> Result<bool, Error> {
    for selector in selectors.iter() {
        if has_wildcard(span, selector)? {
            return Ok(true);
        }
    }
    return Ok(false);
}

fn has_wildcards(span: Span, selector: &Selector) -> Result<bool, Error> {
    let moniker = selector.component_selector.as_ref().unwrap();
    if any_wildcards(span, moniker.moniker_segments.as_ref().unwrap())? {
        return Ok(true);
    }

    let tree_selector = selector.tree_selector.as_ref().unwrap();
    match tree_selector {
        TreeSelector::SubtreeSelector(subtree) => Ok(any_wildcards(span, &subtree.node_path)?),
        TreeSelector::PropertySelector(selector) => {
            if any_wildcards(span, &selector.node_path)? {
                Ok(true)
            } else {
                has_wildcard(span, &selector.target_properties)
            }
        }
        _ => Err(Error::new(span, "Compiled with an unexpected TreeSelector variant.")),
    }
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
                        let selector = if selection.value() == DAEMON_SERVICE_IDENT {
                            selection.value()
                        } else {
                            let parsed_selector = selectors::parse_selector(&selection.value())
                                .map_err(|e| {
                                    Error::new(
                                        selection.span(),
                                        format!("Invalid component selector string: {}", e),
                                    )
                                })?;

                            if has_wildcards(selection.span(), &parsed_selector)? {
                                return Err(Error::new(selection.span(), format!("Component selectors in plugin definitions cannot use wildcards ('*').")));
                            }
                            let moniker = parsed_selector.component_selector.unwrap();
                            let subdir = parsed_selector.tree_selector.unwrap();
                            if !is_v1_moniker(selection.span(), moniker)?
                                && !is_namespace(selection.span(), &subdir, "expose")?
                                && !is_namespace(selection.span(), &subdir, "in")?
                            {
                                return Err(Error::new(selection.span(), format!("Selectors for V2 components in plugin definitions must use `expose` or `in`. `out` is unsupported. See fxbug.dev/60910.")));
                            }
                            selection.value()
                        };
                        map.insert(qualified_name(&path), selector);
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
            parse2, parse_quote, Attribute, ItemType, ReturnType,
        },
    };

    const V1_SELECTOR: &str = "core/appmgr";

    struct WrappedCommand {
        original: ItemStruct,
        plugin: ItemType,
    }

    impl Parse for WrappedCommand {
        fn parse(input: ParseStream<'_>) -> Result<Self, Error> {
            Ok(WrappedCommand { original: input.parse()?, plugin: input.parse()? })
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
        ffx_plugin(original.clone(), proxies).map(|_| ())
    }

    #[test]
    fn test_non_async_ffx_plugin_with_just_a_command() -> Result<(), Error> {
        let proxies = Default::default();
        let original: ItemFn = parse_quote! {
            pub fn echo(_cmd: EchoCommand) -> anyhow::Result<()> { Ok(()) }
        };
        ffx_plugin(original.clone(), proxies).map(|_| ())
    }

    #[test]
    fn test_ffx_plugin_with_a_daemon_proxy_and_command() -> Result<(), Error> {
        let proxies = Default::default();
        let original: ItemFn = parse_quote! {
            pub async fn echo(
                daemon: DaemonProxy,
                _cmd: EchoCommand) -> anyhow::Result<()> { Ok(()) }
        };
        ffx_plugin(original.clone(), proxies).map(|_| ())
    }

    #[test]
    fn test_ffx_plugin_with_a_fastboot_proxy_and_command() -> Result<(), Error> {
        let proxies = Default::default();
        let original: ItemFn = parse_quote! {
            pub async fn echo(
                fastboot: FastbootProxy,
                _cmd: EchoCommand) -> anyhow::Result<()> { Ok(()) }
        };
        ffx_plugin(original.clone(), proxies).map(|_| ())
    }

    #[test]
    fn test_ffx_plugin_with_a_remote_proxy_and_command() -> Result<(), Error> {
        let proxies = Default::default();
        let original: ItemFn = parse_quote! {
            pub async fn echo(
                remote: RemoteControlProxy,
                _cmd: EchoCommand) -> anyhow::Result<()> { Ok(()) }
        };
        ffx_plugin(original.clone(), proxies).map(|_| ())
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
        ffx_plugin(original.clone(), proxies).map(|_| ())
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
        ffx_plugin(original.clone(), proxies).map(|_| ())
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
        ffx_plugin(original.clone(), proxies).map(|_| ())
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
        ffx_plugin(original.clone(), proxies).map(|_| ())
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
        ffx_plugin(original.clone(), proxies).map(|_| ())
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
    fn test_empty_proxy_map_should_not_err() {
        let _proxy_map: ProxyMap = parse_quote! {};
    }

    #[test]
    fn test_v2_proxy_map_succeeds() {
        let _proxy_map: ProxyMap = parse_quote! {test = "test:expose"};
    }

    #[test]
    fn test_v2_proxy_map_expose_with_service_succeeds() {
        let _proxy_map: ProxyMap = parse_quote! {test = "test:expose:anything"};
    }

    #[test]
    fn test_v1_proxy_map_using_out_succeeds() {
        let _proxy_map: ProxyMap = parse_quote! {test = "core/appmgr:out"};
    }

    #[test]
    fn test_v1_proxy_map_using_out_and_service_succeeds() {
        let _proxy_map: ProxyMap = parse_quote! {test = "core/appmgr:out:anything"};
    }

    fn proxy_map_test_value(test: String) -> (String, String, TokenStream) {
        let test_value = format!("{}:{}", V1_SELECTOR, test);
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
    fn test_invalid_selection_should_err() {
        let result: Result<ProxyMap, Error> = parse2(quote! {
            test = "test"
        });
        assert!(result.is_err());
    }

    #[test]
    fn test_v2_proxy_map_using_out_fails() {
        let result: Result<ProxyMap, Error> = parse2(quote! {
            test = "test:out"
        });
        assert!(result.is_err());
    }

    #[test]
    fn test_v2_proxy_map_not_using_expose_and_service_fails() {
        let result: Result<ProxyMap, Error> = parse2(quote! {
            test = "test:anything:anything"
        });
        assert!(result.is_err());
    }

    #[test]
    fn test_v2_proxy_map_using_in_ok() {
        let result: Result<ProxyMap, Error> = parse2(quote! {
            test = "test:in:anything"
        });
        assert!(result.is_ok());
    }

    #[test]
    fn test_v2_proxy_map_using_expose_ok() {
        let result: Result<ProxyMap, Error> = parse2(quote! {
            test = "test:expose:anything"
        });
        assert!(result.is_ok());
    }

    #[test]
    fn test_invalid_mapping_should_err() {
        let result: Result<ProxyMap, Error> = parse2(quote! {
            test, "test"
        });
        assert!(result.is_err());
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

    struct WrappedTestFunctions {
        fake_test: ItemFn,
        fake_oneshot_test: ItemFn,
    }

    impl Parse for WrappedTestFunctions {
        fn parse(input: ParseStream<'_>) -> Result<Self, Error> {
            let fake_test = input.parse()?;
            let fake_oneshot_test = input.parse()?;
            Ok(WrappedTestFunctions { fake_test, fake_oneshot_test })
        }
    }

    #[test]
    fn test_generated_test_functions_should_have_test_attribute() -> Result<(), Error> {
        let test = "test_proxy";
        let proxy_name = Ident::new(&format!("{}", test), Span::call_site());
        let qualified_proxy_type: syn::Path = parse2(quote! { test::TestProxy })?;
        let generated = generate_fake_test_proxy_method(proxy_name, &qualified_proxy_type);
        let result: WrappedTestFunctions = parse2(generated)?;
        let attribute_path: syn::Path = parse_quote! { cfg };
        assert_eq!(result.fake_test.attrs[0].path, attribute_path);
        assert_eq!(result.fake_oneshot_test.attrs[0].path, attribute_path);
        let expected_test_arg = Ident::new("test", Span::call_site());
        let mut attr_args: Ident = Attribute::parse_args(&result.fake_test.attrs[0])?;
        assert_eq!(attr_args, expected_test_arg);
        attr_args = Attribute::parse_args(&result.fake_oneshot_test.attrs[0])?;
        assert_eq!(attr_args, expected_test_arg);
        Ok(())
    }

    #[test]
    fn test_generated_test_functions_should_have_expected_names() -> Result<(), Error> {
        let test = "test_proxy";
        let proxy_name = Ident::new(&format!("{}", test), Span::call_site());
        let qualified_proxy_type: syn::Path = parse2(quote! { test::TestProxy })?;
        let generated = generate_fake_test_proxy_method(proxy_name, &qualified_proxy_type);
        let result: WrappedTestFunctions = parse2(generated)?;
        let expected_name = Ident::new(&format!("setup_fake_{}", test), Span::call_site());
        let expected_oneshot_name =
            Ident::new(&format!("setup_oneshot_fake_{}", test), Span::call_site());
        assert_eq!(result.fake_test.sig.ident, expected_name);
        assert_eq!(result.fake_oneshot_test.sig.ident, expected_oneshot_name);
        Ok(())
    }

    #[test]
    fn test_generated_test_functions_should_have_expected_return_type() -> Result<(), Error> {
        let test = "test_proxy";
        let proxy_name = Ident::new(&format!("{}", test), Span::call_site());
        let qualified_proxy_type: syn::Path = parse2(quote! { test::TestProxy })?;
        let generated = generate_fake_test_proxy_method(proxy_name, &qualified_proxy_type);
        let result: WrappedTestFunctions = parse2(generated)?;
        match result.fake_test.sig.output {
            ReturnType::Type(_, output_type) => match output_type.as_ref() {
                Path(TypePath { path, .. }) => assert_eq!(path, &qualified_proxy_type),
                _ => return Err(Error::new(Span::call_site(), "unexpected return type")),
            },
            _ => return Err(Error::new(Span::call_site(), "unexpected return type")),
        }
        match result.fake_oneshot_test.sig.output {
            ReturnType::Type(_, output_type) => match output_type.as_ref() {
                Path(TypePath { path, .. }) => assert_eq!(path, &qualified_proxy_type),
                _ => return Err(Error::new(Span::call_site(), "unexpected return type")),
            },
            _ => return Err(Error::new(Span::call_site(), "unexpected return type")),
        }
        Ok(())
    }

    #[test]
    fn test_generate_known_proxy_works_with_daemon_proxy() -> Result<(), Error> {
        let input: ItemFn = parse_quote!(
            fn test_fn(test_param: DaemonProxy) {}
        );
        let param = input.sig.inputs[0].clone();
        if let Some(GeneratedProxyParts { arg, fut, .. }) = match param {
            FnArg::Typed(PatType { ty, pat, .. }) => match ty.as_ref() {
                Path(TypePath { path, .. }) => generate_known_proxy(&pat, path)?,
                _ => return Err(Error::new(Span::call_site(), "unexpected param")),
            },
            _ => return Err(Error::new(Span::call_site(), "unexpected param")),
        } {
            assert_eq!(arg.to_string(), quote! { daemon_factory_fut_res? }.to_string());
            assert_eq!(fut.to_string(), quote! { daemon_factory_fut }.to_string());
            Ok(())
        } else {
            Err(Error::new(Span::call_site(), "known proxy not generated"))
        }
    }

    #[test]
    fn test_generate_known_proxy_works_with_remote_proxy() -> Result<(), Error> {
        let input: ItemFn = parse_quote!(
            fn test_fn(test_param: RemoteControlProxy) {}
        );
        let param = input.sig.inputs[0].clone();
        if let Some(GeneratedProxyParts { arg, fut, .. }) = match param {
            FnArg::Typed(PatType { ty, pat, .. }) => match ty.as_ref() {
                Path(TypePath { path, .. }) => generate_known_proxy(&pat, path)?,
                _ => return Err(Error::new(Span::call_site(), "unexpected param")),
            },
            _ => return Err(Error::new(Span::call_site(), "unexpected param")),
        } {
            assert_eq!(arg.to_string(), quote! { remote_factory_fut_res? }.to_string());
            assert_eq!(fut.to_string(), quote! { remote_factory_fut }.to_string());
            Ok(())
        } else {
            Err(Error::new(Span::call_site(), "known proxy not generated"))
        }
    }

    #[test]
    fn test_generate_known_proxy_works_with_fastboot_proxy() -> Result<(), Error> {
        let input: ItemFn = parse_quote!(
            fn test_fn(test_param: FastbootProxy) {}
        );
        let param = input.sig.inputs[0].clone();
        if let Some(GeneratedProxyParts { arg, fut, .. }) = match param {
            FnArg::Typed(PatType { ty, pat, .. }) => match ty.as_ref() {
                Path(TypePath { path, .. }) => generate_known_proxy(&pat, path)?,
                _ => return Err(Error::new(Span::call_site(), "unexpected param")),
            },
            _ => return Err(Error::new(Span::call_site(), "unexpected param")),
        } {
            assert_eq!(arg.to_string(), quote! { fastboot_factory_fut_res? }.to_string());
            assert_eq!(fut.to_string(), quote! { fastboot_factory_fut }.to_string());
            Ok(())
        } else {
            Err(Error::new(Span::call_site(), "known proxy not generated"))
        }
    }

    #[test]
    fn test_generate_known_proxy_does_not_generate_proxy_for_unknown_proxies() -> Result<(), Error>
    {
        let input: ItemFn = parse_quote!(
            fn test_fn(test_param: UnknownProxy) {}
        );
        let param = input.sig.inputs[0].clone();
        let result = match param {
            FnArg::Typed(PatType { ty, pat, .. }) => match ty.as_ref() {
                Path(TypePath { path, .. }) => generate_known_proxy(&pat, path)?,
                _ => return Err(Error::new(Span::call_site(), "unexpected param")),
            },
            _ => return Err(Error::new(Span::call_site(), "unexpected param")),
        };
        assert!(result.is_none());
        Ok(())
    }

    #[test]
    fn test_generate_mapped_proxy_generates_name_and_future() -> Result<(), Error> {
        let proxy_map: ProxyMap = parse_quote! { TestProxy= "test:expose:anything" };
        let input: ItemFn = parse_quote!(
            fn test_fn(test_param: TestProxy) {}
        );
        let param = input.sig.inputs[0].clone();
        if let Some(GeneratedProxyParts { arg, fut, .. }) = match param {
            FnArg::Typed(PatType { ty, pat, .. }) => match ty.as_ref() {
                Path(TypePath { path, .. }) => generate_mapped_proxy(&proxy_map, &pat, path)?,
                _ => return Err(Error::new(Span::call_site(), "unexpected param")),
            },
            _ => return Err(Error::new(Span::call_site(), "unexpected param")),
        } {
            assert_eq!(
                arg.to_string(),
                quote! { test_param_fut_res.map(|_| test_param)? }.to_string()
            );
            assert_eq!(fut, Ident::new("test_param_fut", Span::call_site()));
            Ok(())
        } else {
            Err(Error::new(Span::call_site(), "mappedproxy not generated"))
        }
    }

    #[test]
    fn test_generate_mapped_proxy_does_not_generate_unmapped_proxies() -> Result<(), Error> {
        let proxy_map: ProxyMap = parse_quote! { TestProxy= "test:expose:anything" };
        let input: ItemFn = parse_quote!(
            fn test_fn(test_param: AnotherTestProxy) {}
        );
        let param = input.sig.inputs[0].clone();
        let result = match param {
            FnArg::Typed(PatType { ty, pat, .. }) => match ty.as_ref() {
                Path(TypePath { path, .. }) => generate_mapped_proxy(&proxy_map, &pat, path)?,
                _ => return Err(Error::new(Span::call_site(), "unexpected param")),
            },
            _ => return Err(Error::new(Span::call_site(), "unexpected param")),
        };
        assert!(result.is_none());
        Ok(())
    }

    #[test]
    fn test_known_proxy_works_with_options() -> Result<(), Error> {
        let proxies = Default::default();
        let input: ItemFn = parse_quote! {
            fn test_fn(test_param: Option<test::DaemonProxy>, cmd: OptionCommand) -> Result<()> {}
        };
        ffx_plugin(input, proxies).map(|_| ())
    }

    #[test]
    fn test_known_proxy_works_with_results() -> Result<(), Error> {
        let proxies = Default::default();
        let input: ItemFn = parse_quote! {
            fn test_fn(test_param: Result<DaemonProxy>, cmd: ResultCommand) -> Result<()> {}
        };
        ffx_plugin(input, proxies).map(|_| ())
    }

    #[test]
    fn test_mapped_proxy_works_with_options() -> Result<(), Error> {
        let proxies: ProxyMap = parse_quote! { TestProxy= "test:expose:anything" };
        let input: ItemFn = parse_quote! {
            fn test_fn(test_param: Option<TestProxy>, cmd: ResultCommand) -> Result<()> {}
        };
        ffx_plugin(input, proxies).map(|_| ())
    }

    #[test]
    fn test_mapped_proxy_works_with_results() -> Result<(), Error> {
        let proxies: ProxyMap = parse_quote! { TestProxy= "test:expose:anything" };
        let input: ItemFn = parse_quote! {
            fn test_fn(test_param: Result<TestProxy>, cmd: ResultCommand) -> Result<()> {}
        };
        ffx_plugin(input, proxies).map(|_| ())
    }

    #[test]
    fn test_mapped_proxy_works_with_both_options_and_results() -> Result<(), Error> {
        let proxies: ProxyMap = parse_quote! {
            TestProxy = "test:expose:anything",
            TestProxy2 = "test:expose:anything"
        };
        let input: ItemFn = parse_quote! {
            fn test_fn(
                test_param: Result<TestProxy>,
                test_param_2: Option<TestProxy2>,
                cmd: ResultCommand) -> Result<()> {}
        };
        ffx_plugin(input, proxies).map(|_| ())
    }

    #[test]
    fn test_both_known_and_mapped_proxy_works_with_both_options_and_results() -> Result<(), Error> {
        let proxies: ProxyMap = parse_quote! {
            TestProxy = "test:expose:anything",
            TestProxy2 = "test:expose:anything"
        };
        let input: ItemFn = parse_quote! {
            fn test_fn(
                test_param: Result<TestProxy>,
                test_param_2: Option<TestProxy2>,
                daemon_proxy: DaemonProxy,
                fastboot_proxy: Option<FastbootProxy>,
                remote_proxy: Result<RemoteControlProxy>,
                cmd: ResultCommand) -> Result<()> {}
        };
        ffx_plugin(input, proxies).map(|_| ())
    }

    #[test]
    fn test_ffx_plugin_with_a_target_proxy_and_command() -> Result<(), Error> {
        let proxies = Default::default();
        let original: ItemFn = parse_quote! {
            pub async fn echo(
                target: TargetControlProxy,
                _cmd: EchoCommand) -> anyhow::Result<()> { Ok(()) }
        };
        ffx_plugin(original.clone(), proxies).map(|_| ())
    }

    #[test]
    fn test_known_proxy_works_with_custom_return_type() -> Result<(), Error> {
        let proxies = Default::default();
        let input: ItemFn = parse_quote! {
            fn test_fn(cmd: OptionCommand) -> Result<i32> {}
        };
        ffx_plugin(input, proxies).map(|_| ())
    }

    #[test]
    fn test_service_proxy_works() -> Result<(), Error> {
        let proxies: ProxyMap = parse_quote! { TestProxy = "daemon::service" };
        let input: ItemFn = parse_quote! {
            fn test_fn(test_param: TestProxy, cmd: ResultCommand) -> Result<()> {}
        };
        ffx_plugin(input, proxies).map(drop)
    }

    #[test]
    fn test_service_proxy_works_with_option() -> Result<(), Error> {
        let proxies: ProxyMap = parse_quote! { TestProxy = "daemon::service" };
        let input: ItemFn = parse_quote! {
                fn test_fn(test_param: Option<TestProxy>, cmd: ResultCommand) -> Result<()> {}
        };
        ffx_plugin(input, proxies).map(drop)
    }

    #[test]
    fn test_service_proxy_works_with_result() -> Result<(), Error> {
        let proxies: ProxyMap = parse_quote! { TestProxy = "daemon::service" };
        let input: ItemFn = parse_quote! {
                fn test_fn(test_param: Result<TestProxy>, cmd: ResultCommand) -> Result<()> {}
        };
        ffx_plugin(input, proxies).map(drop)
    }
}
