# Fuchsia Terminal App (Moterm)

A module that displays a command line interface to the system.

To run Moterm, start the device runner: `device_runner`, pull down from the top
and select "Moterm" from the story suggestions. Then, click once within the
terminal area before typing.

## Terminal history

Terminal history is persisted in Ledger. In order to sync terminal history,
[configure Ledger Cloud
Sync](https://fuchsia.googlesource.com/ledger/+/HEAD/docs/user_guide.md#Cloud-Sync).

**Caveat**: the terminal history is populated from Ledger once, on Moterm
startup, so commands being entered concurrently in two Moterm sessions are not
immediately carried over to the other session.
