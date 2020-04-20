could be
improved.

TODO: actually specify the intended API

### Handling Connection Errors

Handling connection errors systematically has been a cause of concern for
clients of FIDL v1 because method result callbacks and connection error
callbacks are implemented by different parts of the client program.

It would be desirable to consider an API which allows for localized handling of
connection errors at the point of method calls (in addition to protocol level
connection error handling as before).

See https://fuchsia-review.googlesource.com/#/c/23457/ for one example of how
a client would otherwise work around the API deficiency.

One approach towards a better API may be constructed by taking advantage of the
fact that std::function<> based callbacks are always destroyed even if they are
not invoked (such as when a connection error occurs). It is possible to
implement a callback wrapper which distinguishes these cases and allows clients
to handle them more systematically. Unfortunately such an approach may not be
able to readily distinguish between a connection error vs. proxy destruction.

Alternately we could wire in support for multiple forms of callbacks or for
multiple callbacks.

Or we could change the API entirely in favor of a more explicit Promise-style
mechanism.

There are lots of options here...

TBD (please feel free to amend / expand on this)

<!-- xrefs -->
[concepts]: /docs/concepts/fidl/overview.md
[c-family-comparison]: /docs/development/languages/fidl/guides/c-family-comparison.md
