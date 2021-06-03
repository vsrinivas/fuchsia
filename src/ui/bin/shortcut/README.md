# Shortcut handler

This is the home of the shortcut handler binary.

The shortcut handler allows clients to register the keyboard shortcuts they are
interested in.  The clients register through
[`fuchsia.ui.shortcut/Registry`][1].

The shortcut handler is notified of keyboard events, through
[`fuchsia.ui.shortcut/Manager`][2].  Typically it is the [input pipeline][3]
that calls into this protocol.

If the shortcut manager notices a sequence of keys matching one of the clients'
interests, it notifies the respective client.

The details of the shortcut API are available in the [README.md for
`fuchsia.ui.shortcut`][4].

## Implementation notes

### Correct handling of registration events and focus changes

For efficiency reasons, the shortcut handler keeps [`focused_registries`][5], a
list of all shortcut registries that match the currently pertinent
focus chain.

This list is invalidated by:

1. A focus chain change (someone calls
  `fuchsia.ui.shortcut/Manager.HandleFocusChange`)
2. A registration of a new shortcut client (someone calls
   `fuchsia.ui.shortcut/Registry.Register`).

It is obvious why (1) invalidates: a whole another set of handlers may now be
armed after a focus goes elsewhere.  It is not quite so obvious why (2)
invalidates too.  A client may register a shortcut *after* the focus chain
update has been received.  Not recomputing at registration time will break the
focused registries invariant, as we'd have a registry with an armed `ViewRef`
that is not in `focused_registries`.

<!-- xrefs -->

[1]: /sdk/fidl/fuchsia.ui.shortcut/registry.fidl
[2]: /sdk/fidl/fuchsia.ui.shortcut/manager.fidl
[3]: /docs/concepts/session/input/README.md
[4]: /sdk/fidl/fuchsia.ui.shortcut/README.md
[5]: /src/ui/bin/shortcut/src/registry.rs

