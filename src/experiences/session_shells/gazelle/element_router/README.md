# `element_router`

`element_router` is a tiny component that helps when there's more than one
implementer of `fuchsia.element.Manager` in a session.

The `element_router` is given a mapping from component URL to
incoming-`element.Manager`-capability name, and uses that to proxy proposals to
the appropriate `element.Manager`.

This functionality probably belongs somewhere else, but at the moment it's not
clear where. So this will do for now!
