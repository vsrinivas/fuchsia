# Fuchsia Terminal App (Moterm)

A module that displays a command line interface to the system.

To run Moterm, start the device runner: `device_runner`, pull down from the top
and select "Moterm" from the story suggestions. Then, click once within the
terminal area before typing.

## Terminal history

Terminal history is persisted in Ledger and synced between moterm instances in
real-time. In order to sync terminal history, [configure Ledger Cloud
Sync](https://fuchsia.googlesource.com/ledger/+/HEAD/docs/user_guide.md#Cloud-Sync).

**Caveat**: note that the history sync works only when moterm is run within a
story, as described above, and not when run directly through `launch moterm`.
