# Fuchsia RFC process

The Fuchsia RFC process is intended to provide a consistent and transparent path
for making project-wide, technical decisions. For example, the RFC process can
be used to evolve the project roadmap and the system architecture.

The RFC process is detailed in [RFC-0001: Fuchsia Request for Comments process](0001_rfc_process.md), along with [RFC-0006: Addendum of the RFC process for Zircon](0006_addendum_to_rfc_process_for_zircon.md).

## Summary of the process

- Review [criteria for requiring an RFC](#criteria).
- Socialize your idea.
- Draft your RFC using this [template](TEMPLATE.md).
- Iterate your idea with appropriate stakeholders.
- After stakeholders signoff, email eng-council@fuchsia.dev to prompt the Eng Council to decide whether to accept your RFC.
- If your RFC is accepted, a member of the Eng Council will comment on your change stating that the RFC is accepted, will assign the RFC a number and mark your change Code-Review +2. Your RFC can now be landed.

## Criteria for requiring an RFC {#criteria}

Criteria for requiring an RFC is detailed in [RFC-0001](0001_rfc_process.md).

The following kinds of changes must use the RFC process.

- Changing the project roadmap
- Adding constraints on future development
- Making project policy
- Changing the system architecture
- Delegating decision-making authority
- Escalations

In addition, changes in the source directories:

- /zircon
- /src/zircon
- /src/bringup

that meet the following criteria must use the RFC process as described in [RFC0006: Addendum of the RFC Process for Zircon](0006_addendum_to_rfc_process_for_zircon.md).

- Adding or removing Zircon system interfaces.
- Changing resource handling behaviors.
- Modifying isolation guarantees.
- Significant changes of performance or memory use.
- Favoring a single platform.
- Adding or Downgrading support for a platform.
- New build configurations.

Note: Documents are sorted by date reviewed.

## Active RFCs

[Gerrit link](https://fuchsia-review.googlesource.com/q/dir:docs/contribute/governance/rfcs+is:open)

## Accepted proposals

RFC                                                     | Submitted  | Reviewed   | Title
------------------------------------------------------- | ---------- | ---------- | -----
[RFC-0001](0001_rfc_process.md)                         | 2020-02-20 | 2020-02-27 | Fuchsia Request for Comments (RFC) process
[RFC-0002](0002_platform_versioning.md)                 | 2020-03-30 | 2020-04-23 | Fuchsia Platform Versioning
[RFC-0003](0003_logging.md)                             | 2020-06-03 | 2020-06-10 | Fuchsia Logging Guidelines
[RFC-0004](0004_units_of_bytes.md)                      | 2020-06-09 | 2020-07-31 | Units of Bytes
[RFC-0005](0005_blobfs_snapshots.md)                    | 2020-09-07 | 2020-09-19 | Blobfs Snapshots
[RFC-0006](0006_addendum_to_rfc_process_for_zircon.md)  | 2020-08-17 | 2020-09-24 | Addendum of the RFC Process for Zircon
[RFC-0007](0007_remove_thread_killing.md)               | 2020-09-25 | 2020-10-06 | Zircon Removal of Thread Killing
[RFC-0008](0008_remove_zx_clock_get_and_adjust.md)      | 2020-10-21 | 2020-10-29 | Remove zx_clock_get and zx_clock_adjust
[RFC-0009](0009_edge_triggered_async_wait.md)           | 2020-10-22 | 2020-11-04 | Edge triggered async_wait
[RFC-0010](0010_channel_iovec.md)                       | 2020-10-01 | 2020-11-06 | zx_channel_iovec_t support for zx_channel_write and zx_channel_call
[RFC-0011](0011_getinfo_kmemstats_extended.md)          | 2020-11-04 | 2020-11-20 | zx_object_get_info ZX_INFO_KMEM_STATS_EXTENDED
[RFC-0012](0012_zircon_discardable_memory.md)           | 2020-10-27 | 2020-12-02 | Zircon Discardable Memory

## Rejected proposals

RFC      | Submitted | Reviewed | Title
-------- | --------- | -------- | ------
_(none)_ | &nbsp;    | &nbsp;   | &nbsp;

