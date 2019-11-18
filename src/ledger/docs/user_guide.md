# User Guide

[TOC]

## Prerequisites

### Disk Storage

Ledger writes all data under `/data/modular/<USER_ID>/LEDGER`. In order for data
to be persisted, ensure that a persistent partition is mounted under /data. See
[minfs setup](/docs/concepts/filesystems/minfs).

### Networking

Follow the instructions in
[netstack](https://fuchsia.googlesource.com/netstack/+/d24151e74c745358b102f4f33a3c5f4d720ddc52/README.md)
to ensure that your Fuchsia has Internet access.

Run `curl` to verify that it worked:

```
curl http://example.com
```

You should see the HTML content of the `example.com` placeholder page.

## Cloud sync

When running in the guest mode, Ledger works locally but does not synchronize data
with the cloud.

When running in the regular mode, Ledger automatically synchronizes user data
using a communal cloud instance.

## Reset

Deleting persistent storage (e.g. `rm -rf /data`) does not erase the Ledger
state in the cloud, which will be synced back the next time a user signs in.

In order to reset the Ledger state in the cloud, visit
https://fuchsia-ledger.firebaseapp.com/ , sign in with the account you want to
reset and hit the erase button.

Any device that has local account state synced with the cloud before the erase
becomes at this point incompatible with the cloud. Ledger will detect this on
each device upon the next attempt to sync, erase all local data and log the user
out. The account is usable again immediately after logging back in.

## Debug and inspect

The **Ledger Debug Dashboard** allows you to inspect the local state of the Ledger.

You need to point your browser to the port 4001 of the Fuchsia machine to access
the dashboard of the current user.
The dashboard is also accessible from any device connected to the fuchsia machine
on the same local network.

The dashboard exposes the instances for the current user. When you select an instance,
it displays its pages. When you select a page, it displays the commits graph, where the root node is
colored in a different color than the other nodes. When you select a commit,
it displays its entries (keys, values, priorities), generation (internal Ledger counter)
and timestamp.
