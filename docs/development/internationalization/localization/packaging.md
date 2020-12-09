# Packaging

The generated JSON file from the previous section must be bundled together with
the Fuchsia program so that it can be found at program runtime.  This is done
by the regular Fuchsia build rules, such as those in
[package.gni](/build/package.gni).

We have established some conventions for packaging resources (i.e. localized
assets). The schema is intended to be extensible to other asset types, and also
to be able to support _combinations_ of asset types which are sometimes useful
to have when expressing more complex relationships between device and locale (a
Hebrew icon version for a 200dpi display).  All paths below are relative to the
package's data directory and are found under `/pkg/data` on a running system.

| **Path** | **Description** |
|----------|-----------------|
| `assets/` | Stores all assets.  This is similar to how the <code>meta/</code> directory contains package manifests and other metadata.  In the future, this directory could contain conventional indices. |
| `assets/locales` | Stores data specifically for locales |
| `assets/locales/fr-fr` | Stores data for particular locales.  The locale names are individual directories in [BCP47](https://tools.ietf.org/html/bcp47) format. Each program contributes a single JSON file to this directory, named `program.json`, where the `program` part of the name is chosen by the author. We will, at some point, probably need to ensure that package and library names for files here do not collide. Also, due to Fuchsia's packaging strategy, it may pay to have many smaller files storing translations instead of one large one, for ease of update. |
