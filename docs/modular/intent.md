Intents
===

An [`Intent`](../../public/lib/intent/fidl/intent.fidl) is a runtime structure
for describing a composable action in Fuchsia.  `Intents` are produced by
3rd-party code and platform components.

Ultimately, an `Intent` will be handled by a [`Module`](module.md). A process
called [Module Resolution](module_resolution.md) finds `Modules` capable of
performing the `action` described by the `Intent`.

## Overview

`Intents` describe an `action`, its `parameters`, and optionally an explicit 
`handler` for the `action`.

If an explicit `handler` is not set, then the `Intent` is passed to a
[Module Resolution](module_resolution.md) process, which returns a set of
potential `handlers` for the `Intent`.

Once a `handler` has been specified, the data in an `Intent`'s `parameters` is
exposed to the `handler` via [`Links`](../../public/lib/story/fidl/link.fidl).

## Examples

### Creating an Intent with an explicit handler

```
// Create an Intent parameter with an entity representing the selectable
// contacts.
IntentParameterData parameter_data;
parameter_data.set_entity_reference(contacts_list_entity_ref);

IntentParameter parameter;
parameter.name = "contacts";
parameter.data = parameter_data;

Intent intent;
intent.handler = url_to_contacts_picker_module;
intent.action = "com.google.fuchsia.pick-contacts";
intent.parameters = { parameter };

module_context.StartModule("contacts-picker", intent ...);
```

The framework will find the `handler` (i.e. the contacts picker module), and 
make sure that it sees the provided contacts list entity under a link called
`contacts` (as specified by the `IntentParameter`'s name).
