// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    proc_macro::TokenStream,
    quote::{quote, ToTokens},
    std::collections::HashSet,
    syn::{
        bracketed,
        export::TokenStream2,
        parenthesized,
        parse::{Parse, ParseBuffer},
        parse_macro_input::parse,
        punctuated::Punctuated,
        Attribute, Ident, Token,
    },
};

/// Represents a parsable struct which accepts the following input:
/// ```
/// A => B,
/// B => [C, A],
/// ...
/// ```
struct TransitionArgs {
    from_name: Ident,
    to_names: Vec<Ident>,
}

impl Parse for TransitionArgs {
    fn parse(input: &ParseBuffer) -> syn::parse::Result<Self> {
        let from_name: Ident = input.parse()?;
        input.parse::<Token![=>]>()?;
        let to_names = match input.peek(Ident) {
            true => vec![input.parse::<Ident>()?],
            false => {
                let content;
                bracketed!(content in input);
                let group = Punctuated::<Ident, Token![,]>::parse_terminated(&content)?;
                group.into_iter().collect()
            }
        };
        Ok(Self { from_name, to_names })
    }
}

/// Represents a parsable struct which accepts the following input:
/// ```
/// () => A,
/// ```
struct InitStateArgs(Ident);

impl Parse for InitStateArgs {
    fn parse(input: &ParseBuffer) -> syn::parse::Result<Self> {
        let _content;
        parenthesized!(_content in input);
        input.parse::<Token![=>]>()?;
        let state_name = input.parse()?;
        input.parse::<Token![,]>()?;
        Ok(Self(state_name))
    }
}

impl ToTokens for InitStateArgs {
    fn to_tokens(&self, tokens: &mut TokenStream2) {
        let name = &self.0;
        *tokens = quote! {
            #tokens
            impl InitialState for #name {}
        };
    }
}

/// Represents a parsable struct which accepts the following input:
/// ```
/// #derive(Debug)
/// enum Client,
/// ```
struct EnumArgs {
    attrs: Option<Vec<Attribute>>,
    public: bool,
    name: Ident,
}

impl Parse for EnumArgs {
    fn parse(input: &ParseBuffer) -> syn::parse::Result<Self> {
        let attrs = input.call(Attribute::parse_outer).ok();
        let public = input.peek(Token![pub]);
        if public {
            input.parse::<Token![pub]>()?;
        }
        input.parse::<Token![enum]>()?;
        let name = input.parse()?;
        input.parse::<Token![,]>()?;
        Ok(Self { attrs, name, public })
    }
}

impl ToTokens for EnumArgs {
    fn to_tokens(&self, tokens: &mut TokenStream2) {
        let default = vec![];
        let attrs = self.attrs.as_ref().unwrap_or(&default);
        let name = &self.name;
        let public = if self.public { quote!(pub) } else { quote!() };
        *tokens = quote! {
            #tokens
            #(#attrs)*
            #public enum #name
        };
    }
}

/// Represents a parsable struct which accepts the following input:
/// ```
/// #derive(Debug)
/// enum Client,
/// () => A,
/// A => B,
/// B => [C, A],
/// C => A
/// ```
struct StateMachineArgs {
    enum_data: Option<EnumArgs>,
    init_state: InitStateArgs,
    transitions: Punctuated<TransitionArgs, Token![,]>,
}

impl Parse for StateMachineArgs {
    fn parse(input: &ParseBuffer) -> syn::parse::Result<Self> {
        Ok(Self {
            enum_data: parse_enum_if_declared(input)?,
            init_state: input.parse()?,
            transitions: Punctuated::parse_terminated(input)?,
        })
    }
}

#[derive(Eq, PartialEq, Hash)]
struct StateTransition(Ident, Ident);

// TODO: Transitions can be specified multiple times
pub fn process(input: TokenStream) -> TokenStream {
    // Parse macro input.
    let args: StateMachineArgs = parse(input).expect("error processing macro");

    // Collect unique states and state transitions.
    let mut transitions = HashSet::<StateTransition>::new();
    let mut state_set = HashSet::<Ident>::new();
    state_set.insert(args.init_state.0.clone());
    for transition in &args.transitions {
        state_set.insert(transition.from_name.clone());
        state_set.extend(transition.to_names.iter().map(|x| x.clone()));
        transitions.extend(
            transition
                .to_names
                .iter()
                .map(|x| StateTransition(transition.from_name.clone(), x.clone())),
        );
    }

    // Verify no unreachable state was defined:
    for state in &state_set {
        if state != &args.init_state.0 && !transitions.iter().any(|t| &t.1 == state) {
            panic!("Unreachable state defined: {}", state);
        }
    }

    // Make optional enum type definition:
    let mut enum_code = quote!();
    if let Some(enum_data) = args.enum_data {
        // Define state machine's enum variants:
        let state_variants = state_set
            .iter()
            .map(|x| {
                let x = x.clone();
                quote! {
                    #x(State<#x>),
                }
            })
            .collect::<Vec<_>>();

        // Implement From<State<S>> for the newly defined state machine:
        let enum_name = enum_data.name.clone();
        let states_from_impl = state_set
            .iter()
            .map(|x| {
                let x = x.clone();
                quote! {
                    impl From<State<#x>> for #enum_name {
                        fn from(state: State<#x>) -> #enum_name {
                            #enum_name::#x(state)
                        }
                    }
                }
            })
            .collect::<Vec<_>>();
        enum_code = quote! {
            #enum_data {
                #(#state_variants)*
            }
            #(#states_from_impl)*
        }
    };

    // Implement each necessary state transition:
    let transitions = transitions
        .into_iter()
        .map(|transition| {
            let from = transition.0;
            let to = transition.1;
            quote! {
                impl StateTransition<#to> for State<#from> {
                    fn __internal_transition_to(new_state: #to) -> State<#to> {
                        State::__internal_new(new_state)
                    }
                }
            }
        })
        .collect::<Vec<_>>();

    // Make final enum, initial state and state transitions definitions.
    let initial = args.init_state;
    TokenStream::from(quote! {
        #enum_code
        #initial
        #(#transitions)*
    })
}

fn parse_enum_if_declared(input: &ParseBuffer) -> syn::parse::Result<Option<EnumArgs>> {
    if input.peek(Token![#]) || input.peek(Token![enum]) || input.peek(Token![pub]) {
        Ok(Some(input.parse()?))
    } else {
        Ok(None)
    }
}
