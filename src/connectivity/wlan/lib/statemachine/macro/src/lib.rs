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
/// Example using generics and lifetimes:
/// ```
/// statemachine!(
///     () => A<T>,
///     A<T> => B<'a, T2>,
///     B<'a, T2> => [A<T>, C]
/// );
/// ```
/// If a generic parameter is supplied for a particular state, an identical parameter must be
/// supplied for all other instances of that state. E.g. a single state machine may not include
/// both `A<T>` and `A<T2>` as states.
///
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
///
/// If a state machine enum is generated, additional enums for state transitions are generated.
/// This is particularly useful to keep state transition and business logic separated, example:
/// ```
/// fn authenticate(auth: AuthHdr) -> AuthenticatingTransition {
///    if auth.result == ResultCode::SUCCESS {
///        ...
///        AuthenticatingTransition::ToAuthenticated(data)
///    } else {
///        ...
///        AuthenticatingTransition::ToIdle(data)
///    }
/// }
///
/// fn on_mac_frame(state: States, frame: Frame) -> Sates {
///     ...
///     let transition = authenticate(auth);
///     let next_state = state.apply(transition);
///     ...
/// }
///
/// ```
#[proc_macro]
pub fn statemachine(input: TokenStream) -> TokenStream {
    statemachine::process(input)
}
