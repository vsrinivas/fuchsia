# International profile preferences

The [components](/docs/glossary.md#component) in Fuchsia have access to
[FIDL services](/docs/glossary.md#fidl) providing access to the current
internationalization profile settings, as determined by the
[realm](/docs/glossary.md#realm) that the component is executing in.

There are two FIDL services providing this functionality. They are intended
for different use cases, however, so it is important to pick the correct one to
use when in doubt.

* [`fuchsia.intl.PropertyProvider`][2]

  This is the read-only access to the internationalization profile. Depending
  on the realm, it can provide per user, or system settings, as appropriate.

* [`fuchsia.settings.Intl`][1]

  This is the read-write access to the internationalization profile. Connect
  to this service when you are building programs that need to display and
  modify the internationalization settings.

[1]: https://fuchsia.dev/reference/fidl/fuchsia.settings/index
[2]: https://fuchsia.dev/reference/fidl/fuchsia.intl/index
