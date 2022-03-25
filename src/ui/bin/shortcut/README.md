# Shortcut handler

This is the home of the shortcut handler binary.

## Usage notes

The shortcut handler allows clients to register the keyboard shortcuts they are
interested in.  If the shortcut manager notices a sequence of keys matching one
of the clients' expressed shortcut interests, it notifies the respective client.
The client can then execute an action based on the shortcut that was activated.

### Shortcut registration

The clients register through the [`fuchsia.ui.shortcut/Registry`][1] protocol,
by calling [`Registry.SetView(ViewRef, client_end:Listener)`][6], providing its
[`ViewRef`][7] and the client end of the [`fuchsia.ui.shortcut/Listener`][8]
protocol by which it will receive shortcut notifications.  The `ViewRef` is
necessary for the shortcut manager to understand where in the view hierarchy
this particular client is, and this information together with the focus chain
information is used to determine which view should receive the focus.  The
client then calls [`fuchsia.ui.shortcut/Registry.RegisterShortcut`][12] as many
times as needed to define individual [`Shortcut`s][13].

### Supported shortcuts

The shortcut manager does not support arbitrary key sequences as key shortcuts.
While this choice does not yield the most universal shortcut support, it is
adequate for Fuchsia's present needs, and will be evolved as Fuchsia's shortcut
supporting needs evolve.

When registering a shortcut key sequence, the `Shortcut` API requires the
developer to pick a unique 32-bit unsigned integer as a shortcut identifier.
This identifier will be sent back via `fuchsia.ui.shortcut/Listener.OnShortcut`
when the key sequence is recognized.  At the moment there is no facility to
modify the shortcuts once defined. If clients come about that do require more
involved shortcut processing, the shortcut API will need to be evolved too to
meet that request.

A shortcut key sequence is specified as a split between a set of required keys,
set in [`Shortcut.required_keys`][14],  and a singular activation key set in
[`Shortcut.key3`][15].  The required keys all need to be armed (actuated)
before actuation of the key will have any effect. Once all the required keys
are armed, the key must have a state transition as specified in
[`Shortcut.trigger`][16] to activate the shortcut.  For example, a shortcut
like `Ctrl+Shift+A` requires `Ctrl` and `Shift` to be specified in
`Shortcut.requried_keys`, and `A` to be set in `Shortcut.key3`.  The shortcut
would therefore be triggered when the following happen in succession:

* `Shift` is pressed and held.
* `Ctrl` is pressed and held.
* `A` is pressed and held.

But, for example, the following sequence of events would *not* trigger a
shortcut as defined above:

* `Shift` is pressed and held.
* `A` is pressed and held.
* `Ctrl` is pressed and held.

The shortcut manager can trigger on key actuation or de-actuation today.  It
stands to reason that there could be more activation events defined in the
future.

A possible value for `trigger` is either `KEY_PRESSED` or
`KEY_PRESSED_AND_RELEASED`, as defined in [`fuchsia.ui.shortcut.Trigger`][18].
This may prevent some more exotic activations such as a release after an
arbitrary number of actuations (instead of after just one).

At the time of this writing, the shortcut protocol has limitations in what it
can support, and the way the API is formulated prevents some shortcuts, such as
the "sided" combination of `LeftShift+RightShift` from being defined in an
economical way. That is, it is not possible to define a single shortcut that
will trigger on both `Shift` keys held down.  Instead, you would need to define
two shortcuts, one with `LeftShift` in `required_keys` and with `RightShift` as
`key`, and another with `RightShift` in `required_keys`, and with `LeftShift`
as `key`.  This duplication is subject to combinatorial explosion for similar
"sided" shortcuts.  It does not make for an elegant API, and does not mesh well
with shortcut definition on other platforms, where there are usually no special
trigger keys.

Similarly, the current shortcut manager does not take into
account non-US keymaps, possibly leading to confusing handling of ["sacred"
shortcuts][18].  We consider this an incidental difficulty that will be removed
in the future.

### More information

Further details of the shortcut API are available in the [README.md for
`fuchsia.ui.shortcut/Shortcut.keys_required`][4].

## Implementation notes

### External notifications

The shortcut handler itself reacts to two kinds of notifications:

1. It is notified of keyboard events, through
   [`fuchsia.ui.shortcut/Manager.HandleKey3Event`][2].  Typically it is the
   [input pipeline][3] that [calls into this protocol][9].

1. It is notified of the change of focus through
   `fuchsia.ui.shortcut/Manager.HandleFocusChange`.  This is also typically
   [done by the input pipeline][10], although at the time of this writing there
   also exists an [unexpected detour via the Ermine shell][11] which should
   be removed.

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
[6]: https://cs.opensource.google/fuchsia/fuchsia/+/main:sdk/fidl/fuchsia.ui.shortcut/registry.fidl;l=20;drc=964a1196ff25317b96172952c941ba95f9520bef
[7]: https://cs.opensource.google/fuchsia/fuchsia/+/main:sdk/fidl/fuchsia.ui.views/view_ref.fidl;l=29;drc=8beb042b3e5e1ebda0a8de1413c0090704e8fd13
[8]: https://cs.opensource.google/fuchsia/fuchsia/+/main:sdk/fidl/fuchsia.ui.shortcut/registry.fidl;l=95;drc=964a1196ff25317b96172952c941ba95f9520bef
[9]: https://cs.opensource.google/fuchsia/fuchsia/+/main:src/ui/lib/input_pipeline/src/shortcut_handler.rs;l=119;drc=736d1cff60799806705e26b3473457acbfb31bb7
[10]: https://cs.opensource.google/fuchsia/fuchsia/+/main:src/ui/lib/input_pipeline/src/focus_listener.rs;l=89;drc=545093b87208d4c48a4ce6fd34374f320a4692be
[11]: https://fuchsia.googlesource.com/experiences/+/1d46b60eb0b58b7a434074c8ba70ff34f53c1947/session_shells/ermine/session/lib/main.dart#83
[12]: https://cs.opensource.google/fuchsia/fuchsia/+/main:sdk/fidl/fuchsia.ui.shortcut/registry.fidl;l=28;drc=964a1196ff25317b96172952c941ba95f9520bef
[13]: https://cs.opensource.google/fuchsia/fuchsia/+/main:sdk/fidl/fuchsia.ui.shortcut/registry.fidl;l=34;drc=964a1196ff25317b96172952c941ba95f9520bef
[14]: https://cs.opensource.google/fuchsia/fuchsia/+/main:sdk/fidl/fuchsia.ui.shortcut/registry.fidl;l=75;drc=964a1196ff25317b96172952c941ba95f9520bef
[15]: https://cs.opensource.google/fuchsia/fuchsia/+/main:sdk/fidl/fuchsia.ui.shortcut/registry.fidl;l=63;drc=964a1196ff25317b96172952c941ba95f9520bef
[16]: https://cs.opensource.google/fuchsia/fuchsia/+/main:sdk/fidl/fuchsia.ui.shortcut/registry.fidl;l=60;drc=964a1196ff25317b96172952c941ba95f9520bef
[17]: https://cs.opensource.google/fuchsia/fuchsia/+/main:sdk/fidl/fuchsia.ui.shortcut/registry.fidl;l=80;drc=964a1196ff25317b96172952c941ba95f9520bef
[18]: https://en.wikipedia.org/wiki/Keyboard_shortcut#%22Sacred%22_keybindings
