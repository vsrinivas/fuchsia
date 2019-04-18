
# Rust language FIDL tutorial

## About this tutorial

This tutorial describes how to make client calls and write servers in Rust
using the FIDL InterProcess Communication (**IPC**) system in Fuchsia.

Refer to the [main FIDL page](../README.md) for details on the
design and implementation of FIDL, as well as the
[instructions for getting and building Fuchsia](/docs/getting_started.md).

## Getting started

We'll use the `echo.fidl` sample that we discussed in the [FIDL Tutorial](README.md)
introduction section, by opening
[//garnet/examples/fidl/services/echo.fidl](/garnet/examples/fidl/services/echo.fidl).

<!-- NOTE: the code snippets here need to be kept up to date manually by
     copy-pasting from the actual source code. Please update a snippet
     if you notice it's out of date. -->


```fidl
library fidl.examples.echo;

[Discoverable]
protocol Echo {
    EchoString(string? value) -> (string? response);
};
```

## Build

@@@ What are the specific instructions for Rust?

## `Echo` server

The echo server implementation can be found at:
[//garnet/examples/fidl/echo_server_rust/src/main.rs](/garnet/examples/fidl/echo_server_rust/src/main.rs).

This file has two functions: `main()`, and `spawn_echo_server`:

-   The `main()` function creates an asynchronous task executor
    and a `ServicesServer` and runs the `ServicesServer` to completion on
    the executor.
-   `spawn_echo_server` spawns a new asynchronous task which will handle
    incoming echo service requests.

To understand how the code works, here's a summary of what happens in the server
to execute an IPC call. We will dig into what each of these lines means, so it's
not necessary to understand all of this before you move on.

1.  **Services Server:** The `ServicesServer` is the main top-level future
    being run on the executor. It binds itself to the startup handle of the
    current process and listens for incoming service requests.
1.  **Service Request:** When another component needs to access an "Echo"
    server, it sends a request to the `ServicesServer` containing the name of
    the service to connect to ("Echo") and a channel to connect.
1.  **Service Lookup:** The incoming service request wakes up the
    `async::Executor` executor and tells it that the `ServicesServer` task
    can now make progress and should be run. The `ServicesServer` wakes up,
    sees the request available on the startup handle of the process, and looks
    up the name of the requested service in the list of
    `(service_name, service_startup_func)` provided through calls to
    `add_service`. If a matching `service_name` exists, it calls
    `service_startup_func` with the channel to connect to the new service.
1.  **Server Creation:**  At this point in our example,
    `|chan| spawn_echo_server(chan)` is called with the channel that wants to
    be connected to an `Echo` service. `spawn_echo_server` creates a new
    future which loops over each value in the incoming stream of requests.
    It spawns that future to be run on the thread-local `async::Executor`.
1.  **API Request:** An `echo_string` request is sent on the channel.
    This makes the channel the `Echo` service is running on readable, which
    wakes up the asynchronous task spawned in `spawn_echo_server`. The task
    reads the request off of the channel and yields a value from the
    `try_next()` future.
1.  **API Response:** Upon receiving a request, the task sends a response
    back to the client with `responder.send`.

Now let's go through the code and see how this works.

### File headers

Here are the import declarations in the Rust server implementation:

```rust
use failure::{Error, ResultExt};
use fidl::endpoints2::{ServiceMarker, RequestStream};
use fidl_fidl_examples_echo::{EchoMarker, EchoRequest, EchoRequestStream};
use fuchsia_app::server::ServicesServer;
use fuchsia_async as fasync;
use futures::prelude::*;
```
-   `failure` provides conveniences for error handling, including a standard
    dynamically-dispatched `Error` type as well as a extension trait that adds
    the `context` method to `Result` for providing extra information about
    where the error occurred.
-   `fidl::endpoints2::ServiceMarker` is the trait implemented by `XXXMarker`
    types. It provides the associated string `NAME`.
-   `fidl_fidl_examples_echo` contains bindings for the `Echo` protocol.
    This file is generated from the protocol defined in `echo.fidl`.
    These bindings include:
    -   The `EchoMarker` type, a [zero-sized type] used to hold compile-time
        metadata about the `Echo` service (such as `NAME`)
    -   The `EchoRequest` type, an enum over all of the different request types
        that can be received.
    -   The `EchoRequestStream` type, a [`Stream`] of incoming requests for the
        server to handle.
-   `ServicesServer` links service requests to service launcher functions.
-   `fuchsia_async`, often aliased to the abbreviated `fasync`, is the runtime
    library for running asynchronous tasks on Fuchsia. It also provides
    asynchronous bindings to a number of Fuchsia primitives, such as channels,
    sockets, and TCP/UDP.
-   `futures` is a crate for working with asynchronous tasks. These tasks are
    composed of asynchronous units of work that may produce a single value
    (a `Future`) or many values (a `Stream`). Futures can be `await!`ed inside
    an `async` function or block, which will cause the current task to be
    suspended until the future is able to make more progress.
    For more about futures, see [the crate's documentation][docs].
    To understand more about how futures
    are structured internally, see [this post][Tokio internals] on how futures
    connect to system waiting primitives like `epoll` and Fuchsia's ports.
    Note that Fuchsia does not use Tokio, but employs a very similar strategy
    for managing asynchronous tasks.

[docs]: https://rust-lang-nursery.github.io/futures-api-docs/0.3.0-alpha.5/futures/
[`Stream`]: https://docs.rs/futures/0.2.0/futures/stream/trait.Stream.html
[Tokio internals]: https://cafbit.com/post/tokio_internals/
[zero-sized type]: https://doc.rust-lang.org/nomicon/exotic-sizes.html#zero-sized-types-zsts

### `fn main`

Everything starts with main():

```rust
fn main() -> Result<(), Error> {
    let mut executor = fasync::Executor::new().context("Error creating executor")?;
    let quiet = env::args().any(|arg| arg == "-q");

    let fut = ServicesServer::new()
                .add_service((EchoMarker::NAME, move |chan| spawn_echo_server(chan, quiet)))
                .start()
                .context("Error starting echo services server")?;

    executor.run_singlethreaded(fut).context("failed to execute echo future")?;
    Ok(())
}
```

`main` creates an asynchronous task executor and a `ServicesServer` and runs the
`ServicesServer` to completion on the executor. You may notice that `main`
returns a `Result` type: if an `Error` is returned from `main` as a result of
one of the `?` lines, the error will be `Debug` printed and the program will
return with a status code indicating failure. Functions that return `Result`,
such as `async::Executor::new()`, can have extra information appended to their
error message via the `context` function provided by `failure::ResultExt`.

The `ServicesServer` represents a collection of services that can be provided.
`add_service` takes a tuple of `service_name` and `service_start_fn`. We pass
it the name of our `Echo` service, `EchoMarker::NAME`, and a function which
takes a channel and spawns the echo server onto that channel. We then attempt
to `start` the `ServicesServer`, which binds it to the startup handle of the
current component. If that binding fails, the
"Error starting echo services server" occurs. Otherwise, we get back a `Future`
which, when run on the executor, will process and delegate incoming service
requests until a protocol error occurs or the startup handle is closed.

### `fn spawn_echo_server`

```rust
fn spawn_echo_server(chan: fasync::Channel, quiet: bool) {
    fasync::spawn(async move {
        let mut stream = EchoRequestStream::from_channel(chan);
        while let Some(EchoRequest::EchoString { value, responder }) =
            await!(stream.try_next()).context("error running echo server")?
        {
            if !quiet {
                println!("Received echo request for string {:?}", value);
            }
            responder.send(value.as_ref().map(|s| &**s)).context("error sending response")?;
            if !quiet {
                println!("echo response sent successfully");
            }
        }
        Ok(())
    }.unwrap_or_else(|e: failure::Error| eprintln!("{:?}", e)));
}
```

When a request for an echo service is received, `spawn_echo_server` is called
with the channel to host the `Echo` service on. The channel that will contain
incoming requests is turned into an `EchoRequestStream`, an asynchronous
stream of `EchoRequest`s.

We use `async move { ... }` to create an asynchronous block, and spawn that
asynchronous task onto the local executor using `fasync::spawn`.

The `.try_next()` function will return a future which yields a value of type
`Result<Option<EchoRequest>, fidl::Error>`. We `await!` the future, causing
the current task to yield if no request is yet available. When a value
becomes available, `await!` returns the result. We apply a `context("...")`
to give some information about the error that may have occurred, and use
`?` to return early in the error case. If no request is available, this
expression will result in `None`, the `while` loop will exit, and we return
`Ok`.

When a request is received, we
use pattern-matching to extract the contents of the `EchoString` variant
of the `EchoRequest` enum. For a protocol with more than one type of request,
we would instead write `|x| match x { MyServiceRequest::Req1 { ... } => ... }`.
In our case, we receive `value`, an optional string, and `responder`, a control
handle with a `send` method for sending a response.
We log the request using `println!`, and
then do a bit of complicated-looking nonsense. :)
The `as_ref().map(|s| &**s)` trick isn't related to FIDL, but is a specific
issue with converting `Option<String>` into `Option<&str>`. If you're not
interested in the details of this conversion, feel free to skip the following
paragraph.

`s` is an `Option<String>`, but our `send` method takes back an
`Option<&str>` to allow sending back non-heap-allocated strings. To convert between
the two, we use `.as_ref()` to go from `Option<String>` to `Option<&String>`,
and then `.map(|s| &**s)` to get `Option<&str>` using the `Deref<Target=str>`
implementation for `String`. The first `*` goes from `&String` to `String`,
the next goes from `String` to `str`, and the last goes from `str` to `&str`.
You might well ask why we used `as_ref` at all, since we immediately dereference
the resulting `&String`. This necessary in order to make sure that we're still
borrowing from the initial `Option<String>` value. `Option::map` takes `self`
by value and so consumes its input, but we want to instead create a *reference*
to its input.

Once we've done the conversion from `Option<String>` to `Option<&str>`, we call
`send`, which returns a `Result<(), Error>` which we use `?` on to return an
error on failure.

Finally, we call `.unwrap_or_else(|e| ...)` on our `async move { ... }` block
to handle the case in which an error occurred.

## `Echo` client

The echo client implementation can be found at:

[//garnet/examples/fidl/echo_client_rust/src/main.rs](/garnet/examples/fidl/echo_client_rust/src/main.rs)

Our simple client does everything in `main()`.

**Note:** a component can be a client, a service, or both, or many. The
distinction in this example between Client and Server is purely for
demonstration purposes.

Here is the summary of how the client makes a connection to the echo service.

1.  **Launch:** The server component is specified, and we request for it to
    be launched if it wasn't already. Note that this step isn't included in
    most production FIDL-using components: generally you're connecting with
    an already-running server component.
1.  **Connect:** We call `connect_to_service` on the launched server
    component and get back a proxy with methods for making IPC calls to
    the remote server.
1.  **Call:** We call the `echo_string` method with the desired value to
    echo, get back a `Future` of the response, and `map` the future so that
    the response will be logged once it is received.
1.  **Run:** We run the future to completion on an asynchronous task executor.

```rust
#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    #[derive(StructOpt, Debug)]
    #[structopt(name = "echo_client_rust")]
    struct Opt {
        #[structopt(long = "server", help = "URL of echo server",
                    default_value = "fuchsia-pkg://fuchsia.com/echo_server_rust#meta/echo_server_rust.cmx")]
        server_url: String,
    }

    // Launch the server and connect to the echo service.
    let Opt { server_url } = Opt::from_args();

    let launcher = Launcher::new().context("Failed to open launcher service")?;
    let app = launcher.launch(server_url, None)
                      .context("Failed to launch echo service")?;

    let echo = app.connect_to_service::<EchoMarker>()
       .context("Failed to connect to echo service")?;

    let res = await!(echo.echo_string(Some("hello world!")))?;
    println!("response: {:?}", res);
    Ok(())
}
```

### Run the sample

You can run the echo example like this:

```sh
$ run fuchsia-pkg://fuchsia.com/echo_client_rust#meta/echo_client_rust.cmx
```

