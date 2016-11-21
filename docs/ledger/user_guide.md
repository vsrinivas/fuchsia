# User Guide

## Disk Storage

Ledger writes all data under `/data/ledger`. In order for data to be persisted,
ensure that a persistent partition is mounted under /data. See [minfs
setup](https://fuchsia.googlesource.com/magenta/+/master/docs/minfs.md).

## Sync

By default Ledger runs without sync.

To enable sync, you will need an instance of the Firebase Real-Time Database.
You can get one at https://firebase.google.com/.

Ledger does not currently support authorization, so the database needs to be
public-readable and public-writable (better not to store anything private
there). You also need to set up indexes that allow Ledger to perform queries on
synced metadata. Go to the [Firebase
Console](https://console.firebase.google.com/) and set the following in
`Database / Rules`:

```
{
  "rules": {
    ".read": true,
    ".write": true,
    "$user": {
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
```

In order to point Ledger to your database, run the configuration script:

```
/system/bin/configure_ledger --firebase_id=<DATABASE_ID> --firebase_prefix=<USER_IDENTITY>
```

`DATABASE_ID` is the identifier of your Firebase project. (it's "ABC" for a
firebase database "ABC.firebaseio.com")

`USER_IDENTITY` is a stop-gap self-declared identity of the user. You can share
one instance of the database between multiple users declaring different
identities.

To disable sync, run the configuration script again with no sync parameters:

```
/system/bin/configure_ledger
```
