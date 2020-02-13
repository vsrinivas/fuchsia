# Products and Boards

[**Products**][products-source] and [**Boards**][boards-source] are GN
includes used in combination to provide a baseline configuration for a
Fuchsia build.

It is expected that a GN build configuration include exactly one board GNI
file, and one product GNI file. In [fx][fx] this pair is the primary argument
to the `fx set` command.

In a Fuchsia GN build configuration the board is always included first. The
board starts the definition of three dependency lists that are then augmented
by the imported product (and later, optional GN inclusions). Those list are
[Base](#base), [Cache](#cache) and [Universe](#universe) respectively, and
are defined below.

## Boards

A board defines the architecture that the build produces for, as well as key
features of the device upon which the build is intended to run. This
configuration includes what drivers are included, and may also influence
device specific kernel parameters.

The available boards can be listed using `fx list-boards`.

## Products

A product defines the software configuration that a build will produce. Most
critically, a product typically defines the kinds of user experiences that
are provided for, such as what kind of graphical shell the user might
observe, whether or not multimedia support is included, and so on.

The available products can be listed using `fx list-products`.

## Dependency Sets

Boards define, and products augment three lists of dependencies, Base, Cache
and Universe. These dependencies are GN labels that ultimately contribute
packages to various system artifacts, such as disk images and signed package
metadata, as well as various development artifacts such as host tools and
tests.

### Base

The `base` dependency list contributes packages to disk images and system
updates as well as the package repository. A package included by the `base`
dependency set takes precedence over a duplicate membership in the `cache`
dependency set. Base packages in a system configuration are considered system
and security critical. They are updated as an atomic unit and are never
evicted at runtime regardless of resource pressure.

### Cache

The `cache` dependency list contributes packages that are pre-cached in the
disk image artifacts of the build, and will also be made available in the
package repository. These packages are not added to the system updates, but
would instead be updated ephemerally. Cached packages can also be evicted
from running systems in order to free resources based on runtime resource
demands.

### Universe

The `universe` dependency list contributes packages to the package repository
only. These packages will be available for runtime caching and updating, but
are not found in system update images nor are they pre-cached in any disk
images. All members of `base` and `cache` are inherently also members of
`universe`.

## Key Product Configurations

There are many more than below, but the following three particularly
important configurations to be familiar with:

* `bringup` is a minimal feature set product that is focused on being very
  simple and very lean. It exists to provide fast builds and small images
  (primarily used in a [netboot][fx-netboot] rather than [paved][fx-paving]
  fashion), and is great for working on very low-level facilities, such as
  the Zircon kernel or board-specific drivers and configurations. It lacks
  most network capabilities, and therefore is not able to add new software at
  runtime or upgrade itself.
* `core` is a minimal feature set that can install additional software (such as
  items added to the "universe" dependency set). It is the starting point for
  all higher-level product configurations. It has common network capabilities
  and can update a system over-the-air.
* `workstation` is a basis for a general purpose development environment, good
  for working on UI, media and many other high-level features. This is also
  the best environment for enthusiasts to play with and explore.

[products-source]: /products/
[boards-source]: /boards/
[fx]: /docs/development/build/fx.md
[fx-netboot]: /docs/development/build/fx.md#what-is-netbooting
[fx-paving]: /docs/development/build/fx.md#what-is-paving
