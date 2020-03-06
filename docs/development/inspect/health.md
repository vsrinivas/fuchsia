# Health check

Health check is a standardized inspection metric.  Adding a `fuchsia.inspect.Health` child
to an Inspect Node gives that node the contained health information. This information can
be aggregated by system-wide health-checking tools.

## The layout of the health check node

The following properties and metrics are exported in any health check node:

| Name | Type | Description |
|------|------|-------------|
| `start_timestamp_nanos` | int64 | The monotonic clock system timestamp at which this health node was initialized (i.e. first became `STARTING UP`) |
| `message` | String | If `status==UNHEALTHY`, this includes an optional failure detail message. |
| `status` | Enum | `STARTING_UP`:<br>The health node was initialized but not yet marked running. |
|          |      | `OK`:<br>The subsystem reporting to this health node is reporting healthy. |
|          |      | `UNHEALTHY`:<br>The subsystem reporting to this health node is reporting unhealthy. |

## User guide

The following example illustrates the use of [iquery](iquery.md) for getting information about
the component health status.

Examples:

```
$ iquery --recursive `iquery --find` .
a/root.inspect:
  fuchsia.inspect.Health:
    start_timestamp_nanos = ...
    status = OK
  connections:
    0:
      fuchsia.inspect.Health:
        start_timestamp_nanos = ...
        status = STARTING_UP
  optional_database:
    fuchsia.inspect.Health:
      start_timestamp_nanos = ...
      status = UNHEALTHY
      message = "Cannot open local.file"
b/root.inspect:
  fuchsia.inspect.Health:
    start_timestamp_nanos = ...
    status = OK
c/root.inspect:
  fuchsia.inspect.Health:
    start_timestamp_nanos = ...
    status = UNHEALTHY
    message = "Failed to connect to fuchsia.example.RequiredService"

$ iquery --health a/root.inspect b/root.inspect c/root.inspect
a/root.inspect = OK
b/root.inspect = OK
c/root.inspect = UNHEALTHY (Failed to connect to fuchsia.example.RequiredService)

$ iquery --health --summary a/root.inspect b/root.inspect c/root.inspect
c/root.inspect = UNHEALTHY (Failed to connect to fuchsia.example.RequiredService)

$ iquery --health --summary a/root.inspect b/root.inspect not_found/root.inspect
not_found/root.inspect = NOT_FOUND

$ iquery --health --recursive a/root.inspect b/root.inspect c/root.inspect
a/root.inspect = HEALTHY
a/root.inspect#connections/0 = STARTING_UP
a/root.inspect#optional_database = UNHEALTHY (Cannot open local.file)
b/root.inspect = OK
c/root.inspect = UNHEALTHY (Failed to connect to fuchsia.example.RequiredService)
```

# Using health checks in components

The following sections explain how to use the library in Fuchsia components written in
various programming languages.

## Rust

```rust
use fuchsia_inspect as inspect;
use fuchsia_inspect::health;

fn main() {
  // If you have your own inspector, it's also possible to export its health.

  /* inspector needs to be initialized */
  let inspector = /* ... */
  let mut node = inspector::root();
  let mut health = fuchsia_inspect::health::Node(node);
  // ...
  health.set_ok();
  health.set_unhealthy("I'm not feeling well.");
  health.set_ok();  // The component is healthy again.
}
```
