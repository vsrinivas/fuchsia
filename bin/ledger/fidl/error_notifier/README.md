## Generated ErrorNotifierProxy ##

The ledger API is sending a status and closing the connection on unexpected
errors for every interface that extends ledger.ErrorNotifier. These interfaces
must also implement a |Sync| method that will return when all requests called
before the |Sync| call have been handled.

When impementing such an interface, one must keep track of each connection to be
able to close it with the correct status in case of error, and must also
implement the |Sync| method.

To reduce the amount of repeating code, the files in this directory allow to
automatically generate an ErrorNotifierProxy and an ErrorNotifierDelegate.

When one wants to implement an interface extending ledger.ErrorNotifier, one
therefore only need to implement the generated delegate interface. For each
method in the initial interface, the delegate will have a corresponding method
with an additional callback taking a ::fuchsia::ledger::Status. The implementor
must call this additional callback when the call is finished, with either
Status::OK if the call is successful, or with any error status if it is not.

When one wants to bind a request, one can instantiate a new Proxy, associating
it with an implementation of the Delegate. The proxy will take care of
forwarding the call to the Delegate, as well as handling the error reporting and
the |Sync| calls.

For an example, see error\_notifier\_proxy\_base\_unittest.cc, which implements
the ErrorNotifierTestDelegate interface that is automatically generated from the
ErrorNotifierTest FIDL interface defined in fidl/error\_notifier\_test.fidl.
