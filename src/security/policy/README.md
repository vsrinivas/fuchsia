# Security Policy
## Overview
Security policies define a combination of runtime and compile time
restrictions that apply to a particular build type. This directory contains
the security policies for engineering builds only (which are generally
more permissive). If you need your policies to also apply to `_user` and
`_userdebug` as well then please use the corresponding security policy
directories. Policies must be set for their corresponding build type to have
any impact.

This directory is split up into:

- `component_manager_policy.json5`: For restricting V2 (.cml) capabilities at
  runtime & compile time.
- `appmgr/`: For restricting capabilities in V1 (.cmx) at runtime.
- `build/`: For build time verification configuration such as policy exception,
  verifying goldens & structured configuration.
- `pkgfs/`: For the set of pkgfs allowlists.
- `zxcrypt`: For the configuration type of the block encryption driver. Whether
   we use a null key (for testing) or the TEE keysafe implementation.

## Component V2 (.cml) Protocol Restriction
You will want to add your new protocol to
`//src/security/component_manager_policy.json5`.

This file is compiled into the Zircon Boot Image (ZBI) at the path:
`/boot/config/component_manager`. Any protocols listed in this file will
be restricted to route only to the set of `target_monikers` provided. All
other routes will be dropped. Scrutiny will additionally verify at build time
that the security policy is enforced for all build types with verifier
enable. This provides a very powerful way to restrict sensitive capabilities
on the system while being quite straight forward to use.

### Case Study: fuchsia.identity.credential.Manager
Let's take a quick look at an existing restricted protocol
`fuchsia.identity.credential.Manager`.

This is the entry in the policy file that restricts this component to only
be used by the `password_authenticator`.
```
{
    source_moniker: "/core/account/credential_manager",
    source: "component",
    source_name: "fuchsia.identity.credential.Manager",
    capability: "protocol",
    target_monikers: [
        "/core/account/credential_manager",
        "/core/account/password_authenticator",
    ],
},
```

1. `source_moniker`: Identify where the component is exposed from; in this case
   that's, `/core/account/credential_manager`.
2. `source`: This is generally just `component` if you are serving your
   `protocol` from that component. It may be that you are working on the
   `component_manager` itself in which case you may need the `framework`
   capability.
3. `capability`: Identify the capability type. In this case this is just a
   `protocol` but you can also restrict other things like `directory` or an
   `event`. Most of the time it will be a `protocol` if it is an API that you
   are restricting.
4. Test it: I suggest leaving the `target_moniker` blank and running the CL in
   CQ at this point. This should break if anything is using your capability.
   If nothing breaks you probably have a typo.
5. `target_monikers`: In this case the `protocol` is only being routed to
   the `password_authenticator` and so we just provide that one route.
   Currently there is a bug where we also have to provided the `source_moniker`
   in the set of target monikers. This documentation will be updated when that
   is no longer a requirement.
6. Upload your CL and check it in!

## Component V1 (.cmx) Protocol Restriction
Component Framework V1 is deprecated but we have support for a limited number
of protocols. Unlike V2 each V1 protocol that is restricted has its own .txt
file under: `//src/security/policy/appmgr`. Each new protocol that is added
requires a source code change.

### Adding A New Protocol
This is a more complex task that requires editing the `appmgr` source code so
reach out to the security team and we can help you out. This is specifically
only for protocols being routed to V1 (which is deprecated). Note V1
allowlists don't support compile time verification only runtime verification.

### Runtime Allowlist Policies
This directory contains a set of allowlists that are read by the `appmgr` to
limit which components can access certain services and features at runtime. This
runtime enforcement enables the appmgr to block the launch of unauthorized
components from requesting the `RootResource` service or the
`deprecated_ambient_replace_as_executable` feature.

All allowlists in this directory are postfixed with `_eng` to indicate that
they are intended for engineering builds. This means they include
additional components required for debugging and testing that are not allowed
in a user build.
