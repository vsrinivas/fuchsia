# Shortcut handler

This is the home of the shortcut handler binary.

## Usage notes

The shortcut handler allows clients to register the keyboard shortcuts they are
interested in.  If the shortcut manager notices a sequence of keys matching one
of the clients' expressed shortcut interests, it notifies the respective client.
The client can then execute an action based on the shortcut that was activated.

### Shortcut registration

The clients register through the [`fuchsia.ui.shortcut2/Registry`][1] protocol,
by calling [`Registry.SetView(ViewRef, client_end:Listener)`][6], providing its
[`ViewRef`][7] and the client end of the [`fuchsia.ui.shortcut/Listener`][8]
protocol by which it will receive shortcut notifications.  The `ViewRef` is
necessary for the shortcut manager to understand where in the view hierarchy
this particular client is, and this information together with the focus chain
information is used to determine which view should receive the focus.  The
client then calls [`fuchsia.ui.shortcut2/Registry.RegisterShortcut`][12] as many
times as needed to define individual [`Shortcut`s][13].

### Supported shortcuts

The shortcut manager does not support arbitrary key sequences as key shortcuts.
While this choice does not yield the most universal shortcut support, it is
adequate for Fuchsia's present needs, and will be evolved as Fuchsia's shortcut
supporting needs evolve.

When registering a shortcut key sequence, the `Shortcut` API requires the
developer to pick a unique 32-bit unsigned integer as a shortcut identifier.
This identifier will be sent back via `fuchsia.ui.shortcut2/Listener.OnShortcut`
when the key sequence is recognized.  At the moment there is no facility to
modify the shortcuts once defined. If clients come about that do require more
involved shortcut processing, the shortcut API will need to be evolved too to
meet that request.

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

<!-- xrefs -->

[1]: /sdk/fidl/fuchsia.ui.shortcut2/shortcut2.fidl
[2]: /sdk/fidl/fuchsia.ui.shortcut2/manager.fidl
[3]: https://cs.opensource.google/fuchsia/fuchsia/+/main:src/ui/lib/input_pipeline/
[6]: https://cs.opensource.google/fuchsia/fuchsia/+/main:sdk/fidl/fuchsia.ui.shortcut/registry.fidl;l=20;drc=964a1196ff25317b96172952c941ba95f9520bef
[7]: https://cs.opensource.google/fuchsia/fuchsia/+/main:sdk/fidl/fuchsia.ui.views/view_ref.fidl;l=29;drc=8beb042b3e5e1ebda0a8de1413c0090704e8fd13
[8]: https://cs.opensource.google/fuchsia/fuchsia/+/main:sdk/fidl/fuchsia.ui.shortcut/registry.fidl;l=95;drc=964a1196ff25317b96172952c941ba95f9520bef
[9]: https://cs.opensource.google/fuchsia/fuchsia/+/main:src/ui/lib/input_pipeline/src/shortcut_handler.rs;l=119;drc=736d1cff60799806705e26b3473457acbfb31bb7
[10]: https://cs.opensource.google/fuchsia/fuchsia/+/main:src/ui/lib/input_pipeline/src/focus_listener.rs;l=89;drc=545093b87208d4c48a4ce6fd34374f320a4692be
[11]: https://fuchsia.googlesource.com/experiences/+/1d46b60eb0b58b7a434074c8ba70ff34f53c1947/session_shells/ermine/session/lib/main.dart#83
[12]: https://cs.opensource.google/fuchsia/fuchsia/+/main:sdk/fidl/fuchsia.ui.shortcut/registry.fidl;l=28;drc=964a1196ff25317b96172952c941ba95f9520bef
[13]: https://cs.opensource.google/fuchsia/fuchsia/+/main:sdk/fidl/fuchsia.ui.shortcut/registry.fidl;l=34;drc=964a1196ff25317b96172952c941ba95f9520bef