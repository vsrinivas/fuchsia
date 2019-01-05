bindgen \
  --no-layout-tests \
  --whitelist-type 'eth_fifo_entry' \
  --whitelist-var 'ETH_FIFO_.+' \
  -o $FUCHSIA_DIR/garnet/lib/rust/ethernet/src/ethernet_sys.rs \
     $FUCHSIA_DIR/zircon/system/public/zircon/device/ethernet.h
