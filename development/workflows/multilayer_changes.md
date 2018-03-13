# Working on multiple layers

## Switching between layers

When you bootstrapped your development environment (see
[getting source][getting-source]), you selected a layer. Your development
environment views that layer at the latest revision and views the lower layers
at specific revisions in the past.

If you want to switch to working on a different layer, either to get the source
code for higher layers in your source tree or to see lower layers at more recent
revisions, you have two choices:

1. You can bootstrap a new development environment for that layer using
   [the same instructions you used originally][getting-source].
2. You can modify your existing development environment using the
   `fx set-layer <layer>` command. This command edits the `jiri` metadata for
   your source tree to refer to the new layer and prints instructions for how to
   actually get the source and build the newly configured layer.

## Changes that span layers

Fuchsia is divided into a number of [layers][layers]. Each layer views the
previous layers at pinned revisions, which means changes that land in one layer
are not immediately visible to the upper layers.

When making a change that spans layers, you need to think about when the
differnet layers will see the different parts of you change. For example,
suppose you want to change an interface in Zircon and affects clients in Garnet.
When you land your change in Zircon, people building Garnet will not see your
change immediately. Instead, they will start seeing your change once Garnet
updates its revision pin for Zircon.

### Soft transitions (preferred)

The preferred way to make changes that span multiple layers is to use a
*soft transition*. In a soft transition, you make a change to the lower layer
(e.g., Zircon) in such a way that the interface supports both old and new
clients. For example, if you are replacing a function, you might add the new
version and turn the old function into a wrapper for the new function.

Use the follow steps to land a soft transition:

1. Land the change in the lower layer (e.g., Zircon) that introduces the new
   interface without breaking the old interface used by the upper layer
   (e.g., Garnet).
2. Wait for the autoroll bot to update the revision of the lower layer
   used by the upper layer.
3. Land the change to the upper layer that migrates to the new interface.
4. Land a cleanup change in the lower layer that removes the old interface.

### Hard transitions

For some changes, creating a soft transition can be difficult or impossible. For
those changes, you can make a *hard transition*. In a hard transition, you make
a breaking change to the lower layer and update the upper layer manually.

Use the follow steps to land a hard transition:

1. Land the change in the lower layer (e.g., Zircon) that breaks the interface
   used by the upper layer (e.g., Garnet). At this point, the autoroll bot will
   start failing to update the upper layer.
3. Land the change to the upper layer that both migrates to the new interface
   and updates the revision of the lower layer used by the upper layer by
   editing the `revision` attribute for the import of the lower layer in the
   `//<layer>/manifest/<layer>` manifest of the upper layer.

Making a hard transition is more stressful than making a soft transition because
your change will be preventing other changes in the lower layer from becoming
available in the upper layers between steps 1 and 2.

[getting-source]: /getting_source.md "Getting source"
[layers]: /development/source_code/layers.md "Layers"
