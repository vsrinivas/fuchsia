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

### Firebase

Cloud sync is powered by Firebase. Either [configure](firebase.md) your own
instance or use an existing correctly configured instance that your have
permission to use.

Once you picked the instance to use, pass the Firebase project ID (see the
[Firebase configuration](firebase.md) doc) to `configure_ledger`:

```
configure_ledger --firebase_id=<FIREBASE_ID>
```

### Diagnose

If something seems off with sync, run the following command:

```
ledger_tool doctor
```

If the provided information is not enough to resolve the problem, please file a
bug and attach the output.

### Toggling Cloud Sync off and on

To disable sync:

```
configure_ledger --nosync
```

To enable it again:

```
configure_ledger --sync
```

The configuration tool remembers the Firebase settings even when the sync is
off, so you don't need to pass them again.

## Reset Ledger

### Wipe the current user
To wipe all data (local and remote) of the current user, run the command:

```
ledger_tool clean
```
`ledger_tool clean` only clears Firebase for now, but not the associated Google
Cloud Storage. This should not impact Ledger use; however, if you want to
reclaim the space, use the visit `Storage / Files` in the [Firebase
Console](https://console.firebase.google.com/) and delete all objects.

### Wipe all local state

To remove the locally persisted Ledger data for all users, you can run:

```
$ rm -r /data/ledger
```

Note that running this command also removes your current Ledger configuration
(See (Configure Ledger)[#Firebase] on how to configure your Ledger). It
does not remove your Cloud data; if configured for Cloud synchronization,
Ledger will start downloading the missing data the next time it starts.
