// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[macro_use]
extern crate quote;
#[macro_use]
extern crate syn;
extern crate proc_macro;

use core::fmt::Display;

use proc_macro::TokenStream;
use proc_macro2::{Span, TokenStream as TokenStream2};
use quote::ToTokens;
use syn::visit_mut::{self, VisitMut};
use syn::{
    punctuated::Punctuated, spanned::Spanned, AttrStyle, Attribute, Block, Error, Expr, ExprMatch,
    FnArg, GenericParam, Ident, Item, ItemFn, Pat, PatType, Path, Signature, Stmt, TypeImplTrait,
    TypeParamBound,
};

#[proc_macro_attribute]
pub fn specialize_ip(attr: TokenStream, input: TokenStream) -> TokenStream {
    specialize_ip_inner(attr, input, "specialize_ip", "Ip", "Ipv4", "Ipv6", "ipv4", "ipv6")
}

#[proc_macro_attribute]
pub fn specialize_ip_address(attr: TokenStream, input: TokenStream) -> TokenStream {
    specialize_ip_inner(
        attr,
        input,
        "specialize_ip_address",
        "IpAddress",
        "Ipv4Addr",
        "Ipv6Addr",
        "ipv4addr",
        "ipv6addr",
    )
}

// Hook the inner attributes so that we can print reasonable error messages if
// they're ever used outside of a #[specialize_ip[address]] function; otherwise,
// the error message is simply about an unrecognized attribute.

#[proc_macro_attribute]
pub fn ipv4(attr: TokenStream, _input: TokenStream) -> TokenStream {
    let attr: TokenStream2 = attr.into();
    Error::new_spanned(
        attr,
        "#[ipv4] attribute outside of #[specialize_ip] function or in unexpected location",
    )
    .to_compile_error()
    .into()
}

#[proc_macro_attribute]
pub fn ipv6(attr: TokenStream, _input: TokenStream) -> TokenStream {
    let attr: TokenStream2 = attr.into();
    Error::new_spanned(
        attr,
        "#[ipv6] attribute outside of #[specialize_ip] function or in unexpected location",
    )
    .to_compile_error()
    .into()
}

#[proc_macro_attribute]
pub fn ipv4addr(attr: TokenStream, _input: TokenStream) -> TokenStream {
    let attr: TokenStream2 = attr.into();
    Error::new_spanned(
        attr,
        "#[ipv4addr] attribute outside of #[specialize_ip_address] function or in unexpected location",
    )
    .to_compile_error()
    .into()
}

#[proc_macro_attribute]
pub fn ipv6addr(attr: TokenStream, _input: TokenStream) -> TokenStream {
    let attr: TokenStream2 = attr.into();
    Error::new_spanned(
        attr,
        "#[ipv6addr] attribute outside of #[specialize_ip_address] function or in unexpected location",
    )
    .to_compile_error()
    .into()
}

#[proc_macro_attribute]
pub fn ip_test(attr: TokenStream, input: TokenStream) -> TokenStream {
    ip_test_inner(attr, input, "ip_test", "Ip", "Ipv4", "Ipv6")
}

#[proc_macro_attribute]
pub fn ip_addr_test(attr: TokenStream, input: TokenStream) -> TokenStream {
    ip_test_inner(attr, input, "ip_addr_test", "IpAddress", "Ipv4Addr", "Ipv6Addr")
}

fn specialize_ip_inner(
    attr: TokenStream,
    input: TokenStream,
    attr_name: &'static str,
    trait_name: &'static str,
    ipv4_type_name: &str,
    ipv6_type_name: &str,
    ipv4_attr_name: &'static str,
    ipv6_attr_name: &'static str,
) -> TokenStream {
    assert!(attr.is_empty(), "#[specialize_ip]: unexpected attribute argument");
    let f = parse_macro_input!(input as ItemFn);
    let cfg = Config {
        attr_name,
        trait_name,
        ipv4_type_ident: Ident::new(ipv4_type_name, Span::call_site()),
        ipv6_type_ident: Ident::new(ipv6_type_name, Span::call_site()),
        ipv4_attr_name,
        ipv6_attr_name,
    };
    let parsed = parse_input(f, &cfg);
    serialize(parsed, &cfg)
}

fn ip_test_inner(
    attr: TokenStream,
    input: TokenStream,
    attr_name: &'static str,
    trait_name: &'static str,
    ipv4_type_name: &'static str,
    ipv6_type_name: &'static str,
) -> TokenStream {
    assert!(attr.is_empty(), "#[ip_test]: unexpected attribute argument");

    let item = parse_macro_input!(input as syn::ItemFn);
    let syn::ItemFn { attrs, vis, sig, block } = item;
    if let Err(e) = (|| {
        if let Some(gen) = sig.generics.params.first() {
            match gen {
                GenericParam::Type(tp) => {
                    if !tp.bounds.iter().any(|b| match b {
                        TypeParamBound::Trait(t) => t.path.is_ident(trait_name),
                        _ => false,
                    }) {
                        return Err(Error::new(
                            tp.bounds.span(),
                            format!("{} entry must have an {} type bound", attr_name, trait_name),
                        ));
                    }
                }
                _ => {
                    return Err(Error::new(
                        sig.generics.span(),
                        format!("{} entry must ONLY have a single type parameter", attr_name),
                    ))
                }
            }
        } else {
            return Err(Error::new(
                sig.fn_token.span,
                format!("{} entry must have exactly one type parameter", attr_name),
            ));
        }

        if !sig.inputs.is_empty() {
            return Err(Error::new(
                sig.inputs.span(),
                format!("{} entry takes no arguments", attr_name),
            ));
        }
        if let Some(dot3) = &sig.variadic {
            return Err(Error::new(
                dot3.dots.spans[0],
                format!("{} entry may not be variadic", attr_name),
            ));
        }
        Ok(())
    })() {
        return e.to_compile_error().into();
    }

    // we need to check the attributes given to the function, test attributes
    // like `should_panic` need to go to the concrete test definitions instead.
    let (test_attrs, attrs) =
        attrs.into_iter().partition::<Vec<_>, _>(|attr| attr.path.is_ident("should_panic"));
    // borrow here because `test_attrs` is used twice in `quote_spanned!` below.
    let test_attrs = &test_attrs;

    let span = sig.ident.span();
    let output = &sig.output;
    let ident = &sig.ident;
    let v4_test = Ident::new(&format!("{}_v4", ident), Span::call_site());
    let v6_test = Ident::new(&format!("{}_v6", ident), Span::call_site());

    let ipv4_type_ident = Ident::new(ipv4_type_name, Span::call_site());
    let ipv6_type_ident = Ident::new(ipv6_type_name, Span::call_site());

    let output = quote_spanned! { span =>
        #(#attrs)*
        #vis #sig {
            #block
        }

        #[test]
        #(#test_attrs)*
        fn #v4_test () #output {
           #ident::<#ipv4_type_ident>()
        }

        #[test]
        #(#test_attrs)*
        fn #v6_test () #output {
           #ident::<#ipv6_type_ident>()
        }
    };
    output.into()
}

// A configuration that allows us to implement most of the logic agnostic to
// whether we're implementing #[specialize_ip] or #[specialize_ip_address].
#[derive(Debug)]
struct Config {
    // specialize_ip or specialize_ip_address
    attr_name: &'static str,
    // Ip or IpAddr
    trait_name: &'static str,
    // Ipv4 or Ipv4Addr
    ipv4_type_ident: Ident,
    // Ipv6 or Ipv6Addr
    ipv6_type_ident: Ident,
    // ipv4 or ipv4addr
    ipv4_attr_name: &'static str,
    // ipv6 or ipv6addr
    ipv6_attr_name: &'static str,
}

// An input which has been parsed and modified and is ready for serialization.
struct Input {
    // The original, unmodified parsed function item.
    original: ItemFn,
    arg_idents: Vec<Ident>,
    // A modified version of the original function declaration in which:
    // - A single type parameter with a single trait bound of Ip or IpAddr
    //   (depending on which attribute is used)
    // - The type parameter in question has been removed
    // - In the remaining type parameters, function arguments, and return
    //   values, the type parameter is replaced with Self
    traif_fn_sig: Signature,
    // The list of type identifiers to provide as arguments to the trait method
    // when calling it from the outer function. Equivalent to the full list of
    // type parameters minus the target type and lifetimes.
    trait_fn_type_params: Vec<Ident>,
    // The type parameter name found while calculating traif_fn_sig
    type_ident: Ident,

    // In both of these blocks, we have replaced instances of the target type
    // with Self (e.g., `let ethertype = I::ETHER_TYPE` becomes `let ethertype =
    // Self::ETHER_TYPE`).

    // Statements from the #[ipv4[addr]] block
    ipv4_block: Block,
    // Statements from the #[ipv6[addr]] block
    ipv6_block: Block,
    // Errors encountered during processing; if any exist, they should be
    // printed instead of doing anything else
    errors: Vec<TokenStream2>,
}

// Given a parsed and processed input, serialize it.
fn serialize(input: Input, cfg: &Config) -> TokenStream {
    // First generate the body of the function; we'll generate the declaration
    // later.
    let block = if input.errors.is_empty() {
        let ipv4_block = &input.ipv4_block;
        let ipv6_block = &input.ipv6_block;

        let mut trait_decl = input.traif_fn_sig.clone();
        trait_decl.ident = Ident::new("f", Span::call_site());

        let trait_ident = Ident::new(&cfg.trait_name, Span::call_site());
        let ipv4_type_ident = &cfg.ipv4_type_ident;
        let ipv6_type_ident = &cfg.ipv6_type_ident;

        let type_ident = &input.type_ident;
        let trait_fn_type_params = &input.trait_fn_type_params;
        let arg_idents = &input.arg_idents;

        quote! {
                trait Ext: net_types::ip::#trait_ident {
                    #[allow(patterns_in_fns_without_body)]
                    #trait_decl;
                }

                impl<__SpecializeIpDummyTypeParam: net_types::ip::#trait_ident> Ext for __SpecializeIpDummyTypeParam {
                    #![allow(unused_variables)]
                    default #trait_decl {
                        // This is a backstop against a bug in our logic (if the
                        // function is somehow called on a type other than Ipv4/
                        // Ipv4Addr or Ipv6/Ipv6Addr).
                        unreachable!("IP version-specialized function called on version \
                                      other than IPv4 or IPv6");
                    }
                }

                impl Ext for net_types::ip::#ipv4_type_ident {
                    // Due to the lack of support for attributes on expressions
                    // (see README.md), users often need to do `return x;`
                    // rather than just `x`, which causes Clippy to complain.
                    // Also, some specializations may not use all the arguments
                    // so we allow unused variables.
                    //
                    // TODO(joshlf): Some of these allows would be more precise
                    // if they were added only to specific statements (probably
                    // just to the IPv4- or IPv6-specific statements).
                    #[allow(clippy::needless_return, clippy::let_and_return, unused_variables)]
                    #trait_decl #ipv4_block
                }

                impl Ext for net_types::ip::#ipv6_type_ident {
                    #[allow(clippy::needless_return, clippy::let_and_return, unused_variables)]
                    #trait_decl #ipv6_block
                }

                #type_ident::f::<#(#trait_fn_type_params),*>(#(#arg_idents),*)
        }
    } else {
        let errors = &input.errors;
        quote! {
            #(#errors)*
        }
    };

    // NOTE: A previous version of this code worked by parsing the `block` we
    // just generated, and setting input.original.block equal to that. However,
    // that requires us to be able to parse `block`, which can fail in the case
    // of certain user errors. It's much more user-friendly to do it this way,
    // which allows the compiler's parser to output errors and point out where
    // the error originated.
    let ItemFn { attrs, vis, sig, .. } = input.original.clone();
    // TODO(rheacock): strip `mut` keyword from arguments in the outer function
    quote!(
        #(#attrs)*
        #vis #sig {
            #block
        }
    )
    .into()
}

fn push_error<T: ToTokens, U: Display>(errors: &mut Vec<TokenStream2>, tokens: T, message: U) {
    errors.push(Error::new_spanned(tokens, message).to_compile_error());
}

// Parse the original input and perform all of the processing needed to get it
// ready for serialization. This is the core of the proc macro.
fn parse_input(input: ItemFn, cfg: &Config) -> Input {
    let original = input.clone();
    let mut ipv4_block = *original.block.clone();
    let mut ipv6_block = *original.block.clone();

    // Try to build up as many errors as possible without aborting. This allows
    // us to print as many of them for the user as possible. Note that we're OK
    // with storing nonsensical results for the other fields in Input as long as
    // we keep executing and as long as those nonsensical results don't make us
    // print incorrect errors. The reason is that, if any errors were
    // encountered, we print those errors instead of printing the rest of the
    // fields in Input (other than `original`), so it's fine for them to be
    // wrong. It's more important that we don't bail early so we can pick up
    // more errors.
    let mut errors = Vec::new();

    #[derive(Copy, Clone, Eq, PartialEq)]
    enum IpAttr {
        Ipv4,
        Ipv6,
    }

    // A VisitMut which visits every block, and keeps only the statements in
    // that block which either have no #[ipv4[addr]] or #[ipv6[addr]] attribute
    // or have the target attribute.
    struct AttrFilterVisit<'a> {
        attr: IpAttr,
        errors: &'a mut Vec<TokenStream2>,
        cfg: &'a Config,
    }

    impl<'a> AttrFilterVisit<'a> {
        fn new(
            attr: IpAttr,
            errors: &'a mut Vec<TokenStream2>,
            cfg: &'a Config,
        ) -> AttrFilterVisit<'a> {
            AttrFilterVisit { attr, errors, cfg }
        }
    }

    impl<'a> VisitMut for AttrFilterVisit<'a> {
        fn visit_block_mut(&mut self, i: &mut Block) {
            // This is less efficient than a hypothetical retain_mut, but
            // unfortunately retain only provides immutable references, and we
            // need to mutate in place.
            i.stmts = i
                .stmts
                .drain(..)
                .filter_map(|mut stmt| {
                    match with_stmt_attrs(&mut stmt, |attrs| {
                        parse_rewrite_attrs(attrs, self.errors, self.cfg)
                    }) {
                        Some(attr) if attr == self.attr => Some(stmt),
                        Some(_) => None,
                        None => Some(stmt),
                    }
                })
                .collect();
            visit_mut::visit_block_mut(self, i);
        }

        fn visit_expr_match_mut(&mut self, i: &mut ExprMatch) {
            i.arms = i
                .arms
                .drain(..)
                .filter_map(|mut arm| {
                    match parse_rewrite_attrs(&mut arm.attrs, self.errors, self.cfg) {
                        Some(attr) if attr == self.attr => Some(arm),
                        Some(_) => None,
                        None => Some(arm),
                    }
                })
                .collect();
            visit_mut::visit_expr_match_mut(self, i);
        }
    }

    // Parse a Vec of Attributes, looking for a #[ipv4[addr]] or #[ipv6[addr]]
    // attribute; if it is found, remove that attribute. This is unnecessary for
    // blocks since we toss the block and only extract the block's inner list
    // statements, but it's easier to do this generically than to special-case
    // blocks.
    fn parse_rewrite_attrs(
        attrs: &mut Vec<Attribute>,
        errors: &mut Vec<TokenStream2>,
        cfg: &Config,
    ) -> Option<IpAttr> {
        let mut result = None;

        attrs.retain(|attr| {
            let attr_style_outer = match attr.style {
                AttrStyle::Outer => true,
                _ => false,
            };
            let ip_attr = if attr.path.is_ident(&cfg.ipv4_attr_name) && attr_style_outer {
                IpAttr::Ipv4
            } else if attr.path.is_ident(&cfg.ipv6_attr_name) && attr_style_outer {
                IpAttr::Ipv6
            } else {
                // retain attributes that aren't #[ipv4[addr]] or #[ipv6[addr]],
                // including inner attributes (#![ipv4[addr]] or
                // #![ipv6[addr]]); these will be picked up by a later compiler
                // pass and generate an error when they're processed by our proc
                // macros defined above for this purpose
                return true;
            };

            if !attr.tokens.is_empty() {
                push_error(errors, &attr.tokens, "unexpected attribute argument");
            }

            match (ip_attr, result) {
                (IpAttr::Ipv4, Some(IpAttr::Ipv6)) | (IpAttr::Ipv6, Some(IpAttr::Ipv4)) => {
                    push_error(
                        errors,
                        &attr.tokens,
                        format!(
                            "can't have both #[{}] and #[{}]",
                            cfg.ipv4_attr_name, cfg.ipv6_attr_name
                        ),
                    );
                }
                _ => result = Some(ip_attr),
            }

            // remove #[ipv4[addr]] or #[ipv6[addr]] attr
            false
        });

        result
    }

    AttrFilterVisit::new(IpAttr::Ipv4, &mut errors, cfg).visit_block_mut(&mut ipv4_block);
    AttrFilterVisit::new(IpAttr::Ipv6, &mut errors, cfg).visit_block_mut(&mut ipv6_block);

    // Extract the original declaration, and then modify it into the version we
    // will use inside the trait.
    //
    // TODO(joshlf): As part of Rust issue #35203, it will eventually become a
    // hard error to have patterns in methods without bodies. What this means
    // for us is that things like `mut buffer: B` and `(a, b): (usize, usize)`
    // won't be valid in the trait definiton (though they will still be valid in
    // the impl blocks). We will eventually need to strip these appropriately.
    let mut traif_fn_sig = original.sig.clone();
    let type_ident = rewrite_types(
        &mut traif_fn_sig,
        &mut [&mut ipv4_block.stmts, &mut ipv6_block.stmts],
        &mut errors,
        cfg,
    );

    // Get a list of the names of the type parameters that we need to pass when
    // invoking the trait function.
    //
    // Note, we do not capture the lifetime parameters as we do not pass them on
    // to the generated function. This is because rust does not allow us to specify
    // lifetime arguments explicitly if late bound lifetime parameters are present.
    // If we wanted to pass lifetimes, we would need to add logic to first detect
    // whether any late bound lifetime parameters are present. Instead, we simply
    // do not specify any lifetime for the generated function and let rust infer them.
    let trait_fn_type_params = traif_fn_sig
        .generics
        .params
        .iter()
        .filter_map(|param| match param {
            GenericParam::Type(param) => Some(param.ident.clone()),
            GenericParam::Const(param) => Some(param.ident.clone()),
            GenericParam::Lifetime(_param) => None,
        })
        .collect();

    // Get a list of the argument names.
    let arg_idents = original
        .sig
        .inputs
        .iter()
        .map(|arg| match arg {
            FnArg::Receiver(_) => Ident::new("self", Span::call_site()),
            FnArg::Typed(PatType { pat, .. }) => match pat.as_ref() {
                Pat::Ident(pat) => {
                    if pat.subpat.is_some() {
                        push_error(&mut errors, arg, "unexpected attribute argument");
                        parse_quote!(dummy)
                    } else {
                        pat.ident.clone()
                    }
                }
                _ => {
                    push_error(&mut errors, arg, "patterns in function arguments not supported");
                    parse_quote!(dummy)
                }
            },
        })
        .collect();

    Input {
        original,
        type_ident,
        arg_idents,
        trait_fn_type_params,
        traif_fn_sig,
        ipv4_block,
        ipv6_block,
        errors,
    }
}

// Rewrite the types in the function declaration according the following rules:
// First, find exactly one type parameter with a single trait bound with the
// trait `cfg.rait_ident`. If there are zero or more than one such types, fail.
// If a type with the desired trait bound has that trait bound multiple times or
// has other trait bounds, fail. Once the type has been found, replace every
// instance of it in the other trait bounds, argument types, and return types
// with `Self`.
fn rewrite_types(
    sig: &mut Signature,
    stmts: &mut [&mut Vec<Stmt>],
    errors: &mut Vec<TokenStream2>,
    cfg: &Config,
) -> Ident {
    // TODO(joshlf): Support where clauses. Where clauses are difficult because
    // they are somewhat more unstructured than the normal list of type
    // parameters and bounds, and so it can be very annoying to compute whether
    // a type named in the where clause is the same as a type named in the list
    // of type parameters. This, in turn, is necessary because additional trait
    // bounds can appear in the where clause, and so if we support where
    // clauses, we need to look both in the list of type parameters and in the
    // where clause to find trait bounds.
    if sig.generics.where_clause.is_some() {
        panic!("#[{}]: where clause are currently unsupported", cfg.attr_name);
    }

    // The ident of the type and the index into sig.generics.params. We will
    // use the index to remove the type parameter from the list of parameters.
    let mut type_ident_and_index = None::<(Ident, usize)>;

    for (idx, type_param) in sig.generics.params.iter().enumerate() {
        if let GenericParam::Type(type_param) = type_param {
            // number of bounds total for this type parameter
            let total_trait_bounds = type_param.bounds.len();
            // number of bounds which match the target trait bound
            let target_trait_bounds = type_param
                .bounds
                .iter()
                .filter(|bound| {
                    if let TypeParamBound::Trait(bound) = bound {
                        bound.path.is_ident(cfg.trait_name)
                    } else {
                        false
                    }
                })
                .count();

            if target_trait_bounds > 0 {
                if total_trait_bounds != target_trait_bounds {
                    push_error(
                        errors,
                        type_param,
                        format!(
                            "type with {} trait bound cannot also have other bounds",
                            cfg.trait_name
                        ),
                    );
                }
                if target_trait_bounds > 1 {
                    push_error(
                        errors,
                        type_param,
                        format!("duplicate {} trait bounds", cfg.trait_name),
                    );
                }
                if type_ident_and_index.is_some() {
                    push_error(
                        errors,
                        type_param,
                        format!("duplicate types with {} trait bound", cfg.trait_name),
                    );
                }
                type_ident_and_index = Some((type_param.ident.clone(), idx));
            }
        }
    }

    if let Some((type_ident, idx)) = type_ident_and_index {
        // Remove the type with the `cfg.trait_name` bound.
        sig.generics.params = remove_from_punctuated(sig.generics.params.clone(), idx);

        let mut rename = RenameVisit::new(type_ident.clone());
        // For `cfg.type_ident` `I`, rewrite bounds like `A: Foo<I>` to `A: Foo<Self>`.
        rename.visit_generics_mut(&mut sig.generics);
        // For `cfg.type_ident` `I`, rewrite arguments like `a: Foo<I>` to `a: Foo<Self>`
        for arg in &mut sig.inputs {
            rename.visit_fn_arg_mut(arg);
        }
        // For `cfg.type_ident` `I`, rewrite return types like `Foo<I>` to `Foo<Self>`
        rename.visit_return_type_mut(&mut sig.output);
        // Make sure no uses of "impl trait" are present in the return type.
        ReturnImplTraitVisit(errors).visit_return_type_mut(&mut sig.output);

        // Rewrite all types inside of the statements
        for stmts in stmts {
            for stmt in stmts.iter_mut() {
                rename.visit_stmt_mut(stmt);
            }
        }

        type_ident
    } else {
        push_error(
            errors,
            &sig.generics.params,
            format!("missing type with {} trait bound", cfg.trait_name),
        );
        parse_quote!(Dummy)
    }
}

// A VisitMut that renames named types to Self. The match criteria is that the
// first path element must be equal to our target ident. In other words, given a
// type name I, we want to replace I::Addr with Self::Addr, but we don't want to
// touch J::I, since the I in that context isn't the I we care about.
//
// We also use this to descend into the body. This is because the body can also
// contain references to the type parameters.
struct RenameVisit(Ident);

impl RenameVisit {
    fn new(ident: Ident) -> RenameVisit {
        RenameVisit(ident)
    }
}

impl VisitMut for RenameVisit {
    fn visit_path_mut(&mut self, i: &mut Path) {
        let segment = i.segments.iter_mut().next().expect("unexpected path with no elements");
        if i.leading_colon.is_none() && segment.ident == self.0 && segment.arguments.is_empty() {
            *segment = parse_quote!(Self);
        }

        // descend into the individual path segments; we need to do this since
        // path segments can contain arguments (e.g., A::<B>::C), and those
        // arguments can also contain our target
        visit_mut::visit_path_mut(self, i);
    }

    // Don't descend into function or type definitions. It's invalid to use
    // types from the outer scope inside of a function or type definition, so
    // the only valid uses of type parameters would be from type parameters
    // whose names shadow the name of the one we actually care about, in which
    // case it would be incorrect to replace it with Self. Note that this
    // doesn't apply to type aliases - `type Foo = Bar<I>` is valid, and we
    // should replace `I` with `Self` in that case.
    fn visit_item_fn_mut(&mut self, _i: &mut ItemFn) {}
    fn visit_item_struct_mut(&mut self, _i: &mut syn::ItemStruct) {}
    fn visit_item_enum_mut(&mut self, _i: &mut syn::ItemEnum) {}
    fn visit_item_union_mut(&mut self, _i: &mut syn::ItemUnion) {}
}

// A VisitMut that searches for instances of "impl trait". These are unsupported
// in function return values because they can't be used inside of traits, and we
// convert the decorated function to a trait function.
struct ReturnImplTraitVisit<'a>(&'a mut Vec<TokenStream2>);

impl<'a> VisitMut for ReturnImplTraitVisit<'a> {
    fn visit_type_impl_trait_mut(&mut self, i: &mut TypeImplTrait) {
        push_error(self.0, &i, "impl trait return values are unsupported");
        visit_mut::visit_type_impl_trait_mut(self, i);
    }
}

// Consume a Punctuated, and return a new Punctuated which is equal to the old
// one minus the element at idx.
fn remove_from_punctuated<T, P: Default>(p: Punctuated<T, P>, idx: usize) -> Punctuated<T, P> {
    let mut new = Punctuated::new();
    for item in
        p.into_iter().enumerate().filter_map(|(i, val)| if i != idx { Some(val) } else { None })
    {
        new.push(item);
    }
    new
}

// Extract the Attributes from a statement, and call f on them.
fn with_stmt_attrs<O, F: FnOnce(&mut Vec<Attribute>) -> O>(stmt: &mut Stmt, f: F) -> O {
    let mut dummy = Vec::new();
    f(match stmt {
        Stmt::Local(x) => &mut x.attrs,
        Stmt::Item(Item::ExternCrate(x)) => &mut x.attrs,
        Stmt::Item(Item::Use(x)) => &mut x.attrs,
        Stmt::Item(Item::Static(x)) => &mut x.attrs,
        Stmt::Item(Item::Const(x)) => &mut x.attrs,
        Stmt::Item(Item::Fn(x)) => &mut x.attrs,
        Stmt::Item(Item::Mod(x)) => &mut x.attrs,
        Stmt::Item(Item::ForeignMod(x)) => &mut x.attrs,
        Stmt::Item(Item::Type(x)) => &mut x.attrs,
        Stmt::Item(Item::Struct(x)) => &mut x.attrs,
        Stmt::Item(Item::Enum(x)) => &mut x.attrs,
        Stmt::Item(Item::Union(x)) => &mut x.attrs,
        Stmt::Item(Item::Trait(x)) => &mut x.attrs,
        Stmt::Item(Item::TraitAlias(x)) => &mut x.attrs,
        Stmt::Item(Item::Impl(x)) => &mut x.attrs,
        Stmt::Item(Item::Macro(x)) => &mut x.attrs,
        Stmt::Item(Item::Macro2(x)) => &mut x.attrs,
        Stmt::Item(Item::Verbatim(_)) => &mut dummy,
        Stmt::Item(Item::__Nonexhaustive) => unreachable!(),
        Stmt::Expr(expr) | Stmt::Semi(expr, _) => match expr {
            Expr::Box(x) => &mut x.attrs,
            Expr::Array(x) => &mut x.attrs,
            Expr::Await(x) => &mut x.attrs,
            Expr::Call(x) => &mut x.attrs,
            Expr::MethodCall(x) => &mut x.attrs,
            Expr::Tuple(x) => &mut x.attrs,
            Expr::Binary(x) => &mut x.attrs,
            Expr::Unary(x) => &mut x.attrs,
            Expr::Lit(x) => &mut x.attrs,
            Expr::Cast(x) => &mut x.attrs,
            Expr::Type(x) => &mut x.attrs,
            Expr::Let(x) => &mut x.attrs,
            Expr::If(x) => &mut x.attrs,
            Expr::While(x) => &mut x.attrs,
            Expr::ForLoop(x) => &mut x.attrs,
            Expr::Loop(x) => &mut x.attrs,
            Expr::Match(x) => &mut x.attrs,
            Expr::Closure(x) => &mut x.attrs,
            Expr::Unsafe(x) => &mut x.attrs,
            Expr::Block(x) => &mut x.attrs,
            Expr::Assign(x) => &mut x.attrs,
            Expr::AssignOp(x) => &mut x.attrs,
            Expr::Field(x) => &mut x.attrs,
            Expr::Index(x) => &mut x.attrs,
            Expr::Range(x) => &mut x.attrs,
            Expr::Path(x) => &mut x.attrs,
            Expr::Reference(x) => &mut x.attrs,
            Expr::Break(x) => &mut x.attrs,
            Expr::Continue(x) => &mut x.attrs,
            Expr::Return(x) => &mut x.attrs,
            Expr::Macro(x) => &mut x.attrs,
            Expr::Struct(x) => &mut x.attrs,
            Expr::Repeat(x) => &mut x.attrs,
            Expr::Paren(x) => &mut x.attrs,
            Expr::Group(x) => &mut x.attrs,
            Expr::Try(x) => &mut x.attrs,
            Expr::Async(x) => &mut x.attrs,
            Expr::TryBlock(x) => &mut x.attrs,
            Expr::Yield(x) => &mut x.attrs,
            Expr::Verbatim(_) => &mut dummy,
            Expr::__Nonexhaustive => unreachable!(),
        },
    })
}
