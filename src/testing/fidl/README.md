This directory contains common placeholder FIDL protocols and implementations
that can be used in tests:

* `placeholders.test.fidl`: FIDL library containing a simple `Echo` FIDL
  protocol definition.
* `echo_realm_placeholder`: A realm containing echo client and server components:
  * `echo_client`: A child component that is eagerly started with the realm.  Connects to the `echo_server` over the `test.placeholders.Echo` protocol.
  * `echo_server`: Implements and serves the `test.placeholders.Echo` protocol.
