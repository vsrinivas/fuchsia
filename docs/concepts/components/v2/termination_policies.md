# Termination policies {#reboot-policies}

<<../_v2_banner.md>>

This document covers policies that may be configured for reacting to
component termination.

## Reboot on terminate {#reboot-on-terminate}

A component that has the "reboot-on-terminate" policy set will cause the system
to gracefully reboot if the component terminates for any reason (including
successful exit). This is a special feature intended for use only by system
components deemed critical to the system's function. Therefore, its use is
governed by a [security policy allowlist][fidl-child-policy].

If you believe you need this option, please reach out to the Component Framework
team first.

To enable the feature, mark the child as `on_terminate: reboot` in the parent
component's [manifest][doc-manifests]:

```
// core.cml
{
    children: [
        ...
        {
            name: "system-update-checker",
            url: "fuchsia-pkg://fuchsia.com/system-update-checker#meta/system-update-checker.cm",
            startup: "eager",
            on_terminate: "reboot",
        },
    ],
}
```

Also, you'll need to add the component's moniker to component manager's security
policy allowlist at
[`//src/security/policy/component_manager_policy.json5`][src-security-policy]:

```
// //src/security/policy/component_manager_policy.json5
{
    security_policy: {
        ...
        child_policy: {
            reboot_on_terminate: [
                ...
                "/core/system-update-checker",
            ],
        },
    },
}
```

[doc-manifests]: component_manifests.md
[fidl-child-policy]: https://fuchsia.dev/reference/fidl/fuchsia.component.internal#ChildPolicyAllowlists
[src-security-policy]: /src/security/policy/component_manager_policy.json5
