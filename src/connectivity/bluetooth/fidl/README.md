This directory contains the internal protocol used between the core bt-gap.cmx component
and the bt-host devices that it manages. This interface layer is not published in the SDK as it is
strictly internal: no component other than bt-gap.cmx is allowed to access or interact with a
bt-host (with the exception of integration tests defined under the Bluetooth subsystem directory
(//src/connectivity/bluetooth).
