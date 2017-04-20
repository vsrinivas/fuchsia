# User Guide

[TOC]

## Disk Storage

Ledger writes all data under `/data/ledger`. In order for data to be persisted,
ensure that a persistent partition is mounted under /data. See [minfs
setup](https://fuchsia.googlesource.com/magenta/+/master/docs/minfs.md).

## Cloud Sync

By default Ledger runs without sync. To enable sync, follow the instructions
below.

When enabled, Cloud Sync synchronizes the content of all pages using the
selected cloud provider (see below for configuration).

**Note**: Ledger does not support **migrations** between cloud providers. Once
Cloud Sync is set up, you won't be able to switch to a different cloud provider
(e.g. to a different Firebase instance) without flushing the locally persisted
data. (see Reset Everything below)

**Warning**: Ledger does not (currently) support authorization. Data stored in
the cloud is world-readable and world-writable. Please don't store anything
private, sensitive or real in Ledger yet.

### Setup

#### Setup the network stack

Follow the instructions in
[netstack](https://fuchsia.googlesource.com/netstack/+/d24151e74c745358b102f4f33a3c5f4d720ddc52/README.md)
to ensure that your Fuchsia has Internet access.

Run `wget` to verify that it worked:

```
wget http://example.com
```

You should see the HTML content of the `example.com` placeholder page.

#### Create your firebase project

To use sync, you will need an instance of the Firebase Real-Time Database along
with Firebase Storage. You can create a new Firebase project at
(https://firebase.google.com/)[https://firebase.google.com/].

Take note of your *project ID*, as you will need it to setup your device. It is
the name of your project, with spaces replaced by `-`. It is also in your
console URL. For example, if you create a project named *My Firebase Project*
project, its console URL will be
`https://console.firebase.google.com/project/my-firebase-project` and its
identifier will be `my-firebase-project`.

#### Configure Firebase
Go to the [Firebase Console](https://console.firebase.google.com/), and open your project.

In `Database / Rules`, paste the rules below and click "Publish". (note that
this makes the database data world-readable and world-writeable)

```
{
  "rules": {
    ".read": true,
    ".write": true,
    "$prefix": {
      "$user": {
        "$version": {
          "$app": {
            "$page": {
              "commits": {
                ".indexOn": ["timestamp"]
              }
            }
          }
        }
      }
    }
  }
}
```

In `Storage / Rules`, change the line starting with "allow" to match the example
below and click "Publish". (note that this makes the database data
world-readable and world-writeable).

```
service firebase.storage {
  match /b/{bucket}/o {
    match /{allPaths=**} {
      allow read, write: if true;
    }
  }
}
```

#### Configure Ledger

In order to point Ledger to your database, run the configuration script:

```
configure_ledger --firebase_id=<DATABASE_ID>
```

`DATABASE_ID` is the identifier of your Firebase database. If you followed the
setup instructions above, it is your project identifier. It is also your
firebase ID as in `https://<firebase-id>.firebaseio.com`.

### Diagnose

If something seems off with sync, run the following command:

```
cloud_sync doctor
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

## Reset the ledger

### Reset local and remote states
To remove all data, locally and remotely, run the command:

```
cloud_sync clean
```
`cloud_sync clean` only clears Firebase for now, but not the associated Google
Cloud Storage. This should not impact Ledger use; however, if you want to
reclaim the space, use the visit `Storage / Files` in the [Firebase
Console](https://console.firebase.google.com/) and delete all objects.

### Reset local state

To remove only the locally persisted Ledger data, you can run:

```
$ rm -r /data/ledger
```

Note that running this command also removes your current ledger configuration
(See (Configure Ledger)[#Configure-Ledger] on how to configure your Ledger). It
does not remove your Cloud data; if configured for Cloud synchronization,
Ledger will start downloading the missing data the next time it starts.
