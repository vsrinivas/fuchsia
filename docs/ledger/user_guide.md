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

## Cloud Sync

By default Ledger runs without sync. When enabled, Cloud Sync synchronizes the
content of all pages using the selected cloud provider.

*** note
Ledger does not support **migrations** between cloud providers. Once
Cloud Sync is set up, you won't be able to switch to a different cloud provider
(e.g. to a different Firebase instance) without flushing the locally persisted
data (see Reset Everything below).
***

*** note
**Warning**: Ledger does not (currently) support authorization. Data stored in
the cloud is world-readable and world-writable. Please don't store anything
private, sensitive or real in Ledger yet.
***

### Configure

Cloud sync is powered by Firebase. Either [configure](firebase.md) your own
instance or use an existing correctly configured instance that you have
permission to use.

Once you picked the instance to use, add a new Fuchsia user in the device shell,
entering the Firebase project ID (see the [Firebase configuration](firebase.md)
doc) into the "firebase_id" field. After login, Ledger data of this user will
sync using the indicated Firebase instance.

### Diagnose

If something seems off with sync, run the following command:

```
ledger_tool doctor
```

If the provided information is not enough to resolve the problem, please file a
bug and attach the output.

## Reset Ledger

### Wipe the current user
To wipe all data (local and remote) of the current user, run the command:

```
ledger_tool clean
```

*** aside
**cloud storage**: `ledger_tool clean` only clears Firebase for now, but not the
associated Google Cloud Storage blobs. This should not impact Ledger use and
it's safe to leave them around. If you want to reclaim the space, the
administrator of the Firebase instance can visit `Storage / Files` in the
[Firebase Console](https://console.firebase.google.com/) and delete all objects.
***

### Wipe all local state

To remove the locally persisted Ledger data for all users, you can run:

```
$ rm -r /data/ledger
```

*** note
Running this command does not remove any data synced to the cloud; if the user
has sync configured, Ledger will start downloading the missing data the next
time it starts.
***


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
