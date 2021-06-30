# WLAN MLME

This library implements IEEE 802.11 MLME functions for hardware with SoftMAC capabilities.

Our SoftMAC MLME is generally instatiated inside the ['wlan'](../../drivers/wlan) driver.

## Layout

MLME is divided into two separate implementations, one for ['client STAs'](./rust/src/client/mod.rs) and one for ['APs'](./rust/src/client/mod.rs).
Each implementation is primarily a state machine (or in the case of AP, a state machine per-client).

## Rust MLME

This Module is currently transitioning from C++ to Rust. The two directories, ['cpp'](./cpp) and ['rust'](./rust) contain MLME code pre- and post-transition. When Rust MLME is complete, the cpp directory will be removed.

To facilitate this transition, we use cbindgen to generate bindings between our Rust and C++ code.