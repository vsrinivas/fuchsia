# Internal Bluetooth FIDL

This directory contains protocols that are internal to the Bluetooth subsystem. These interfaces
are not published in the SDK. No component outside of the //src/connectivity/bluetooth subtree
is allowed to access or interact with them without an explicit exception.

## host.fidl

This protocol is used between the core bt-gap.cmx component and the bt-host devices that it
manages.

## lifecycle.fidl

This protocol allows components to query the state of other components' lifecycles.
There is nothing special about the current consumers of this API or Bluetooth components
requires a custom protocol for watching lifecycle events other than the fact that profile
components have side-effects in the system that are not visible through a FIDL interface
directly. This protocol provides a way to check whether the component is in a state to
perform required operations.
