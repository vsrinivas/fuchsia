# Cobalt System Metrics Daemon

The Cobalt System Metrics Daemon starts early after boot on a Fuchsia system
and periodically measures several system metrics and logs the values to Cobalt.
The daemon is not part of the Cobalt telemetry system but rather a client
of it that happens to be owned by the Cobalt team.

If you would like to start measuring a new system metric on Fuchsia you might
consider adding the instrumentation to this daemon. That would be appropriate if

- the metric is about the Fuchsia system as a whole
- there is no other Fuchsia component where it would be more appropriate to
  add the instrumentation
- the instrumentation periodically polls some measurement