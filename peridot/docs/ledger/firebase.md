# Firebase Configuration

This document describes how to configure a Firebase project so that it can be
used for cloud sync by Ledger.

## Create the Firebase project

You can create a new Firebase project at https://firebase.google.com/.

Take note of your *project ID* - this information is needed to configure a
Fuchsia device to use your instance of Firebase. You can find out what the
project ID is by looking at the console URL, which takes the form of:
`https://console.firebase.google.com/project/<project id>`.

## Configuration
Go to the [Firebase Console](https://console.firebase.google.com/), and open
your project.

In `Database / Rules`, paste the rules below and click "Publish".

```
{
  "rules": {
    ".read": false,
    ".write": false,
    "$prefix": {
      "$user": {
        ".read": "$user === auth.uid",
        ".write": "$user === auth.uid",
        "$version": {
          "$app": {
            "$page": {
              "commits": {
                ".indexOn": ["timestamp"],
                "$id": {
                  ".validate": "!data.exists()"
                }
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
below and click "Publish".

```
service firebase.storage {
  match /b/{bucket}/o {
    match /{prefix}/{user} {
      match /{allPaths=**} {
        allow read, write: if request.auth.uid == user;
      }
    }
  }
}
```
