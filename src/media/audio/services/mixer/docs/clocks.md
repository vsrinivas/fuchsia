# Audio Mixer Service: Clocks

Every audio stream is associated with a *reference clock*, which is a
`zx::clock` that was created with the `ZX_CLOCK_OPT_MONOTONIC` and
`ZX_CLOCK_OPT_CONTINUOUS`
[flags](https://fuchsia.dev/fuchsia-src/reference/kernel_objects/clock).
Reference clocks model the reality that different audio streams may be generated
or consumed by devices with clocks that are not perfectly synchronized to
Zircon's
[system monotonic clock](https://fuchsia.dev/fuchsia-src/reference/syscalls/clock_get_monotonic).
For example, a reference clock might model an audio device with its own clock
crystal (separate from the system's main clock crystal), or a device over a
network, such as a Bluetooth device. Although reference clocks are continuous
and monotonic, their rates can
[change over time](https://fuchsia.dev/fuchsia-src/reference/syscalls/clock_update),
causing them to drift relative to each other and relative to Zircon's system
monotonic clock.

In practice, clock rate drift is expected to be very small. Zircon clocks can
drift from the system monotonic clock rate by at most 0.1%.

## Clock reconciliation

Since every mix graph edge represents an audio stream, every mix graph edge is
associated with a reference clock. Most mix graph nodes must use the same
reference clock for all incoming and outgoing edges.

The exception is Mixer nodes, where each incoming edge may use a different
reference clock than the Mixer's outgoing edge. Mixer nodes are responsible for
clock *reconciliation*. Given a Mixer node where the incoming edge uses clock A
and the outgoing edge uses clock B, the Mixer must translate that source stream
from clock A to B before summing the source stream into the destination stream.
We perform this clock translation in two ways:

*   **Clock adjustment:** If we can *adjust* one clock to follow the other
    clock, we can pretend the source and destination clocks are the same -- even
    if they are not the same now, they should be soon -- so we can essentially
    ignore clock differences during summation. This case is explained in more
    detail below.

*   **MicroSRC:** If we cannot adjust either clock, we use sample rate
    conversion (SRC) to translate the source stream from clock A into clock B.
    For example, if the source and destination frame rates are 96kHz and 48kHz
    and the source clock runs 0.1% faster than the dest clock, then during SRC,
    instead of sampling 2 source frames per destination frame, we sample 2.001
    source frames per destination frame. We call this "micro" SRC because it
    adds a small factor on top of ordinary SRC (0.1% in this case).

## Adjustable clocks, a.k.a. optimal clocks or graph-controlled clocks

If an external client generates or consumes an audio stream but doesn't need
precise control over the stream's clock, the stream can use an "optimal" clock,
a.k.a. an "adjustable clock" or a "graph-controlled clock". Optimal clocks are
adjusted to follow a leader clock.

For example, suppose an audio renderer opts to use a graph-controlled clock C.
Suppose the system has two audio output devices, device A and device B, which
use different clocks, clock A and clock B. If the audio renderer is initially
routed to device A, we will adjust clock C's rate to follow clock A. We do this
by measuring C's current time relative to A -- if C is behind A, we increase C's
rate, and if C is ahead, we decrease C's rate. If the audio renderer is rerouted
to device B, we readjust clock C to follow clock B. This is "optimal" in the
sense that it does not require MicroSRC, so it is much less CPU intensive. The
client can see these rate updates by reading the current state of clock C, via
[`zx_clock_get_details`](https://fuchsia.dev/fuchsia-src/reference/syscalls/clock_get_details).

## MicroSRC

If the source and destination both need precise control over clocks, then
neither clock is adjustable and we must use MicroSRC. For example, MicroSRC is
necessary when the client wants to synchronize an audio stream with a display
(for A/V sync), where the display and the speaker use a different physical
clocks that each run at an uncontrollable rate.

Suppose an audio renderer is currently routed to an output device, where both
sides run at the same frame rate but use different clocks that are not
adjustable. MicroSRC works as follows. Initially, we assume the clocks are
synchronized: since the source and destination run at the same frame rate, we
advance one source frame for each destination frame. At the start of the next
mix job, we compute what our source position *should* be relative to our actual
clocks, then compare that to our *current* position based on the last frame we
read from the source stream. If our current position is behind, we need to
adjust our sample conversion rate to read from the source more quickly. If our
current position is ahead, we need to read more slowly.

For example, if the source clock is running 2x faster than the destination
clock, then during the time we advanced N destnation frames we should have
advanced 2N source frames. To makeup for this difference, the next mix job will
read from the source more quickly.

## Deciding between clock adjustment and MicroSRC

For each incoming edge of a Mixer node where the source and destination streams
use different clocks, we must decide whether we will reconcile those clocks
using *clock adjustment* or *MicroSRC*. If neither clock is adjustable, this
decision is easy: use MicroSRC. Otherwise, we go through a process of leader
assignment, as described below.

### Leader assignment for adjustable clocks

Every *adjustable* clock that is used on a path from a Producer to a Consumer
must be assigned a leader clock. There cannot be a cycle of assignments, such as
A follows B, which follows A. The simplest way to avoid cycles is to never use
an adjustable clock as a leader – all leader clocks must be unadjustable.

Suppose we create an undirected graph, where for every edge (A,B), there exists
a Mixer node `n`, where `n` exists on some path from a Producer to a Consumer,
and there is an incoming edge of `n` such that the source stream uses clock A,
`n`'s destination stream uses clock B, and A and B are different `zx::clocks`
(meaning they have a different
[koid](https://fuchsia.dev/fuchsia-src/concepts/kernel/concepts#kernel_object_ids)).
Given such a graph, once we produce a mapping from adjustable clocks to leader
clocks, we can delete all edges (A,B) where B is unadjustable and B is A's
leader, or both A and B are adjustable and have the same leader. The remaining
edges represent cases where we must use MicroSRC. Hence, the optimal leader
assignment is the one that removes the most edges.

### A global greedy heuristic

Suppose we create a directed graph, similar to the above undirected graph,
except for every source stream of a Mixer node `n`, we create a directed edges
as follows:

*   Let A be the clock used by the source stream
*   Let B be the clock used by `n`'s destination stream
*   If A and B are different clocks, and A is adjustable, create an edge B→A
*   If A and B are different clocks, and B is adjustable, create an edge A→B

As before, our graph has two kinds of nodes, adjustable and unadjustable clocks.
Edges flow from unadjustable clocks to adjustable clocks. For every unadjustable
clock, we count the number of reachable clocks. By construction, all reachable
clocks must be adjustable. We order the unadjustable clocks by the number of
reachable clocks, then produce leader assignments as follows:

Take the first unadjustable clock, A, and create a leadership assignment B→A for
every clock B that is reachable from A. Repeat for the next unadjustable clock
until all adjustable clocks are assigned leaders or there are no more
unadjustable clocks. If any adjustable clocks remain, they must form cycles in
the graph. Every remaining clock will follow the system monotonic clock.

This is *O(nm)* where the graph has *n* unadjustable clocks and *m* adjustable
clocks, hence this is worst-case quadratic in the size of the mix graph.

### A local greedy heuristic

Iterate over every Mixer source stream, in any order, and assign leaders as
follows:

*   Let A be the clock used by the source stream
*   Let B be the clock used by the Mixer's destination stream
*   If A and B are the same clocks, or neither is an adjustable clock that does
    not yet have a leader, continue to the next source stream
*   If exactly one clock (A or B) is adjustable, then the adjustable clock
    follows the unadjustable clock
*   If A and B are adjustable, and one already has a leader, then both clocks
    follow that same leader
*   Otherwise, A and B are both adjustable and neither has a leader, so A and B
    will both follow the system monotonic clock

This is linear in the size of the mix graph.

### Optimality

It is easy to construct graphs where the global heuristic outperforms the local
heuristic. We don't have a proof that the global heuristic is optimal -- in
fact, this problem is similar to set-covering problems, so it may be NP-hard. We
expect that the local heuristic will be sufficient for most graphs we see in
practice.

### Incremental assignments

Naively, we might completely recompute leader assignments each time a mix graph
edge is added or deleted, but that can lead to frequent leader changes, and if
an adjustable clock's leader changes, we may be forced to dynamically turn
MicroSRC on and off at some Mixer edges, which we'd like to avoid because it can
be tricky to change SRC algorithms smoothly.

Instead, we hold leader assignments constant until clocks are disconnected from
the graph. Leader assignment is run on the new subset of the graph:

*   On every `CreateEdge` call, we run the above local heuristic to assign
    leaders to adjustable clocks that are newly connected to the graph.

*   On every `DeleteEdge` call, we enumerate the set of adjustable clocks that
    have been disconnected from the graph. Clock A is disconnected if there does
    not exist an edge E that uses clock A such that there exists a path from E
    to any Consumer node. Disconnected clocks are not assigned a leader. They
    may be reassigned a new leader the next time they are connected to the
    graph.

### Threads and concurrency

Leader assignments must be computed on the [FIDL thread](execution_model.md),
where we have a full view of the entire graph, then the computed assignments
must be communicated to mix threads, as follows:

*   On `CreateEdge`, new leader assignments must be communicated to mix thread
    before the `CreateEdge` calls are communicated. This ensures that
    MixerStages will see the leader assignments before the first mix job uses
    the new clocks.

*   On `DeleteEdge`, dropped leader assignments must be communicated to mix
    threads after the `DeleteEdge` calls are communicated. This ensures that
    MixerStages won't lose the leader assignments until they are no longer using
    the old clocks.

Additionally, to avoid races where two mix threads try to adjust a clock at the
same time, each adjustable clock will be assigned to a controlling Consumer
node. These assignments can be done arbitrarily, but in practice it's best for a
Consumer C to control adjustable clock A only if A is used by C's pipeline tree.
These assignments are computed and communicated at the same time as leader
assignments.
