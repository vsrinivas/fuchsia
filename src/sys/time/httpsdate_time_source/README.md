# HTTPSDate Timesource

The HTTPSDate Timesource implements the
[`fuchsia.time.external.PushSource`][time-fidl] protocol. It retrieves time
samples by calling an HTTPS URL specified in the component configuration and
pulling the time off the Date header in the response. HTTPSDate relies on the
underlying TLS connection for server authentication. As the device on which
HTTPSDate is running may not have a valid UTC time, expiry dates on TLS
certificates are checked for consistency against the time reported by the
server.

## Key Assumptions
HTTPSDate relies on the server to conform to the standards defined for the Date
HTTP header as defined in [RFC 2616][rfc-2616], in particular that the server
has a reasonably accurate time, and that the date is sampled at the time of
message generation.

HTTPSDate also relies on some assumptions not in the RFC:
* The servers polled to produce a single sample are synced to the same time
source.
* The servers truncate time down to the second.

When these additional assumptions break down, the accuracy of time on the
device is affected:
* If servers are inconsistently synced or truncated the time samples produced
will likely contain more noise.
* If servers consistently round time in a different manner (such as rounding
up) the time samples will contain a systemic error of up to a second.

## Design Overview
HTTPSDate contains an algorithm to produce a sample from network polls, and a 
scheduler that determines when to produce a sample.

### Sampling Algorithm
The sampling algorithm is responsible for producing a single time sample.
A fundamental limitation of using HTTP Date headers is that the header has a
resolution of seconds. The sampling algorithm attempts to improve the
resolution of a produced sample by combining the results of multiple polls.
Note that for simplicity the discussion here ignores the effects of network
latency and drift between the device and server's clock, but they are taken
into account in the implementation.

The algorithm encapsulates the information obtained from polling a server once
in a _bound_. A _bound_ is a tuple `(monotonic, utc_min, utc_max)` that defines
the range of possible UTC times for a given monotonic time. When a server is
polled at monotonic time `t_mono`, the server returns some UTC time `t_server`.
Because the server is assumed to truncate time down to the nearest second,
`t_server` is a whole number of seconds, and the actual UTC time at `t_mono` is
in the range `[t_server, t_server + 1]`. This is expressed as the bound
`(t_mono, t_server, t_server + 1)`.

The following operations are possible on bounds:
* _Projection_ - A bound `(t_mono, t_utc_min, t_utc_max)` may be _projected_ to
a later monotonic time `t_mono_later` by adding `t_mono_later - t_mono` to each
value in the tuple.
* _Combination_ - Two bounds at the same monotonic time may be _combined_ by
taking the intersection of their UTC ranges. _Combining_ two polls produces
a single bound that usually has a smaller bound and therefore lower
uncertainty.

These operations allow combining the results of any two polls by _projecting_
the earlier bound to the time of the later poll, then _combining_ the resulting
polls.

The algorithm polls the server a few times and combines the resulting bounds
into a single tighter bound. The algorithm makes a best effort to poll at
intervals such that the size of the accumulated bound is halved after each
poll. The final bound is then converted to a single sample.

### Scheduler
The scheduler is responsible for periodically producing samples by invoking the
sampling algorithm, and retrying in case of failure. The scheduler progresses
through three phases that invoke the sampling algorithm at different timings
and parameters:
* _Initial phase_ - comprised of the first sample only, which is produced with
fewer polls than subsequent polls. This optimizes to start the system clock as
early as possible at the cost of accuracy.
* _Converge phase_ - comprised of the first few subsequent samples. Samples are
produced relatively frequently so that the system clock can converge on an
accurate time.
* _Maintain phase_ - comprised of all remaining samples. Samples are produced
infrequently so that an accurate time can be maintained.

[time-fidl]: https://fuchsia.dev/reference/fidl/fuchsia.time.external#PushSource
[rfc-2616]: https://tools.ietf.org/html/rfc2616
