# User Guide

[TOC]

## Prerequisites

### Disk Storage

Ledger writes all data under `/data/ledger`. In order for data to be persisted,
ensure that a persistent partition is mounted under /data. See [minfs
setup](https://fuchsia.googlesource.com/magenta/+/master/docs/minfs.md).

### Networking

Follow the instructions in
[netstack](https://fuchsia.googlesource.com/netstack/+/d24151e74c745358b102f4f33a3c5f4d720ddc52/README.md)
to ensure that your Fuchsia has Internet access.

Run `wget` to verify that it worked:

```
wget http://example.com
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

The `inspect` command of `ledger_tool` allows you to inspect the local state of
the Ledger. It has three main subcommands:

### Pages
To get a list of local pages for the provided app, along with the current head
commits, use the `pages` subcommand:

```
ledger_tool inspect <APP_NAME> pages
```

### Commits
To get the metadata of a commit (timestamp, parents), as well as the contents
of the page at this commit, use the `commit` subcommand:

```
ledger_tool inspect <APP_NAME> commit <PAGE_ID> <COMMIT_ID>
```

### Commit graph
To get a graph of all commits of a page, use the `commit_graph` subcommand:

```
ledger_tool inspect <APP_NAME> commit_graph <PAGE_ID>
```

`commit_graph` writes a .dot file in `/tmp` containing the full commit graph of
the provided page. One can then use `scp` to download the file to the host and
compile it with dot. Use the SVG format to get additional information, such as
the commit timestamp and the content hash as tooltips. Unsynced commits are
displayed in red.

Note that Ledger should not be running while `ledger_tool inspect` is used to
avoid internal conflicts, so remember to use `killall ledger` beforehand, or
run in headless mode. Thus, `ledger_tool inspect` can only be used post-mortem.

Note also that this tool exposes commits, which are internal structures used by
the Ledger and not exposed to clients.
