# Configuration

This document describes the configuration required for a Firestore Database
project to be used with `cloud_provider_firestore`.

*** note
These instructions are relevant only when setting up one's own server instance,
and not needed to use an existing server instance.
***

## Rules

In the [Firebase Console], under `Database > Cloud Firestore > Rules`, set the
rules to:

```
service cloud.firestore {
  match /databases/{database}/documents {
    match /users/{uid}/{everything=**} {
      allow read, write: if request.auth.uid == uid;
    }
  }
}
```

[Firebase Console]: https://console.firebase.google.com/

## Credentials

In order to run tests (end-to-end sync tests, sync benchmarks, validation tests)
against a Firestore instance, you need the following:

 - **server ID** - this is the ID of your Firestore instance
 - **API key** - available in `Project Settings / General`
 - **credentials file** for a service account - available in `Project Settings /
     Service accounts`. Click on "Generate new private key".
