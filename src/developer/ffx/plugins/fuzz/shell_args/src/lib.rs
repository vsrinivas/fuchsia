// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains two proc_macros designed to be used by the ffx_fuzz_args library. As such,
// they are closely tailored to the type definitions for the ffx plugin and are not meant for
// general purpose usage.

use {
    proc_macro::TokenStream,
    proc_macro2::{Ident, Span},
    quote::{quote, quote_spanned},
    syn::parse::Parser,
    syn::punctuated::Punctuated,
    syn::{parse_macro_input, Fields, ItemStruct, Token, TypePath},
};

/// Generates a standalone subcommand from a shell subcommand., e.g. `FooSubcommand` from
/// `FooShellSubcommand`.
///
/// The generated struct has the same attributes and fields as the shell version, plus the `quiet`
/// switch that may be provided at startup, and the `url` and `output` fields that the shell
/// provides when attaching a fuzzer. Additionally, a From<T> implementation is provided to the
/// shell version, where `T` is the standalone version. Together, the extra fields and trait make it
/// straightforward to convert a standalone subcommand into a sequence of equivalent shell
/// subcommands.
///
/// For example, given:
///
/// ```rust
///    #[derive_subcommand]
///    #[argh(subcommand, name = "foo" ... )]
///    struct FooShellSubcommand {
///        #[argh(positional)]
///        name: String,
///    }
/// ```
///
/// Then the following struct is generated:
///
/// ```rust
///    #[argh(subcommand, name = "foo" ... )]
///    struct FooSubcommand {
///        #[argh(positional)]
///        pub url: String,
///
///        #[argh(option, short = 'o')]
///        pub output: Option<String>,
///
///        #[argh(switch, short = 'q')]
///        pub quiet: bool,
///
///        #[argh(positional)]
///        pub name: String,
///    }
/// ```
///
/// And, given `output`, `url`, and `name`, the following is valid:
///
/// ```rust
///    let cmd = FooSubcommand { name, output, url, .. };
///    let shell_cmds = vec![
///        AttachShellSubcommand::from(&cmd).unwrap(),
///        FooShellSubcommand::from(cmd),
///    ];
/// ```
#[proc_macro_attribute]
pub fn derive_subcommand(_attr: TokenStream, item: TokenStream) -> TokenStream {
    let item = parse_macro_input!(item as ItemStruct);
    let ItemStruct { attrs, ident, fields, .. } = item;
    let span = ident.span();
    let new_ident = format!("{}", ident);
    let new_ident = Ident::new(&new_ident.replace("Shell", ""), Span::call_site());
    let fields_inner = match fields {
        Fields::Named(ref fields) => fields.named.clone(),
        _ => unreachable!(),
    };
    let mut assignments = Vec::new();
    for field in fields.iter() {
        let field_ident = field.ident.as_ref().unwrap();
        let output = quote! {
            #field_ident: args.#field_ident,
        };
        assignments.push(output);
    }
    let args = match assignments.is_empty() {
        true => quote! { _args },
        false => quote! { args },
    };
    let output = quote_spanned! {span=>
        #(#attrs)*
        pub struct #ident
        #fields

        #(#attrs)*
        pub struct #new_ident {
            /// package URL for the fuzzer
            #[argh(positional)]
            pub url: String,

            /// where to send fuzzer logs and artifacts; default is stdout and the current directory
            #[argh(option, short = 'o')]
            pub output: Option<String>,

            /// if present, suppress non-error output from the ffx tool itself
            #[argh(switch, short = 'q')]
            pub quiet: bool,

            /// disables forwarding standard output from the fuzzer
            #[argh(switch)]
            pub no_stdout: bool,

            /// disables forwarding standard error from the fuzzer
            #[argh(switch)]
            pub no_stderr: bool,

            /// disables forwarding system logs from the fuzzer
            #[argh(switch)]
            pub no_syslog: bool,

            #fields_inner
        }

        impl From<#new_ident> for #ident {
            fn from(#args: #new_ident) -> Self {
                Self {
                  #(#assignments)*
                }
            }
        }
    };
    output.into()
}

/// Generates an `add_if_valid` associated method for a shell subcommand.
///
/// This method takes a mutable list of commands and the current shell state, and adds the command
/// for this shell subcommand to the list if the given state matches one of those provided as
/// arguments to the macro.
///
/// For example, given:
///
/// ```rust
///     #[valid_when(FuzzerState::Idle, FuzzerState::Running)]
///    #[argh(subcommand, name = "foo" ... )]
///    struct FooShellSubcommand { ... }
///
///    #[valid_when(FuzzerState::Detached)]
///    #[argh(subcommand, name = "bar" ... )]
///    struct BarShellSubcommand { ... }
/// ```
///
/// Then running the following would produce `vec!["foo".to_string()]`:
///
/// ```rust
///    let mut commands = Vec::new();
///    FooShellSubcommand::add_if_valid(&mut commands, FuzzerState::Idle);
///    BarShellSubcommand::add_if_valid(&mut commands, FuzzerState::Idle);
/// ```
#[proc_macro_attribute]
pub fn valid_when(attr: TokenStream, item: TokenStream) -> TokenStream {
    let item = parse_macro_input!(item as ItemStruct);
    let ItemStruct { attrs, ident, fields, .. } = item;
    let span = ident.span();
    let tokens = attr.clone();
    let parser = Punctuated::<TypePath, Token![,]>::parse_separated_nonempty;
    let states = parser.parse(tokens).expect("failed to parse");
    let mut conditional_exprs = Vec::new();
    for state in &states {
        let output = quote! {
            if state == #state {
                commands.push(Self::COMMAND.name.to_string());
            }
        };
        conditional_exprs.push(output);
    }
    let output = quote_spanned! {span=>
        #(#attrs)*
        pub struct #ident
        #fields

        impl #ident {
            pub fn add_if_valid(commands: &mut Vec<String>, state: FuzzerState) {
                #(#conditional_exprs)*
            }
        }
    };
    output.into()
}
