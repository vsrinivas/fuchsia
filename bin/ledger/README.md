# Ledger

Implementation of Ledger.

## Contents

 - [app](app) implements Ledger fidl API
 - [cache](cache)
 - [cloud_sync](cloud_sync) implements Ledger synchronisation via cloud
 - [cobalt](cobalt) contains helper methods for reporting Cobalt events
 - [coroutine](coroutine) coroutine library
 - [encryption](encryption) implements encryption service for Ledger
 - [environment](environment)
 - [fidl](fidl) FIDL interfaces internal to Ledger and peridot framework (not exposed to upper layers)
 - [fidl_helpers](fidl_helpers)
 - [lock](lock)
 - [p2p_provider](p2p_provider) implements P2P primitives powering the P2P sync
 - [p2p_sync](p2p_sync) implements Ledger synchronisation via P2P
 - [storage](storage) implements persistent representation of data held in
   Ledger
 - [testing](testing) contains helper functions used for testing Ledger
 - [tests](tests) contains tests and benchmarks for Ledger
