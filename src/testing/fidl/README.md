This directory contains common placeholder FIDL protocols and implementations
that can be used in tests:

* `placeholders.test.fidl`: FIDL library containing a simple `Echo` FIDL
  protocol definition.
* `echo_server_placeholder`: Component that implements and serves the
  `test.placeholders.Echo` protocol. Available as an individual component or in
  a single-component package.
* `echo_client_placeholder`: Component that connects to an implementation of the
  `test.placeholders.Echo` protocol. Available as an individual component or in
  a single-component package.
