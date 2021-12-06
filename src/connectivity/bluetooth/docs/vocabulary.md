# Bluetooth Vocabulary

Some commonly encountered Bluetooth terms are overloaded, especially when used
in contexts which are not exclusively scoped to Bluetooth. This leads to
confusion and communication overhead. We therefore define the following
recommended vocabulary to encourage consistent usage.

## Host

This term is overloaded between an instance of the Bluetooth host subsystem
(which comprises bt-init, bt-gap and the bt-host driver), the bt-host
driver-layer, and the 'host' machine when performing operations on a separate
device under test (the 'target', e.g. Fuchsia test hardware)

## Adapter

The term adapter has historically been used in our code base to roughly refer to
either the controller hardware or the bt-host driver. It's use is ambiguous and
unclear. It should be avoided.

## Device

This term is overloaded between a remote unit of physical hardware capable of
communicating via bluetooth, and a part of the system bound as a device driver

### Respectful Code

Bluetooth Core Specification 5.3 updated certain terms that were identified as
inappropriate to more inclusive versions. For example, usages of 'master' and
'slave' were changed to 'central' and 'peripheral', respectively. We have
transitioned our code's terminology to the more appropriate language. We no
longer allow uses of the prior terms.

In following with the Fuchsia project's policy on
[Respectful Code](https://fuchsia.dev/fuchsia-src/contribute/respectful_code),
we use the new terms in all cases, even when referring to previous versions of
the specification, and no longer allow new uses of the prior terms.

For more information on the appropriate language mapping table published by the
Bluetooth SIG, see
[Appropriate Language Mapping Table](https://specificationrefs.bluetooth.com/language-mapping/Appropriate_Language_Mapping_Table.pdf).
