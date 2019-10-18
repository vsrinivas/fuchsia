# fuchsia.identity.keys

fuchsia.identity.keys defines protocols to manage and access key material that
is consistent across all devices provisioned with a particular identity, such
as a Fuchsia Persona.

Acheiving consistency implies synchonization between devices and this
synchronization is not instantaneous. For some implementations synchronization
may take many minutes, or even longer when devices are offline. The library
provides information about the progress of the sychronization.
