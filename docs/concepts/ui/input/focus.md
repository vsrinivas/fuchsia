# Focus

The user typically interacts with one UI window at a time. At a high level,
*focus* identifies one UI window which gets the user's interaction.

A UI window is constructed with a Fuchsia [view][view-glossary], typically just
one. Focus moves between these views, and those views must be connected to the
global view tree. When focus moves to a view, that view is notified that it has
gained focus, and the previously focused view is notified that it has lost
focus.

There is always a focused view. If a focused view becomes disconnected from the
view tree, it loses focus, and the view's parent gains focus.

Input modalities like keyboard and shortcut build their interaction models on
top of focus. For example, only the focused view may interact with the user's
keyboard. A view that loses focus cannot receive keyboard events.

Products can construct their own model of focus movement. For example, a
workstation-type product can choose to give focus to the view that is under a
mouse cursor's click. Or a touchscreen-type product can choose to give focus to
the view that is under a user's finger. Or an accessibility feature can choose
to give focus to a view, driven by vocal commands or special gestures.

This hierarchy of models allow products to construct specific guarantees for its
own use. For example, a workstation-type product wishes to ensure that a
lockscreen cannot "leak" keyboard events to any UI window behind the lock
dialog. If a UI window's view is disconnected from the view tree, it cannot
receive focus, and thus cannot interact with the keyboard.

[view-glossary]: /docs/glossary#view
