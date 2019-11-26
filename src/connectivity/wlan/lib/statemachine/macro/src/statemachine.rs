// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fmt::{Error, Formatter};
use {
    proc_macro::TokenStream,
    quote::{format_ident, quote, ToTokens},
    std::collections::{HashMap, HashSet},
    syn::{
        bracketed,
        export::TokenStream2,
        parenthesized,
        parse::{Parse, ParseBuffer},
        parse_macro_input::parse,
        punctuated::Punctuated,
        token, AngleBracketedGenericArguments, Attribute, GenericArgument, Generics, Ident, Token,
    },
};

/// Represents a state identifier that optionally provides generic arguments, e.g. 'A' or 'A<T>'
#[derive(PartialEq, Eq, Clone, Hash)]
struct StateArgs {
    name: Ident,
    generic_args: Option<AngleBracketedGenericArguments>,
}

impl Parse for StateArgs {
    fn parse(input: &ParseBuffer<'_>) -> syn::parse::Result<Self> {
        let name: Ident = input.parse()?;
        let generic_args = match input.peek(token::Lt) {
            true => Some(input.parse::<AngleBracketedGenericArguments>()?),
            false => None,
        };
        Ok(Self { name, generic_args })
    }
}

impl ToTokens for StateArgs {
    fn to_tokens(&self, tokens: &mut TokenStream2) {
        let name = &self.name;
        let generics = tokenize_opt(&self.generic_args);
        *tokens = quote! {
            #tokens
            #name #generics
        };
    }
}

impl std::fmt::Display for StateArgs {
    fn fmt(&self, f: &mut Formatter<'_>) -> Result<(), Error> {
        write!(f, "{}{}", self.name, tokenize_opt(&self.generic_args))
    }
}

/// Represents a parsable struct which accepts the following input:
/// ```
/// A => B,
/// B => [C, A],
/// C => D<T>,
/// D<T> => [F<T>, G<T2>]
/// ...
/// ```
struct TransitionArgs {
    from_name: StateArgs,
    to_names: Vec<StateArgs>,
}

impl Parse for TransitionArgs {
    fn parse(input: &ParseBuffer<'_>) -> syn::parse::Result<Self> {
        let from_name: StateArgs = input.parse()?;
        input.parse::<Token![=>]>()?;
        let to_names = match input.peek(Ident) {
            true => vec![input.parse::<StateArgs>()?],
            false => {
                let content;
                bracketed!(content in input);
                let group = Punctuated::<StateArgs, Token![,]>::parse_terminated(&content)?;
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
struct InitStateArgs(StateArgs);

impl Parse for InitStateArgs {
    fn parse(input: &ParseBuffer<'_>) -> syn::parse::Result<Self> {
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
        let generics = tokenize_opt(&self.0.generic_args);
        *tokens = quote! {
            #tokens
            impl #generics InitialState for #name {}
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
    generic_args: Option<Generics>,
}

impl Parse for EnumArgs {
    fn parse(input: &ParseBuffer<'_>) -> syn::parse::Result<Self> {
        let attrs = input.call(Attribute::parse_outer).ok();
        let public = input.peek(Token![pub]);
        if public {
            input.parse::<Token![pub]>()?;
        }
        input.parse::<Token![enum]>()?;
        let name = input.parse()?;
        let generic_args = match input.peek(token::Lt) {
            true => Some(input.parse::<Generics>()?),
            false => None,
        };
        input.parse::<Token![,]>()?;
        Ok(Self { attrs, name, public, generic_args })
    }
}

impl ToTokens for EnumArgs {
    fn to_tokens(&self, tokens: &mut TokenStream2) {
        let default = vec![];
        let attrs = self.attrs.as_ref().unwrap_or(&default);
        let name = &self.name;
        let public = if self.public { quote!(pub) } else { quote!() };
        let generics = tokenize_opt(&self.generic_args);
        *tokens = quote! {
            #tokens
            #(#attrs)*
            #public enum #name #generics
        };
    }
}

/// Represents a parsable struct which accepts the following input:
/// ```
/// #derive(Debug)
/// enum Client<T>,
/// () => A,
/// A => B,
/// B => [C<T>, A],
/// C<T> => A
///
/// ```
/// If generic parameters are provided for a particular state, the same parameters must be used for
/// each instance of that state.
struct StateMachineArgs {
    enum_data: Option<EnumArgs>,
    init_state: InitStateArgs,
    transitions: Punctuated<TransitionArgs, Token![,]>,
}

impl Parse for StateMachineArgs {
    fn parse(input: &ParseBuffer<'_>) -> syn::parse::Result<Self> {
        Ok(Self {
            enum_data: parse_enum_if_declared(input)?,
            init_state: input.parse()?,
            transitions: Punctuated::parse_terminated(input)?,
        })
    }
}

/// Tokenize the given value if present, or return an empty stream.
fn tokenize_opt<T: ToTokens>(opt: &Option<T>) -> TokenStream2 {
    match opt {
        Some(content) => quote!(#content),
        None => quote!(),
    }
}

/// Combines the generic arguments for the given states into a single tokenized generic argument
/// consisting of their union.
/// E.g. [StateA<A, B>, StateB<B, C>, StateC<'l, A>] => "<'l, A, B, C>"
fn merge_generic_args(state_list: &[&StateArgs]) -> TokenStream2 {
    let mut merged = vec![];
    state_list.iter().for_each(|state| {
        if let Some(generic) = state.generic_args.as_ref() {
            generic.args.pairs().for_each(|pair| merged.push(pair.value().clone()))
        }
    });
    // We use an alphabetical sort to remove duplicates and place lifetimes first.
    merged.sort_by(|a, b| quote!(#a).to_string().cmp(&quote!(#b).to_string()));
    merged.dedup();
    if merged.is_empty() {
        quote!()
    } else {
        let mut punctuated = Punctuated::<&GenericArgument, token::Comma>::new();
        merged.into_iter().for_each(|arg| punctuated.push(arg));
        quote!(<#punctuated>)
    }
}

#[derive(Eq, PartialEq, Hash)]
struct StateTransition(Ident, Ident);

// TODO: Transitions can be specified multiple times
pub fn process(input: TokenStream) -> TokenStream {
    // Parse macro input.
    let args: StateMachineArgs = parse(input).expect("error processing macro");

    // Ensure that each state uses only one set of generic parameters.
    let mut generics = HashMap::<Ident, StateArgs>::new();
    generics.insert(args.init_state.0.name.clone(), args.init_state.0.clone());
    for transition in &args.transitions {
        let mut states = vec![transition.from_name.clone()];
        states.extend(transition.to_names.iter().cloned());
        for state in states {
            if generics.entry(state.name.clone()).or_insert(state.clone()) != &state {
                panic!("State {} uses multiple generic argument lists", state.name);
            }
        }
    }

    // Collect unique states and state transitions.
    let mut transitions = HashMap::<StateArgs, HashSet<StateArgs>>::new();
    let mut state_set = HashSet::<StateArgs>::new();
    state_set.insert(args.init_state.0.clone());
    for transition in &args.transitions {
        state_set.insert(transition.from_name.clone());
        state_set.extend(transition.to_names.iter().map(|x| x.clone()));
        transitions
            .entry(transition.from_name.clone())
            .or_insert(HashSet::new())
            .extend(transition.to_names.iter().map(|x| x.clone()));
    }

    // Check if every state is reachable.
    for state in &state_set {
        let is_reachable = transitions
            .values()
            .fold(false, |reachable, states| reachable || states.iter().any(|x| x == state));
        if state != &args.init_state.0 && !is_reachable {
            panic!("Unreachable state defined: {}", state);
        }
    }

    // Add self transitions.
    for state in &state_set {
        transitions.entry(state.clone()).or_insert(HashSet::new()).insert(state.clone());
    }

    // Make optional enum type definition:
    let mut enum_code = quote!();
    let mut transition_enums = quote!();
    if let Some(enum_data) = args.enum_data {
        // Define state machine's enum variants:
        let state_variants = state_set.iter().fold(quote!(), |code, x| {
            let name = &x.name;
            quote! {
                #code
                #name(State<#x>),
            }
        });

        // Implement From<State<S>> for the newly defined state machine:
        let enum_name = enum_data.name.clone();
        let enum_generic = tokenize_opt(&enum_data.generic_args);
        let states_from_impl = state_set.iter().fold(quote!(), |code, x| {
            let name = &x.name;
            quote! {
                #code
                impl #enum_generic From<State<#x>> for #enum_name #enum_generic {
                    fn from (state: State<#x>) -> #enum_name #enum_generic {
                        #enum_name::#name(state)
                    }
                }
            }
        });
        enum_code = quote! {
            #enum_data {
                #state_variants
            }
            #states_from_impl
        };

        // Implement transition enum for each state:
        transition_enums = transitions.iter().fold(quote!(), |code, (from, to_list)| {
            let states_enum_name = enum_data.name.clone();
            let enum_name = format_ident!("{}Transition", from.name);
            let mut transition_generics: Vec<&StateArgs> = to_list.iter().collect();
            transition_generics.push(from);
            let transition_generics = merge_generic_args(&transition_generics[..]);
            let variants = to_list.iter().fold(quote!(), |code, to| {
                let variant_name = format_ident!("To{}", to.name);
                quote! {
                    #code
                    #variant_name(#to),
                }
            });

            let from_transitions = to_list.iter().fold(quote!(), |code, to| {
                let variant_name = format_ident!("To{}", to.name);
                quote! {
                    #code
                    #enum_name::#variant_name(data) => {
                        state.transition_to(data).into()
                    },
                }
            });

            let via_transitions = to_list.iter().fold(quote!(), |code, to| {
                let variant_name = format_ident!("To{}", to.name);
                quote! {
                    #code
                    #enum_name::#variant_name(data) => {
                        transition.to(data).into()
                    },
                }
            });

            quote! {
                #code
                enum #enum_name #transition_generics {
                    #variants
                }

                impl #enum_generic MultiTransition<#states_enum_name #enum_generic, #from> for
                        #enum_name #transition_generics {
                    fn from(self, state: State<#from>) -> #states_enum_name #enum_generic {
                        match self {
                            #from_transitions
                        }
                    }
                    fn via(self, transition: Transition<#from>) -> #states_enum_name #enum_generic {
                        match self {
                            #via_transitions
                        }
                    }
                }
            }
        });
    };

    // Implement each necessary state transition:
    let transitions = transitions.iter().fold(quote!(), |code, (from, to_list)| {
        to_list.iter().fold(code, |code, to| {
            let generic = merge_generic_args(&[from, to][..]);
            quote! {
                #code
                impl #generic StateTransition<#to> for #from {
                    fn __internal_transition_to(new_state: #to) -> State<#to> {
                        State::__internal_new(new_state)
                    }
                }
            }
        })
    });

    // Make final enum, initial state and state transitions definitions.
    let initial = args.init_state;
    TokenStream::from(quote! {
        #enum_code
        #initial
        #transitions
        #transition_enums
    })
}

fn parse_enum_if_declared(input: &ParseBuffer<'_>) -> syn::parse::Result<Option<EnumArgs>> {
    if input.peek(Token![#]) || input.peek(Token![enum]) || input.peek(Token![pub]) {
        Ok(Some(input.parse()?))
    } else {
        Ok(None)
    }
}
