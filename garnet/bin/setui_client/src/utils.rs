// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The `utils` module contains helper functions for common operations
//! throughout the setui client.

use anyhow::Error;
use futures::{TryFutureExt, TryStream, TryStreamExt};
use std::fmt::Debug;
use std::future::Future;

/// An abstraction over a stream result from a watch, or the string output
/// from a get or set call.
pub enum Either {
    Watch(StringTryStream),
    Set(String),
    Get(String),
}
pub type StringTryStream =
    Box<dyn TryStream<Ok = String, Error = Error, Item = Result<String, Error>> + Unpin>;
pub type WatchOrSetResult = Result<Either, Error>;

/// A utility function to convert a watch into a stream of responses. Relies
/// on the output type of the watch call supporting `Debug` formatting.
pub(crate) fn watch_to_stream<P, W, Fut, T, E>(proxy: P, watch_fn: W) -> StringTryStream
where
    P: 'static,
    W: Fn(&P) -> Fut + 'static,
    Fut: Future<Output = Result<T, E>> + Unpin + 'static,
    T: Debug,
    E: Into<Error> + 'static,
{
    formatted_watch_to_stream(proxy, watch_fn, |t| format!("{:#?}", t))
}

/// A utility function to convert a watch into a stream of Responses. This variant
/// allows specifying the formatting function based on the output type of the
/// `watch` call.
pub(crate) fn formatted_watch_to_stream<P, W, Fut, F, T, E>(
    proxy: P,
    watch_fn: W,
    formatting_fn: F,
) -> StringTryStream
where
    P: 'static,
    W: Fn(&P) -> Fut + 'static,
    Fut: Future<Output = Result<T, E>> + Unpin + 'static,
    F: Fn(T) -> String + Clone + 'static,
    E: Into<Error> + 'static,
{
    Box::new(futures::stream::try_unfold(proxy, move |proxy| {
        let formatting_fn = formatting_fn.clone();
        watch_fn(&proxy)
            .map_ok(move |result| Some((formatting_fn(result), proxy)))
            .map_err(Into::into)
    }))
}

/// A utility function to display every output that comes from a watch stream.
pub(crate) async fn print_results<S>(label: &str, mut stream: S) -> Result<(), Error>
where
    S: TryStream<Ok = String, Error = Error> + Unpin,
{
    println!("Watching `{}` in a loop. Press Ctrl+C to stop.", label);
    while let Some(output) = stream.try_next().await? {
        println!("{}", output);
    }

    Ok(())
}

/// A utility function to manage outputting the results of either a watch or set
/// call.
pub(crate) async fn handle_mixed_result(
    label: &str,
    result: WatchOrSetResult,
) -> Result<(), Error> {
    Ok(match result? {
        Either::Watch(stream) => print_results(label, stream).await?,
        Either::Set(output) | Either::Get(output) => println!("{}: {}", label, output),
    })
}
