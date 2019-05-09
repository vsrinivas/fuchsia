# Bluetooth Vocabulary

Some commonly encountered Bluetooth terms are overloaded, especially when used in contexts which are not exclusively scoped to Bluetooth. This leads to confusion and communication overhead. We therefore define the following recommended vocabulary to encourage consistent usage.

## Host

This term is overloaded between an instance of the Bluetooth host subsystem (which comprises bt-init, bt-gap and the bt-host driver), the bt-host driver-layer, and the 'host' machine when performing operations on a separate device under test (the 'target', e.g. Fuchsia test hardware)

Suggestions:
* When referring solely to the Bluetooth Host driver, use the term 'bt-host' or 'bt-host driver'.
* When referring to the Bluetooth Host Subsystem, use the full term ('Bluetooth Host Subsystem')
* When referring to a host machine in a host/target setup, the term 'host' may be used.
* Do not use any of these terms outside these definitions
* In source code, namespaces may be considered part of the name, so `bt::host` is sufficient to describe the host driver, and `bt::bthost` is not required.

## Adapter

The term adapter has historically been used in our code base to roughly refer to either the controller hardware or the bt-host driver. It's use is ambiguous and unclear. It should be avoided.

Suggestions:
* **Avoid using the term 'adapter'**
* If referring to the bluetooth hardware, use the term 'controller'. E.g. "A fuchsia system may contain multiple active *controllers* at once"
* If referring to the bt-host driver (for example, within bt-gap), then use the term 'bt-host' as directed above.

## Device

This term is overloaded between a remote unit of physical hardware capable of communicating via bluetooth, and a part of the system bound as a device driver

Suggestion:
* When referring to a system device (e.g. represented in /dev), use the term ‘device’ (this is consistent with the rest of Fuchsia)
* When referring to a remote Bluetooth Host/Controller pair that we may communiate and interact with, use the term ‘peer’
* In situations where standard Bluetooth terms like 'paging device' or 'inquiring device' are used to describe peers in a specific situation, these terms may be used, but should be used in full. If in the given context it is clearer and more consistent to avoid the term 'peer', then the qualified term 'remote device' can be used, but the standalone term 'device' should be avoided.
