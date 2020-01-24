# Inspect Rust codelab

This document contains the codelab for Inspect using Rust.

The code is available at
[//src/diagnostics/examples/inspect/rust][inspect-rust-codelab].

This codelab is organized into several parts, each with their own
subdirectory. The starting point for the codelab is [part 1][part1],
and the code for each part contains the solution for the previous parts.

When working on this codelab, you may continue adding your solutions to
"part\_1", or you may skip around by building on the existing solutions.

## Prerequisites

Set up your development environment.

This codelab assumes you have completed [Getting Started](/docs/getting_started.md) and have:

1. A checked out and built Fuchsia tree.
2. A device or emulator (`fx emu`) that runs Fuchsia.
3. A workstation to serve components (`fx serve`) to your Fuchsia device or emulator.

To build and run the examples in this codelab, add the following arguments
to your `fx set` invocation:

Note: Replace core.x64 with your product and board configuration.

```
fx set core.x64 \
--with //src/diagnostics/examples/inspect/rust \
--with //src/diagnostics/examples/inspect/rust:tests
```

## Part 1: A buggy component

There is a component that serves a protocol called [Reverser][fidl-reverser]:

```
// Implementation of a string reverser.
[Discoverable]
protocol Reverser {
    // Returns the input string reversed character-by-character.
    Reverse(string:1024 input) -> (string:1024 response);
};
```

This protocol has a single method, called "Reverse," that simply reverses
any string passed to it. An implementation of the protocol is provided,
but it has a critical bug. The bug makes clients who attempt to call
the Reverse method fail. It is up to you to fix this bug.

### Run the component

There is a client application called `inspect_rust_codelab_client` that
will launch the Reverser component and send the rest of its command line
arguments as strings to Reverse:

1. See usage

  ```
  fx shell run inspect_rust_codelab_client
  ```

2. Run part 1 code, and reverse the string "Hello"

  ```
  fx shell run inspect_rust_codelab_client 1 Hello
  ```

  This command prints some output containing errors.

3. Press Ctrl+C to stop the client and try running with
  more arguments:

  ```
  fx shell run inspect_rust_codelab_client 1 Hello World
  ```

  This command also prints errors.

You are now ready to look through the code to troubleshoot the issue.

### Look through the code

Now that you can reproduce the problem, take a look at what the
[client][client-main] is doing:

```
for string in args.strings {
    println!("Input: {}", string);
    match reverser.reverse(&string).await {
        Ok(output) => println!("Output: {}\n", output),
        Err(e) => println!("Failed to reverse string. Error: {:?}", e),
    }
}
```

In this code snippet, the client calls the `Reverse` method but never
seems to get a response. There doesn't seem to be an error message
or output.

Take a look at the [server code][part1-main] for this part of the
codelab. There is a lot of standard component setup:

- Logging initialization

  ```
  syslog::init_with_tags(&["inspect_rust_codelab", "part1"])?;
  ```

- ServiceFs initialization and collection:

  ```
  let mut fs = ServiceFs::new();
  ...
  let running_service_fs = fs.collect::<()>().map(Ok);
  ```

- Serving a public service

  ```
  fs.dir("svc").add_fidl_service(move |stream| reverser_factory.spawn_new(stream));
  fs.take_and_serve_directory_handle()?;
  ```

See what the [reverser definition][part1-reverser] is:

```
pub struct ReverserServerFactory {}

impl ReverserServerFactory {
    // CODELAB: Create a new() constructor that takes an Inspect node.
    pub fn new() -> Self {
        Self {}
    }

    pub fn spawn_new(&self, stream: ReverserRequestStream) {
        ReverserServer::new().spawn(stream);
    }
}

struct ReverserServer {}

impl ReverserServer {
    fn new() -> Self {
        Self {}
    }

    pub fn spawn(self, mut stream: ReverserRequestStream) {
        fasync::spawn_local(async move {
            while let Some(request) = stream.try_next().await.expect("serve reverser") {
                let ReverserRequest::Reverse { input, responder } = request;
                let result = input.chars().rev().collect::<String>();
                responder.send(&result).expect("send reverse request response");
            }
        });
    }
}
```

This struct serves the `Reverser` protocol. The `ReverserServerFactory` (will make more sense later)
constructs a `ReverserServer` when a new connection to `Reverser` is established.

### Add Inspect

Now that you know the code structure, you can start to instrument the
code with Inspect to find the problem.


Note: [Inspect](/docs/development/inspect/README.md) is a powerful instrumentation feature for
Fuchsia Components. You can expose structured information about the component's state to diagnose
the problem.

You may have previously debugged programs by printing or logging. While
this is often effective, asynchronous Components that run persistently
often output numerous logs about their internal state over time. This
codelab shows how Inspect provides snapshots of your component's current
state without needing to dig through logs.

1. Include Inspect dependencies in [BUILD.gn][part1-build]:

  ```
  rustc_binary("bin") {
    ...

    deps = [
      "//src/lib/inspect/rust/fuchsia-inspect",
      ...
    ]
  }
  ```

2. Initialize Inspect in [main.rs][part1-main]:

  ```
  let mut fs = ServiceFs::new();
  component::inspector().serve(&mut fs)?;
  ```

  You are now using Inspect.

3. Add a simple "version" property to show which version is running:

  ```
  component::inspector().root().record_string("version", "part1");
  ```

This snippet does the following:

1. Obtain the "root" node of the Inspect hierarchy.

  The Inspect hierarchy for your component consists of a tree of Nodes,
  each of which contains any number of properties.

2. Create a new property using `record_string`.

  This adds a new `StringProperty` on the root. This `StringProperty`
  is called "version", and its value is "part1".

3. It records it in the root node.

  The usual way of creating properties is through `create_*` methods on nodes. The lifetime of
  a property created with these methods is tied to the object returned and destroying the object
  causes the property to disappear. The library provides convinience methods `record_*` that perform
  creation of a property and tie the property lifetime to the node on which the method was called.
  As a result, the new property lives as long as the node itself (in this case, as long as the root
  node, so the entire execution of the component).

### Reading Inspect Data

Now that you have added Inspect to your component, you can read what it says:

1. Rebuild and push the component:

  ```
  fx build-push inspect_rust_codelab
  ```

  Note: In some cases you may find it useful to rebuild and update all components:

  ```
  fx build && fx update
  ```

2. Run the client (should still hang):

  ```
  fx shell run inspect_rust_codelab_client 1 Hello
  ```

3. Use `iquery` (Inspect query) to view your output:

  ```
  fx iquery
  ```

  This dumps all of the Inspect data for the entire system, which may be a lot of data.

4. Since `iquery` supports regex matching, run:

  ```
  $ fx iquery inspect_rust_codelab_part_1
  /hub/r/codelab/1234/c/inspect_rust_codelab_part_1.cmx/1234/out/diagnostics/root.inspect:
    version = part1
  ```

5. You can also view the output as JSON:

  ```
  $ fx iquery -f json inspect_rust_codelab_part_1
  [
      {
          "contents": {
              "root": {
                  "version": "part1"
              }
          },
          "path": "/hub/r/codelab/1234/c/inspect_rust_codelab_part_1.cmx/1234/out/diagnostics/root.inspect"
      }
  ]
  ```

### Instrumenting the code to find the bug

Now that you have initialized Inspect and know how to read data, you
are ready to instrument your code and uncover the bug.

The previous output shows you how the component is actually running. Otherwise the Inspect
read would hang.

Add new information per-connection to observe if the connection
is even being handled by your component.

1. Add a new child to your root node to contain statistics about the `reverser` service:

  ```
  let reverser_factory =
      ReverserServerFactory::new(component::inspector().root().create_child("reverser_service"));
  ```

2. Update the definition of `ReverserServerFactory::new` to accept this node in
  [reverser.rs][part1-reverser]:

  ```
  pub struct ReverserServerFactory {
      node: inspect::Node,
  }

  impl ReverserServerFactory {
      pub fn new(node: inspect::Node) -> Self {
          Self { node }
      }

      pub fn spawn_new(&self, stream: ReverserRequestStream) {
          ReverserServer::new().spawn(stream)
      }
  }
  ```

3. Add a property to keep track of the number of connections:

  Note: Nesting related data under a child is a powerful feature of Inspect.

  ```
  pub struct ReverserServerFactory {
      node: inspect::Node,
      connection_count: inspect::UintProperty,
  }

  impl ReverserServerFactory {
      pub fn new(node: inspect::Node) -> Self {
          let connection_count = node.create_uint("connection_count", 0);
          Self { node, connection_count }
      }

      pub fn spawn_new(&self, stream: ReverserRequestStream) {
          ReverserServer::new().spawn(stream);
          self.connection_count.add(1);
      }
  }
  ```

  This snippet demonstrates creating a new `UintProperty` (containing a
  `u64`) called `connection_count` and setting it to 0. Everytime a new connection to the service
  is established (a new server is created for each connection), the property is incremented by 1.

  Note: `node` is moved into the factory struct so that it is not dropped and
  deleted from the output.

4. Rebuild, re-run your component and then run iquery:

  ```
  $ fx iquery inspect_rust_codelab_part_1
  ...
  "contents": {
      "root": {
          "reverser_service": {
              "connection_count": 1,
          },
          "version": "part1"
      }
  }
  ```

The output above demonstrates that the client successfully connected
to the service, so the error problem must be caused by the Reverser
implementation itself. In particular, it will be helpful to know:

1. If the connection is still open while the client is failing.

2. If the `Reverse` method was called.


**Exercise**: Create a child node for each connection, and record
"request\_count" inside the Reverser.

- *Hint*: There is a utility function for generating unique names:

  ```
  let node = self.node.create_child(inspect::unique_name("connection"));
  ```

  This will create unique names starting with "connection-".

- *Hint*: You will find it helpful to create a constructor for `ReverserServer`
  that takes `inspect::Node` for the same reason as we did for `ReverserServerFactory`.

- *Hint*: You will need to create a member on Reverser to hold the
  `request_count` property. Its type will be `inspect::UintProperty`.

- *Follow up*: Does request count give you all of the information you
  need? Add `response_count` as well.

- *Advanced*: Can you add a count of *all* requests on *all*
  connections? The Reverser objects must share some state. You may find
  it helpful to refactor arguments to Reverser into a separate struct
  (See solution in [part 2](#part-2) for this approach).

After completing this exercise, you should see something like this:

```
$ fx iquery inspect_rust_codelab_part_1
...
"contents": {
    "root": {
        "reverser_service": {
            "connection-0x0": {
                "request_count": 1,
            },
            "connection_count": 1,
        },
        "version": "part1"
    }
}
```

The output above shows that the connection is still open and it received one request.

If you added "response\_count" as well, you may have noticed the bug.
The `Reverse` method receives a `callback`, but it is never called with the value of `output`.


1. Add a call to `responder.send` at the end of `ReverserServer::spawn` while loop to fix the bug:

  ```
  responder.send(&result).expect("send reverse request response");
  ```

2. Run the client again:

  ```
  fx shell run inspect_rust_codelab_client 1 hello
  Input: hello
  Output: olleh
  Done. Press Ctrl+C to exit
  ```

  The component continues running until Ctrl+C is pressed to give you
  a chance to run iquery and observe your output.

This concludes part 1. You may commit your changes so far:

```
git commit -am "solution to part 1"
```

## Part 2: Diagnosing inter-component problems {#part-2}

Note: All links and examples in this section refer to "part\_2" code. If
you are following along, you may continue using "part\_1."

You received a bug report, the "FizzBuzz" team is saying they
are not receiving data from your component.

In addition to serving the Reverser protocol, the component also reaches
out to the "FizzBuzz" service and prints the response:

```
let fizzbuzz = client::connect_to_service::<FizzBuzzMarker>()
    .context("failed to connect to fizzbuzz")?;
match fizzbuzz.execute(30u32).await {
    Ok(result) => fx_log_info!("Got FizzBuzz: {}", result),
    Err(e) => fx_log_err!("failed to FizzBuzz#Execute: {:?}", e),
};
```

If you use `fx log --tag inspect_rust_codelab`, you will see that this
log is never printed. You will need to diagnose and solve this problem.

### Diagnose the issue with Inspect

1. Run the component to see what is happening:

  Note: Replace 2 with 1 if you are continuing from part 1.

  ```
  $ fx shell run inspect_rust_codelab_client 2 hello
  ```

  Fortunately the FizzBuzz team instrumented their component using Inspect.

2. Read the FizzBuzz Inspect data:

  ```
  $ fx iquery -f json inspect_rust_codelab_fizzbuzz
  "contents": {
      "root": {
          "fizzbuzz_service": {
              "closed_connection_count": 0,
              "incoming_connection_count": 0,
              "request_count": 0,
              ...
  ```

  This output confirms that FizzBuzz is not receiving any connections.

3. Add Inspect to identify the problem:

  ```
  let fizzbuzz = client::connect_to_service::<FizzBuzzMarker>()
      .context("failed to connect to fizzbuzz")?;
  match fizzbuzz.execute(30u32).await {
      Ok(result) => {
          // CODELAB: Add Inspect here to see if there is a response.
          fx_log_info!("Got FizzBuzz: {}", result);
      },
      Err(_) => {
          // CODELAB: Add Inspect here to see if there is an error
      }
  };
  ```

**Exercise**: Add Inspect to the FizzBuzz connection to identify the problem

- *Hint*: Use the snippet above as a starting point, it provides an
error handler for the connection attempt.

- *Advanced*: `fuchsia_inspect::component` has a function called `health()` that returns an object
  that announces overall health status in a special location (a node child of the root of the
  inspect tree). Since our service is not healthy unless it can connect to FizzBuzz, can
  you incorporate this:

  ```
  /*
  "fuchsia.inspect.Health": {
      "status": "STARTING_UP"
  }
  */
  inspector.Health().StartingUp();

  /*
  "fuchsia.inspect.Health": {
      "status": "OK"
  }
  */
  inspector.Health().Ok();

  /*
  "fuchsia.inspect.Health": {
      "status": "UNHEALTHY",
      "message": "Something went wrong!"
  }
  */

  use fuchsia_inspect::component;
  component::health().set_unhealthy("Something went wrong!");
  ```

Once you complete this exercise, you should see that the connection
error handler is being called with a "not found" error. Inspect
output showed that FizzBuzz is running, so maybe something is
misconfigured. Unfortunately not everything uses Inspect (yet!) so
look at the logs:

```
$ fx log --only FizzBuzz
...
... Component fuchsia-pkg://fuchsia.com/inspect_rust_codelab_part_2.cmx
is not allowed to connect to fuchsia.examples.inspect.FizzBuzz...
```

Sandboxing errors are a common pitfall that are sometimes difficult to uncover.

Note: While you could have looked at the logs from the beginning to find
the problem, the log output for the system can be extremely verbose. The
particular log that you are looking for was a kernel log from the framework,
which is additionally difficult to test for.

Looking at the [sandbox][part2-meta], you can see it is missing the service:

```
"sandbox": {
    "services": [
        "fuchsia.logger.LogSink"
    ]
}
```

Add `"fuchsia.examples.inspect.FizzBuzz"` to the services array, rebuild,
and run again. You should now see FizzBuzz in the logs and an OK status:

```
$ fx log --tag inspect_rust_codelab
[inspect_rust_codelab, part3] INFO: main.rs(52): Got FizzBuzz: 1 2 Fizz
4 Buzz Fizz 7 8 Fizz Buzz 11 Fizz 13 14 FizzBuzz 16 17 Fizz 19 Buzz Fizz
22 23 Fizz Buzz 26 Fizz 28 29 FizzBuzz
```

This concludes Part 2.

You can now commit your solution:

```
git commit -am "solution for part 2"
```

## Part 3: Unit Testing for Inspect {#part-3}

Note: All links and examples in this section refer to "part\_3" code. If
you are following along, you may continue using the part you started with.

All code on Fuchsia should be tested, and this applies to Inspect data as well.

While Inspect data is not *required* to be tested in general, you
need to test Inspect data that is depended upon by other tools such as
Triage or Feedback.

Reverser has a basic [unit test][part3-unittest].

Run the unit test:

```
fx run-test inspect_rust_codelab_unittests
```

Note: This runs unit tests for all parts of this codelab.

The unit test ensures that Reverser works properly (and doesn't fail!), but it does
not check that the Inspect output is as expected.

Note: If you are following along from part\_1, you will need to uncomment
some lines in the [unit test][part1-unittest] and pass default values
for the Inspect properties to your Reverser (Inspect properties and nodes derive `Default`,
calling `fuchsia_inspect::*[Property | Node]::default()` will generate No-Op objects.

Passing Nodes into constructors is a form of [Dependency
Injection](https://en.wikipedia.org/wiki/Dependency_injection), which
allows you to pass in test versions of dependencies to check their state.

The code to open a Reverser looks like the following:

```
let (proxy, stream) = fidl::endpoints::create_proxy_and_stream::<ReverserMarker>()?;
let reverser = ReverserServer::new(ReverserServerMetrics::default());
reverser::spawn(stream);
```

A default version of the Inspect Node is passed into the Reverser. This
allows the reverser code to run properly in tests, but it does not
support asserting on Inspect output.

**Exercise**: Change `open_reverser` to take the dependency for a `ReverserServerFactory`
as an argument and use it when constructing Reverser.

- *Hint*: Create a `fuchsia_inspect::Inspector` in the test function. You can
  get the root using `inspector.root()`.

- *Note*: Do not use `component::inspector()` directly in your tests, this creates a static
  inspector that will be alive in all your tests and can lead to flakes or unexpected behaviors.
  For unit tests, alwas prefer to use a new `fuchsia_inspect::Inspector`

- *Hint*: You will need to create a child on the root to pass in to `ReverserServerFactory::new`.

- *Follow up*: Create multiple reverser connections and test them independently.

Following this exercise, your unit test will set real values in an
Inspect hierarchy.

Add code to test the output in Inspect (note your tree depends on your test writing, the following
is just an example):

```
use fuchsia_inspect::{Inspector, assert_inspect_tree};
...
let inspector = Inspector::new();
...
assert_inspect_tree!(inspector, root: {
    reverser_service: {
        total_requests: 3u64,
        connection_count: 2u64,
        "connection0": {
            request_count: 2u64,
            response_count: 2u64,
        },
        "connection1": {
            request_count: 1u64,
            response_count: 1u64,
        },
    }
});
```

The `assert_inspect_macro` has lots of nice features that allow you to match a part of the tree,
match only some properties, etc. Refer to the [crate
documentation](https://fuchsia-docs.firebaseapp.com/rust/fuchsia_inspect/macro.assert_inspect_tree.html)
to learn more about these features.

If you want to have control on the assertion of your hierarchy, you can read it directly:

```
use fuchsia_inspect::reader::{self, NodeHierarchy};
let hierarchy : NodeHierarchy = reader::read_from_inspector(&inspector);
```

The snippets above read a snapshot from the underlying virtual memory object (VMO)
containing Inspect data and parses it into a readable hierarchy.

**Exercise**: Add assertions for all your Inspect data.

This concludes Part 3.

You may commit your changes:

```
git commit -am "solution to part 3"
```


## Part 4: Integration Testing for Inspect

Note: All links and examples in this section refer to "part\_4" code. If
you are following along, you may continue using the part you started with.

[Integration testing](https://en.wikipedia.org/wiki/Integration_testing)
is an important part of the software development workflow for
Fuchsia. Integration tests allow you to observe the behavior of your
actual component when it runs on the system.

### Running integration tests

You can run the integration tests for the codelab as follows:

```
$ fx run-test inspect_rust_codelab_integration_tests
```

Note: This runs integration tests for all parts of this codelab.

### View the code

Look at how the integration test is setup:

1. View the [component manifest (cmx)][part4-integration-meta]:

  ```
  {
      "facets": {
          "fuchsia.test": {
              "injected-services": {
                  "fuchsia.diagnostics.Archive": "fuchsia-pkg://fuchsia.com/archivist#meta/observer.cmx"
              }
          }
      },
      "program": {
          "binary": "test/integration_test_part_4"
      },
      "sandbox": {
          "services": [
              "fuchsia.logger.LogSink",
              "fuchsia.sys.Loader",
              "fuchsia.sys.Environment"
          ]
      }
  }
  ```

  The important parts of this file are:

  - Injected services

    The `fuchsia.test` facet includes configuration for tests.

    In this file, the `fuchsia.diagnostics.Archive` service is injected
    and points to a component called `observer.cmx`. The observer collects
    information from all components in your test environment and provides
    a reading interface. You can use this information to look at your
    Inspect output.

  - Sandbox services

    Integration tests need to start other components in the test
    environment and wire them up. For this you need `fuchsia.sys.Loader`
    and `fuchsia.sys.Environment`.

2. Look at the [integration test][part4-integration] itself. The individual test cases are fairly
  straightforward:

  ```
  #[fasync::run_singlethreaded(test)]
  async fn start_with_fizzbuzz() -> Result<(), Error> {
      let mut test = IntegrationTest::start()?;
      let reverser = test.start_component_and_connect(TestOptions::default())?;
      let result = reverser.reverse("hello").await?;
      assert_eq!(result, "olleh");
      // CODELAB: Check that the component was connected to FizzBuzz.
      Ok(())
  }

  #[fasync::run_singlethreaded(test)]
  async fn start_without_fizzbuzz() -> Result<(), Error> {
      let mut test = IntegrationTest::start()?;
      let reverser = test.start_component_and_connect(TestOptions { include_fizzbuzz: false })?;
      let result = reverser.reverse("hello").await?;
      assert_eq!(result, "olleh");
      // CODELAB: Check that the component failed to connect to FizzBuzz.
      Ok(())
  }

  ```

  `IntegrationTest::start` is responsible for creating a new test
  environment and starting the codelab component inside of it. The
  `include_fizzbuzz` option instructs the method to optionally
  launch the FizzBuzz component. This feature tests that your Inspect
  output is as expected in case it fails to connect to FizzBuzz as in Part 2.

5. Add the following method to the test struct to read from the Archive service:

  ```
  use {
      ...,
      anyhow::{format_err, Context},
      fidl_fuchsia_diagnostics::{
          ArchiveMarker, BatchIteratorMarker, Format, FormattedContent, ReaderMarker,
      },
      fidl_fuchsia_mem::Buffer,
      fuchsia_component::client,
      fuchsia_inspect::{assert_inspect_tree, reader::NodeHierarchy},
      fuchsia_zircon::DurationNum,
      inspect_formatter::{json::RawJsonNodeHierarchySerializer, HierarchyDeserializer},
      serde_json,
  };

  async fn get_inspect_hierarchy(&self) -> Result<NodeHierarchy, Error> {
      let archive =
          client::connect_to_service::<ArchiveMarker>().context("connect to Archive")?;

      let (reader, server_end) = fidl::endpoints::create_proxy::<ReaderMarker>()?;
      let selectors = Vec::new();
      archive
          .read_inspect(server_end, &mut selectors.into_iter())
          .await
          .context("get Reader")?
          .map_err(|e| format_err!("accessor error: {:?}", e))?;

      loop {
          let (iterator, server_end) = fidl::endpoints::create_proxy::<BatchIteratorMarker>()?;
          reader
              .get_snapshot(Format::Json, server_end)
              .await
              .context("get BatchIterator")?
              .map_err(|e| format_err!("get snapshot: {:?}", e))?;

          if let Ok(result) = iterator.get_next().await? {
              for entry in result {
                  match entry {
                      FormattedContent::FormattedJsonHierarchy(json) => {
                          let json_string =
                              self.vmo_buffer_to_string(json).context("read vmo")?;
                          if json_string.contains(&format!(
                              "{}/inspect_rust_codelab_part_5.cmx",
                              self.environment_label
                          )) {
                              let mut output: serde_json::Value =
                                  serde_json::from_str(&json_string).expect("valid json");
                              let tree_json =
                                  output.get_mut("contents").expect("contents are there").take();
                              return RawJsonNodeHierarchySerializer::deserialize(tree_json);
                          }
                      }
                      _ => unreachable!("response should contain only json"),
                  }
              }
          }

          // Retry with delay to ensure data appears.
          150000.micros().sleep();
      }
  }

  pub fn vmo_buffer_to_string(&self, buffer: Buffer) -> Result<String, Error> {
      let buffer_size = buffer.size;
      let buffer_vmo = buffer.vmo;
      let mut bytes = vec![0; buffer_size as usize];
      buffer_vmo.read(&mut bytes, 0)?;
      Ok(String::from_utf8_lossy(&bytes).to_string())
  }
  ```

**Exercise**: Add assertions on the returned NodeHierarchy.

- *Hint*: It may help to print the JSON output to view the schema.

Your integration test will now ensure your inspect output is correct.

This concludes Part 4.

You may commit your solution:

```
git commit -am "solution to part 4"
```

## Part 5: Feedback Selectors

This section is under construction.

- TODO: Writing a feedback selector and adding tests to your integration test.

- TODO: Selectors for Feedback and other pipelines

[inspect-rust-codelab]: /src/diagnostics/examples/inspect/rust
[part1]: /src/diagnostics/examples/inspect/rust/part_1
[part1-unittest]:
/src/diagnostics/examples/inspect/rust/part_1/src/reverser.rs#46
[part1-main]: /src/diagnostics/examples/inspect/rust/part_1/src/main.rs
[part1-reverser]: /src/diagnostics/examples/inspect/rust/part_1/src/reverser.rs
[part1-build]: /src/diagnostics/examples/inspect/rust/part_1/BUILD.gn
[fidl-reverser]: /src/diagnostics/examples/inspect/fidl/reverser.test.fidl
[fidl-fizzbuzz]: /src/diagnostics/examples/inspect/fidl/fizzbuzz.test.fidl
[client-main]: /src/diagnostics/examples/inspect/rust/client/src/main.rs#41
[part2-meta]: /src/diagnostics/examples/inspect/rust/part_2/meta/inspect_rust_codelab_part_2.cmx
[part3-unittest]: /src/diagnostics/examples/inspect/rust/part_3/src/reverser.rs#99
[part3-build]: /src/diagnostics/examples/inspect/rust/part_3/BUILD.gn
[part4-integration]: /src/diagnostics/examples/inspect/rust/part_4/tests/integration_test.rs
[part4-integration-meta]: /src/diagnostics/examples/inspect/rust/meta/integration_test_part_4.cmx
