// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern crate proc_macro;

use {
    proc_macro::TokenStream,
    quote::{quote, quote_spanned},
    syn::{
        parse,
        parse::{Parse, ParseStream},
        Attribute, Block, Error, Ident, ItemFn, LitBool, LitInt, LitStr, Signature, Token,
    },
};

#[derive(Clone, Copy)]
enum FunctionType {
    Component,
    Test,
}

// How should code be executed?
#[derive(Clone, Copy)]
enum Executor {
    // Directly by calling it
    None,
    // fasync::run_singlethreaded
    Singlethreaded,
    // fasync::run
    Multithreaded(usize),
    // #[test]
    Test,
    // fasync::run_singlethreaded(test)
    SinglethreadedTest,
    // fasync::run(test)
    MultithreadedTest(usize),
    // fasync::run_until_stalled
    UntilStalledTest,
}

impl Executor {
    fn is_test(&self) -> bool {
        match self {
            Executor::Test
            | Executor::SinglethreadedTest
            | Executor::MultithreadedTest(_)
            | Executor::UntilStalledTest => true,
            Executor::None | Executor::Singlethreaded | Executor::Multithreaded(_) => false,
        }
    }
}

impl quote::ToTokens for Executor {
    fn to_tokens(&self, tokens: &mut proc_macro2::TokenStream) {
        tokens.extend(match self {
            Executor::None => quote! { ::fuchsia::main_not_async(func) },
            Executor::Test => quote! { ::fuchsia::test_not_async(func) },
            Executor::Singlethreaded => quote! { ::fuchsia::main_singlethreaded(func) },
            Executor::Multithreaded(n) => quote! { ::fuchsia::main_multithreaded(func, #n) },
            Executor::SinglethreadedTest => quote! { ::fuchsia::test_singlethreaded(func) },
            Executor::MultithreadedTest(n) => quote! { ::fuchsia::test_multithreaded(func, #n) },
            Executor::UntilStalledTest => quote! { ::fuchsia::test_until_stalled(func) },
        })
    }
}

// Helper trait for things that can generate the final token stream
trait Finish {
    fn finish(self) -> TokenStream
    where
        Self: Sized;
}

struct Transformer {
    executor: Executor,
    attrs: Vec<Attribute>,
    sig: Signature,
    block: Box<Block>,
    logging: bool,
}

struct Args {
    threads: usize,
    allow_stalls: bool,
    logging: bool,
}

fn get_arg<T: Parse>(p: &ParseStream<'_>) -> syn::Result<T> {
    p.parse::<Token![=]>()?;
    p.parse()
}

fn get_base10_arg<T>(p: &ParseStream<'_>) -> syn::Result<T>
where
    T: std::str::FromStr,
    T::Err: std::fmt::Display,
{
    get_arg::<LitInt>(p)?.base10_parse()
}

fn get_bool_arg(p: &ParseStream<'_>, if_present: bool) -> syn::Result<bool> {
    if p.peek(Token![=]) {
        Ok(get_arg::<LitBool>(p)?.value)
    } else {
        Ok(if_present)
    }
}

impl Parse for Args {
    fn parse(input: ParseStream<'_>) -> syn::Result<Self> {
        let mut args = Self { threads: 1, allow_stalls: true, logging: true };

        loop {
            if input.is_empty() {
                break;
            }
            let ident: Ident = input.parse()?;
            let err = |message| Err(Error::new(ident.span(), message));
            match ident.to_string().as_ref() {
                "threads" => args.threads = get_base10_arg(&input)?,
                "allow_stalls" => args.allow_stalls = get_bool_arg(&input, true)?,
                "logging" => args.logging = get_bool_arg(&input, true)?,
                x => return err(format!("unknown argument: {}", x)),
            }
            if input.is_empty() {
                break;
            }
            input.parse::<Token![,]>()?;
        }

        Ok(args)
    }
}

impl Transformer {
    // Construct a new Transformer, verifying correctness.
    fn parse(
        function_type: FunctionType,
        args: TokenStream,
        input: TokenStream,
    ) -> Result<Transformer, Error> {
        let args: Args = parse(args)?;
        let ItemFn { attrs, vis: _, sig, block } = parse(input)?;
        let is_async = sig.asyncness.is_some();

        let err = |message| Err(Error::new(sig.ident.span(), message));

        let executor = match (args.threads, args.allow_stalls, is_async, function_type) {
            (0, _, _, _) => return err("need at least one thread"),
            (_, false, _, FunctionType::Component) => {
                return err("allow_stalls=false only applies to tests")
            }
            (1, _, false, FunctionType::Component) => Executor::None,
            (1, true, true, FunctionType::Component) => Executor::Singlethreaded,
            (n, true, true, FunctionType::Component) => Executor::Multithreaded(n),
            (1, _, false, FunctionType::Test) => Executor::Test,
            (1, true, true, FunctionType::Test) => Executor::SinglethreadedTest,
            (n, true, true, FunctionType::Test) => Executor::MultithreadedTest(n),
            (1, false, true, FunctionType::Test) => Executor::UntilStalledTest,
            (_, false, _, FunctionType::Test) => {
                return err("allow_stalls=false tests must be single threaded")
            }
            (_, true, false, _) => return err("must be async to use >1 thread"),
        };

        Ok(Transformer { executor, attrs, sig, block, logging: args.logging })
    }
}

impl Finish for Transformer {
    // Build the transformed code, knowing that everything is ok because we proved that in parse.
    fn finish(self) -> TokenStream {
        let ident = self.sig.ident;
        let span = ident.span();
        let ret_type = self.sig.output;
        let attrs = self.attrs;
        let asyncness = self.sig.asyncness;
        let block = self.block;
        let inputs = self.sig.inputs;

        let mut func_attrs = Vec::new();

        // Initialize logging
        let init_logging = if !self.logging {
            quote! {}
        } else if self.executor.is_test() {
            let test_name = LitStr::new(&format!("{}", ident), ident.span());
            quote! { ::fuchsia::init_logging_for_test(#test_name); }
        } else {
            quote! { ::fuchsia::init_logging_for_component(); }
        };

        if self.executor.is_test() {
            // Add test attribute to outer function.
            func_attrs.push(quote!(#[test]));
        }

        let func = if self.executor.is_test() {
            quote! { test_entry_point }
        } else {
            quote! { component_entry_point }
        };

        // Adapt the runner function based on whether it's a test and argument count
        // by providing needed arguments.
        let adapt_main = match (self.executor.is_test(), inputs.len()) {
            // Main function, no arguments - no adaption needed.
            (false, 0) => quote! { #func },
            // Main function, one arguemnt - adapt by parsing command line arguments.
            (false, 1) => quote! { ::fuchsia::adapt_to_parse_arguments(#func) },
            // Test function, no arguments - adapt by taking the run number and discarding it.
            (true, 0) => quote! { ::fuchsia::adapt_to_take_test_run_number(#func) },
            // Test function, one argument - no adaption needed.
            (true, 1) => quote! { #func },
            // Anything with more than one argument: error.
            (_, n) => panic!("Too many ({}) arguments to function", n),
        };

        // Select executor
        let run_executor = self.executor;

        // Finally build output.
        let output = quote_spanned! {span =>
            #(#attrs)* #(#func_attrs)*
            fn #ident () #ret_type {
                #asyncness fn #func(#inputs) #ret_type {
                    #block
                }
                #init_logging
                let func = #adapt_main;
                #run_executor
            }
        };
        output.into()
    }
}

impl Finish for Error {
    fn finish(self) -> TokenStream {
        self.to_compile_error().into()
    }
}

impl<R: Finish, E: Finish> Finish for Result<R, E> {
    fn finish(self) -> TokenStream {
        match self {
            Ok(r) => r.finish(),
            Err(e) => e.finish(),
        }
    }
}

/// Define a fuchsia component.
///
/// This attribute should be applied to the process `main` function.
/// It will take care of setting up various Fuchsia crates for the component.
/// If an async function is provided, a fuchsia-async Executor will be used to execute it.
///
/// Arguments:
///  - `threads` - integer worker thread count for the component. Must be >0. Default 1.
///  - `logging` - boolean toggle for whether to initialize logging (or not). Default true.
///                This currently does nothing on host. On Fuchsia fuchsia-syslog is used.
///
/// The main function can return either () or a Result<(), E> where E is an error type.
/// If the main function takes an argument, it's expected that argument implement
/// argh::TopLevelCommand. argh::from_env will be invoked to parse command line arguments and
/// pass them to the main function.
#[proc_macro_attribute]
pub fn component(args: TokenStream, input: TokenStream) -> TokenStream {
    Transformer::parse(FunctionType::Component, args, input).finish()
}

/// Define a fuchsia test.
///
/// This attribute should be applied to a function in a cfg(test) module.
/// It will take care of setting up various Fuchsia crates for the test.
/// If an async function is provided, a fuchsia-async Executor will be used to execute it.
///
/// Arguments:
///  - `threads`      - integer worker thread count for the test. Must be >0. Default 1.
///  - `logging`      - boolean toggle for whether to initialize logging (or not). Default true.
///                     This currently does nothing on host. On Fuchsia fuchsia-syslog is used.
///  - `allow_stalls` - boolean toggle for whether the async test is allowed to stall during
///                     execution (if true), or whether the function must complete without pausing
///                     (if false).
///                     `.await` is not a stall if something preceding the await will guarantee
///                     that it finishes within one loop of the Executor. Defaults to true.
///                     This argument is not currently available for host tests.
///
/// The test function can return either () or a Result<(), E> where E is an error type.
/// The test function can either take no arguments, or a single usize argument. If it takes an
/// argument, that value will be the current iteration count of running this test repeatedly.
#[proc_macro_attribute]
pub fn test(args: TokenStream, input: TokenStream) -> TokenStream {
    Transformer::parse(FunctionType::Test, args, input).finish()
}
