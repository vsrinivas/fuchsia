// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern crate proc_macro;

mod statemachine;

use proc_macro::TokenStream;

/// Defines a state machine's initial state and its allowed transitions.
/// Optionally, a state machine enum type can be generated.
/// Example:
/// ```
/// statemachine!(
///     () => A,
///     A => B,
///     B => [C, A],
///     C => A
/// );
/// ```
/// Example to also generate optional enum type:
/// ```
/// statemachine!(
///     #derive(Debug, PartialEq)
///     pub enum Client,
///     () => A,
///     A => B,
///     B => [C, A],
///     C => A
/// );
/// ```
/// The following enum will be generated for this example:
/// ```
/// #derive(Debug, PartialEq)
/// pub enum Client {
///     A(State<A>),
///     B(State<B>),
///     C(State<C>),
/// }
/// // From impls are also generated to wrap states in the generate enum.
/// impl From<State<A>> for Client {
///    ...
/// }
///
/// ```
#[proc_macro]
pub fn statemachine(input: TokenStream) -> TokenStream {
    statemachine::process(input)
}
