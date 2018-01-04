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
