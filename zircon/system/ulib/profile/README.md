# Profile Provider

A `svchost` library implementing fuchsia.scheduler.ProfileProvider.

The legacy methods that take direct scheduling parameters, `GetProfile`, `GetDeadlineProfile`, and
`GetAffinityProfile`, are deprecated and will be removed in the future. Prefer `SetProfileByRole`
and newer methods/protocols as they are introduced.

## Roles

Roles are an abstraction of the scheduler profile concept using names instead of direct parameters
to avoid hard-coding scheduling parameters in the codebase, enabling per-product customization and
faster workload optimization and performance tuning. A role is bound to a thread, and later other
obejcts, using `SetProfileByRole`. A set of configuration files, including per-product configs,
specifies the mappings from role names to concrete scheduling parameters.

## Configuration Files

Profile configs are JSON file with the extension `.profiles` routed to the `config/profiles` config
directory of `svchost`. Any number of profile config files may be routed, with defaults available
from bringup and core products.

The `scope` parameters in the config files determines which take precedence when more than one file
defines the same role name. The `product` scope has the highest precedence, followed by `core`, then
`bringup`, and finally none. When there is more than one role with the same name and scope, the
first one encountered takes precedence.

The JSON file has the following format:

```JSON
// example.profiles
// Comments are supported. The JSON document element must be an object.
{
  // Optional scope for role overrides. Applies to all roles in the same file.
  "scope": "bringup" | "core" | "product",

  // The map of role names to scheduler parameters.
  "profiles": {
    // A profile with fair priority.
    "fuchsia.async.loop": { "priority": 16 },

    // A profile with fair priority and affinity to CPU 0 using an array of CPU numbers.
    "fuchsia.async.loop:boot-cpu": { "priority": 16, "affinity": [ 0 ] },

    // A profile with fair priority and affinity to CPUs 0 and 1 using a CPU bitmask.
    "fuchsia.async.loop:two-cpus": { "priority": 16, "affinity": 3 },

    // A profile with deadline parameters.
    "fuchsia.drivers.acme.irq-handler": { "capacity": "500us", "deadline": "1ms", "period": "1ms" },

    // A profile with deadline parameters and affinity to CPUs 2-5 using array of CPU numbers.
    "fuchsia.drivers.acme.irq-handler:bigs": {
      "capacity": "500us", "deadline": "1ms", "period": "1ms", "affinity": [ 2, 3, 4, 5 ] },
  }, // Trailing commas are allowed.
}
```