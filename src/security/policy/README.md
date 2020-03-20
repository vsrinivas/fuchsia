# Runtime Allowlist Policies
This directory contains a set of allowlists that are read by the `appmgr` to
limit which components can access certain services and features at runtime. This
runtime enforcement enables the appmgr to block the launch of unauthorized
components from requesting the `RootResource` service or the
`deprecated_ambient_replace_as_executable` feature.

All allowlists in this directory are postfixed with `_eng` to indicate that
they are intended for engineering builds. This means they may include
additional components required for debugging and testing that are not required
by a user build.
