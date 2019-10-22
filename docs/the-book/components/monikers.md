# Monikers

A moniker identifies a specific component instance in the component tree
using a topological path.

Note: Use [component URLs][doc-component-urls] to identify the location from
which the component's manifest and assets are retrieved; use monikers to
identify a specific instance of a component.

## Types

There are three types of monikers:

- Child moniker: Denotes a child of a component instance relative to its parent.
- Relative moniker: Denotes the path from a source component instance to a
  target component instance, expressed as a sequence of child monikers.
- Absolute moniker: Denotes the path from the root of the component instance
  tree to a target component instance, expressed as a sequence of child
  monikers. Every component instance has a unique absolute moniker.

## Stability

Monikers are stable identifiers. Assuming the component topology does not
change, the monikers used to identify component instances in the topology
will remain the same.

## Uniqueness

Each time a component instance is destroyed and a new component instance with
the same name is created in its place in the component topology (as a child
of the same parent), the new instance is assigned a unique instance identifier
to distinguish it from prior instances in that place.

Monikers include unique instance identifiers to prevent confusion of old
component instances with new component instances of the same name as the
tree evolves.

## Privacy

Monikers may contain privacy-sensitive information about other components that
the user is running.

To preserve the encapsulation of the system, components should be unable to
determine the identity of other components running outside of their own
realm. Accordingly, monikers are only transmitted on a need-to-know basis
or in an obfuscated form.

For example, components are not given information about their own absolute
moniker because it would also reveal information about their parents and
ancestors.

Monikers may be collected in system logs. They are also used to implement the
component framework's persistence features.

TODO: Describe obfuscation strategy.

## Notation

This section describes the syntax used for displaying monikers to users.

### Instance and Collection Names

Parents assign names to each of their children. Dynamically created children
are arranged by their parent into named collections.

Syntax: Each name is a string of 1 to 100 of the following characters:
`a-z`, `0-9`, `_`, `.`, `-`.

See [component manifest][doc-manifests] documentation for more details.

### Instance Identifiers

Instance identifiers ensure uniqueness of monikers over time whenever a parent
destroys a component instance and creates a new one with the same name.

Syntax: Decimal formatted 32-bit unsigned integer using characters: `0-9`.

### Child Monikers

Represented by the child's collection name (if any), name, and instance
identifier delimited by `:`.

Syntax: `{name}:{id}` or `{collection}:{name}:{id}`

Examples:

- `truck:2`: child "truck" (instance id 2)
- `animals:bear:1`: child "bear" (instance id 1) in collection "animals"

TODO: Add a diagram to go along with the examples.

### Relative Monikers

Represented by the minimal sequence of child monikers encountered when tracing
upwards from a source to the common ancestor of the source and target and then
downwards to the target.

A relative path begins with `.` and is followed by path segments. `\` denotes
an upwards traversal segment. `/` denotes a downwards traversal segment. There
is no trailing `\` or `/`.

Relative monikers are invertible; a path from source to target can be
transformed into a path from target to source because information about
both paths is fully encoded by the representation.

In contrast, file system paths are not invertible because they use `..`
to denote upwards traversal so some inverse traversal information is missing.

Syntax: `.\{path from source to ancestor}/{path from ancestor to target}`

Examples:

- `.`: self - no traversal needed
- `./truck:2`: a child - traverse down `truck:2`
- `./truck:2/axle:1`: a grandchild - traverse down `truck:2` then down `axle:1`
- `.\truck:2/animals:bear:1`: a cousin - traverse up `truck:2` then down
  `animals:bear:1`
- `.\animals:bear:1/truck:2`: a cousin - inverse of the prior example,
  constructed by reversing the segments of the traversal

TODO: Add a diagram to go along with the examples.

### Absolute Monikers

Represented by the absolute path from the root to the component instance as
a sequence of child monikers.

An absolute path begins with `/` and is followed by downwards traversal path
segments delimited by `/`. There is no trailing `/`.

Syntax: `/{path from root to target}`

Examples:

- `/`: the root itself (it has no name because it has no parent)
- `/objects:2/animals:deer:1`: from root traverse down `objects:2` then down
  `animals:deer:1`

TODO: Add a diagram to go along with the examples.

[doc-manifests]: component_manifests.md
[doc-component-urls]: introduction.md#component-urls
