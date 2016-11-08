# src

Implementation of Ledger.

## Contents

 - [app](app) implements Ledger fidl API
 - [cloud_provider](cloud_provider) encapsulates the features provided by the
   cloud that enable cloud sync
 - [convert](convert) is a helper type-conversion library
 - [fake_network_service](fake_network_service) contains a fake network service
   implementation to be used in tests
 - [firebase](firebase) is a client for the REST api of Firebase Realtime
   Database
 - [gcs](gcs) is a client for Google Cloud Storage
 - [storage](storage) implements persistant representation of data held in
   Ledger
