# Migrating selectors

## Overview

`ffx` plug-ins often depend on services on Fuchsia devices. This is typically done by declaring a
dependency on a service in the plug-in's `ffx_plugin` macro call by providing a
[selector](/docs/development/tools/ffx/commands/component-select.md), such as
`core/appmgr:out:fuchsia.update.channelcontrol.ChannelControl`.

However, a selector is dependent on the location of the component which offers the service in the
system topology. And a component's location is not necessarily permanent (for instance, a
component's location might change when it's migrated from v1 to v2).

`ffx` and the Remote Control Service (RCS) provide a mechanism for maintaining a selector's
compatibility with `ffx` plug-ins when a service or its component's location is changed. This
process is detailed below.

## Process

First, you will need to commit a change to a mapping file in RCS. You can make this
change in the same CL as the component or service migration or in an earlier standalone one.

To do this, add a line to
[src/developer/remote-control/data/selector-maps.json](https://osscs.corp.google.com/fuchsia/fuchsia/+/master:src/developer/remote-control/data/selector-maps.json)
in the following format:

```none {:.devsite-disable-click-to-copy}
{
	# ...previous entries
	"some/moniker:out:fuchsia.MyService": "some/other/moniker:expose:fuchsia.MyOtherService"
}
```

In this example, requests by `ffx` plug-ins for
`some/moniker:out:fuchsia.MyService` will instead be re-routed to
`some/other/moniker:expose:fuchsia.MyOtherService` in any build which contains
this change.

Once you've verified that the mapping works as expected, send the CL to a member of the `ffx` team
for approval.

## Changing the `ffx` plug-in selectors and removing the mapping

Because the mapping is made in RCS, you're not required to make any changes to `ffx` plug-ins
themselves during the migration. However, after the migration is complete, the selector as currently
written in the `ffx_plugin` declarations becomes no longer valid, which can be a source of confusion
for future contributors. We recommend relying on the RCS mapping for a defined compatibility window
only (for example, 6 weeks, or 1 branch cut).

Once you reach the end of your compatibility window, remove the
mapping from RCS and update all of the `ffx` plug-in selectors to use the
post-migration selector for your service.