# Elements

Reviewed on: 2020-02-04

This directory contains an example implementation of a session that instantiates a single [element proposer](./element_proposer/README.md).

The element proposer connects to the [`ElementManager`](//sdk/fidl/fuchsia.session/element_manager.fidl) service offered by the session, and uses that service to add a [`simple_element`](./simple_element/README.md) to the session.

The `simple_element` then connects to the `ElementPing` service exposed to it by the session notifies the session that it has been successfully instantiated.

## Element Session

The element session is configured to:

  1. Declare a static child: the [`element_proposer`](./element_proposer/README.md).
  2. Declare a child collection with a name which matches the one passed to the `ElementManagement` library.
  3. Expose the `ElementManager` service.
  4. Expose the `ElementPing` service.
  5. Offer the `ElementManager` service to the `element_proposer` child.
  6. Offer the `ElementPing` service to the child collection mentioned above.

Details of how this is done can be found in the [`element_session.cml`](./element_session/meta/element_session.cml) file.

Once the session is launched, it exposes the aforementioned services and starts
handling requests.

## Element Proposer

The element proposer is configured to use the `ElementManager` service. Details of how this is done can be found in the [`element_proposer.cml`]() file.

Once the element proposer is started it connects to the `ElementManager` service
and attempts to add an element (`simple_element`) to the session.

## Simple Element

The simple element is configured to use the `ElementPing` service. Details of how this is done can be found in the [`simple_element.cml`](./simple_element/meta/simple_element.cml) file.

Once the simple element is started, it will call `ElementPing::Ping()`. The
session receives the ping and log a confirmation.

## Building `element_session`

The example sessions are included in the build when you include `//src/session` with your `fx set`:

```
fx set <PRODUCT>.<BOARD> --with-base=//src/session,//src/session/bin/session_manager:session_manager.config
```

To see a list of possible products, run: `fx list-products`.

To see a list of possible boards, run: `fx list-boards`.

## Running `element_session`
### Boot into `element_session`

To boot into `element_session`, edit the [session manager cml](//src/session/bin/session_manager/meta/session_manager.cml) file to set the element session's component url as the argument:
```
"args": [ "-s", "fuchsia-pkg://fuchsia.com/element_session#meta/element_session.cm" ],
```
and run
```
fx update
```

To build the relevant components and boot into the session, follow the
instructions in [//src/session/README.md](//src/session/README.md).

### Launch `element_session` from Command Line

To instruct a running `session_manager` to launch the session, run:
```
fx shell session_control -s fuchsia-pkg://fuchsia.com/element_session#meta/element_session.cm
```

The last command should output a message stating that the element's ping has
been received.

## Testing

Add `--with //src/session:tests` to your existing `fx set` command will include  the `element_session` unit tests in the build. The resulting `fx set` looks like:
```
fx set <PRODUCT>.<BOARD> --with-base=//src/session,//src/session/bin/session_manager:session_manager.config --with //src/session:tests
```
To see a list of possible products, run: `fx list-products`.

To see a list of possible boards, run: `fx list-boards`.

The tests are available in the `element_session_tests`, `element_proposer_tests`, and `simple_element_tests` packages.
```
$ fx run-test element_session_tests
$ fx run-test element_proposer_tests
$ fx run-test simple_element_tests
```

## Source Layout

The entry point and session units tests are located in `src/main.rs`.
