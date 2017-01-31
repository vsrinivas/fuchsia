# User Guide

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

### Prerequisities

Follow the instructions in
[netstack](https://fuchsia.googlesource.com/netstack/+/master/README.md) to
ensure that your Fuchsia has Internet access.

Run `wget` to verify that it worked:

```
@ bootstrap /system/test/wget http://example.com
```

You should see the HTML content of the `example.com` placeholder page.

### Setup

To use sync, you will need an instance of the Firebase Real-Time Database along
with Firebase Storage. You can create a new Firebase project at
https://firebase.google.com/. Then, visit the [Firebase
Console](https://console.firebase.google.com/) and follow the instructions
below.

In `Database / Rules`, paste the rules below and click "Publish". (note that
this makes the database data world-readable and world-writeable)

```
{
  "rules": {
    ".read": true,
    ".write": true,
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
```

In `Storage / Rules`, change the line starting with "allow" to match the example
below and click "Publish". (note that this makes the database data
world-readable and world-writeable) Also take note of "YOUR_BUCKET_NAME" in the
rules - you will need that in a second.

```
service firebase.storage {
  match /b/<BUCKET NAME>/o {
    match /{allPaths=**} {
      allow read, write: if true;
    }
  }
}
```

In order to point Ledger to your database, run the configuration script:

```
configure_ledger --gcs_bucket=<BUCKET NAME> --firebase_id=<DATABASE_ID> [ --cloud_prefix=<CLOUD_PREFIX> ]
```

`BUCKET_NAME` is the name of the storage bucket referenced above. Firebase
Storage is backed by GCS, so this is actually a name of a Google Cloud Storage
bucket. It however has to be the bucket of the Firebase Storage instance
configured as described above, and not any general GCS bucket.

`DATABASE_ID` is the identifier of your Firebase project. (it's "ABC" for a
firebase database "ABC.firebaseio.com")

`CLOUD_PREFIX` is a stop-gap self-declared namespace for the ledger. You can share
one instance of the cloud database between multiple ledger declaring different
identities. This parameter is not mandatory.

### Diagnose

If something seems off with sync, run the following command:

```
@ bootstrap cloud_sync doctor
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

## Reset Everything

To remove all locally persisted Ledger data, you can run:

```
$ rm -r /data/ledger
```

To remove the data synced to the cloud, visit `Database / Data` in the [Firebase
Console](https://console.firebase.google.com/) and click on the red cross that
appears when you hover over the root of your database. Then, visit `Storage /
Files` and delete all objects.
