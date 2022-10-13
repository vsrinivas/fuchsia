// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[macro_use]
extern crate quote;
#[macro_use]
extern crate syn;

use core::fmt::Display;

use proc_macro::TokenStream;
use proc_macro2::{Span, TokenStream as TokenStream2};
use quote::ToTokens;
use syn::{
    parse::{ParseStream, Parser},
    punctuated::Punctuated,
    spanned::Spanned,
    visit_mut::{self, VisitMut},
    Attribute, Error, Expr, ExprPath, FnArg, GenericParam, Ident, ItemFn, Pat, PatType, Path,
    TypeImplTrait, TypeParamBound, TypePath,
};

#[proc_macro_attribute]
pub fn ip_test(attr: TokenStream, input: TokenStream) -> TokenStream {
    ip_test_inner(attr, input, "ip_test", "Ip", "Ipv4", "Ipv6")
}

#[proc_macro_attribute]
pub fn ip_addr_test(attr: TokenStream, input: TokenStream) -> TokenStream {
    ip_test_inner(attr, input, "ip_addr_test", "IpAddress", "Ipv4Addr", "Ipv6Addr")
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
    let (type_ident, trait_path) = {
        if let Some(variadic) = &sig.variadic {
            return Error::new(
                variadic.dots.spans[0],
                format!("{} entry may not be variadic", attr_name),
            )
            .to_compile_error()
            .into();
        }

        let trait_info = sig.generics.params.iter().find_map(|gen| match gen {
            GenericParam::Type(tp) => tp
                .bounds
                .iter()
                .find_map(|b| match b {
                    TypeParamBound::Trait(t) => t.path.is_ident(trait_name).then(|| t.path.clone()),
                    _ => None,
                })
                .map(|trait_path| (&tp.ident, trait_path)),
            _ => None,
        });

        match trait_info {
            Some((type_ident, trait_path)) => (type_ident, trait_path),
            None => {
                return Error::new(
                    sig.generics.span(),
                    format!("some generic parameter must have a {} type bound", trait_name),
                )
                .to_compile_error()
                .into()
            }
        }
    };

    // we need to check the attributes given to the function, test attributes
    // like `should_panic` need to go to the concrete test definitions instead.
    let (mut test_attrs, attrs) = attrs.into_iter().partition::<Vec<_>, _>(|attr| {
        attr.path.is_ident("should_panic") || attr.path.is_ident("test_case")
    });
    if !test_attrs.iter().any(|a| a.path.is_ident("test_case")) {
        test_attrs.push(Attribute {
            path: syn::parse_quote!(test),
            bracket_token: Default::default(),
            pound_token: Default::default(),
            style: syn::AttrStyle::Outer,
            tokens: Default::default(),
        });
    }
    // borrow here because `test_attrs` is used twice in `quote_spanned!` below.
    let test_attrs = &test_attrs;

    let span = sig.ident.span();
    let output = &sig.output;
    let ident = &sig.ident;

    let arg_idents;
    {
        let mut errors = Vec::new();
        arg_idents = make_arg_idents(sig.inputs.iter(), &mut errors);
        if !errors.is_empty() {
            return Error::new(sig.inputs.span(), quote!(#(#errors)*)).to_compile_error().into();
        }
    }

    let v4_test = Ident::new(&format!("{}_v4", ident), Span::call_site());
    let v6_test = Ident::new(&format!("{}_v6", ident), Span::call_site());

    let ipv4_type_ident = Ident::new(ipv4_type_name, Span::call_site());
    let ipv6_type_ident = Ident::new(ipv6_type_name, Span::call_site());

    struct IpSpecializations {
        test_attrs: Vec<Attribute>,
        inputs: Vec<FnArg>,
        fn_generics: Vec<GenericParam>,
        generic_params: Vec<Ident>,
    }

    let specialize = |ip_type_ident: Ident| {
        let test_attrs = test_attrs
            .iter()
            .cloned()
            .map(|mut attr| {
                let parser = parse_with_ignoring_trailing(
                    Punctuated::<Expr, Token![,]>::parse_separated_nonempty,
                );
                if let Ok(mut punctuated) = attr.parse_args_with(parser) {
                    let mut visit = TraitToConcreteVisit {
                        concrete: ip_type_ident.clone().into(),
                        trait_path: trait_path.clone(),
                        type_ident: type_ident.clone(),
                    };

                    for expr in punctuated.iter_mut() {
                        visit.visit_expr_mut(expr);
                    }
                    attr.tokens = quote!((#punctuated));
                }
                attr
            })
            .collect();

        let mut input_visitor = TraitToConcreteVisit {
            concrete: ip_type_ident.clone().into(),
            trait_path: trait_path.clone(),
            type_ident: type_ident.clone(),
        };
        let inputs = sig
            .inputs
            .iter()
            .cloned()
            .map(|mut a| {
                input_visitor.visit_fn_arg_mut(&mut a);
                a
            })
            .collect();

        let fn_generics = sig
            .generics
            .params
            .iter()
            .filter(|gen| match gen {
                GenericParam::Type(tp) => &tp.ident != type_ident,
                _ => true,
            })
            .cloned()
            .map(|mut gen| {
                input_visitor.visit_generic_param_mut(&mut gen);
                gen
            })
            .collect::<Vec<_>>();

        let generic_params = sig
            .generics
            .params
            .iter()
            .filter_map(|a| match a {
                GenericParam::Type(tp) => {
                    Some(if &tp.ident == type_ident { &ip_type_ident } else { &tp.ident })
                }
                GenericParam::Lifetime(_) => None,
                GenericParam::Const(c) => Some(&c.ident),
            })
            .cloned()
            .collect();

        IpSpecializations { test_attrs, inputs, generic_params, fn_generics }
    };

    let IpSpecializations {
        test_attrs: ipv4_test_attrs,
        inputs: ipv4_inputs,
        fn_generics: ipv4_fn_generics,
        generic_params: ipv4_generic_params,
    } = specialize(ipv4_type_ident);

    let IpSpecializations {
        test_attrs: ipv6_test_attrs,
        inputs: ipv6_inputs,
        fn_generics: ipv6_fn_generics,
        generic_params: ipv6_generic_params,
    } = specialize(ipv6_type_ident);

    let output = quote_spanned! { span =>
        #(#attrs)*
        // Note: `ItemFn::block` includes the function body braces. Do not add
        // additional braces (will break source code coverage analysis).
        // TODO(fxbug.dev/77212): Try to improve the Rust compiler to ease
        // this restriction.
        #vis #sig #block

        #(#ipv4_test_attrs)*
        fn #v4_test<#(#ipv4_fn_generics),*> (#(#ipv4_inputs),*) #output {
           #ident::<#(#ipv4_generic_params),*>(#(#arg_idents),*)
        }

        #(#ipv6_test_attrs)*
        fn #v6_test<#(#ipv6_fn_generics),*> (#(#ipv6_inputs),*) #output {
           #ident::<#(#ipv6_generic_params),*>(#(#arg_idents),*)
        }
    };
    output.into()
}

fn push_error<T: ToTokens, U: Display>(errors: &mut Vec<TokenStream2>, tokens: T, message: U) {
    errors.push(Error::new_spanned(tokens, message).to_compile_error());
}

fn make_arg_idents<'a>(
    input: impl Iterator<Item = &'a FnArg>,
    errors: &mut Vec<TokenStream2>,
) -> Vec<Ident> {
    input
        .map(|arg| match arg {
            FnArg::Receiver(_) => Ident::new("self", Span::call_site()),
            FnArg::Typed(PatType { pat, .. }) => match pat.as_ref() {
                Pat::Ident(pat) => {
                    if pat.subpat.is_some() {
                        push_error(errors, arg, "unexpected attribute argument");
                        parse_quote!(pushed_error)
                    } else {
                        pat.ident.clone()
                    }
                }
                _ => {
                    push_error(errors, arg, "patterns in function arguments not supported");
                    parse_quote!(pushed_error)
                }
            },
        })
        .collect()
}

// A VisitMut that renames named types. The match criteria is that the first
// path element must be equal to our target ident. In other words, given a
// `from` type name I and Self as `to`, we want to replace I::Addr with
// Self::Addr, but we don't want to touch J::I, since the I in that context
// isn't the I we care about.
//
// We also use this to descend into the body. This is because the body can also
// contain references to the type parameters.
struct RenameVisit {
    from: Ident,
    to: Ident,
}

impl VisitMut for RenameVisit {
    fn visit_path_mut(&mut self, i: &mut Path) {
        let Self { from, to } = self;
        if i.is_ident(from) {
            *i = to.clone().into();
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

/// A VisitMut that replaces accesses of an associated type or constant with
/// type a different type qualified with a trait name. Given a `type_ident` of
/// `I`, `concrete` of `Ipv4` and `trait_path` of `Ip`, `I::Addr` would be
/// replaced with `<Ipv4 as Ip>::Addr`.
struct TraitToConcreteVisit {
    type_ident: Ident,
    concrete: Path,
    trait_path: Path,
}

impl TraitToConcreteVisit {
    fn update_type_path(&self, qself: &mut Option<syn::QSelf>, path: &mut Path) {
        let Self { type_ident, concrete, trait_path } = self;
        if qself == &None {
            let mut segments = path.segments.iter();
            if segments.next().map_or(false, |p| &p.ident == type_ident) {
                let remaining_path = segments.cloned().collect::<Vec<_>>();
                let TypePath { path: new_path, qself: new_qself } = if remaining_path.is_empty() {
                    parse_quote!(#concrete)
                } else {
                    parse_quote!(<#concrete as #trait_path>::#(#remaining_path)::*)
                };
                *path = new_path;
                *qself = new_qself;
            }
        }
    }
}

impl VisitMut for TraitToConcreteVisit {
    fn visit_expr_path_mut(&mut self, i: &mut ExprPath) {
        let ExprPath { attrs: _, qself, path } = i;
        self.update_type_path(qself, path);

        visit_mut::visit_expr_path_mut(self, i);
    }

    fn visit_type_path_mut(&mut self, i: &mut TypePath) {
        let TypePath { qself, path } = i;
        self.update_type_path(qself, path);

        visit_mut::visit_type_path_mut(self, i)
    }
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

/// Constructs a parser that parses with the provided function, consuming the
/// rest of the input on success.
///
/// This is useful for "successfully" parsing a stream of tokens with a known
/// prefix. Consuming the rest of the input makes this useful for adapting `P`
/// for [`syn::parse`], which returns an error if any tokens are left
/// unconsumed.
fn parse_with_ignoring_trailing<P>(
    parser: for<'a> fn(ParseStream<'a>) -> syn::Result<P>,
) -> impl Parser<Output = P> {
    fn consume_input<'a>(input: ParseStream<'a>) {
        while !input.is_empty() {
            input
                .step(|cursor| {
                    let mut rest = *cursor;
                    while let Some((_, next)) = rest.token_tree() {
                        rest = next
                    }
                    Ok(((), rest))
                })
                .expect("step fn can't fail");
        }
    }

    move |input: ParseStream<'_>| {
        parser(input).map(|p| {
            consume_input(input);
            p
        })
    }
}
