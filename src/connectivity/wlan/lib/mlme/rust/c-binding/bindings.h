// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_LIB_MLME_RUST_C_BINDING_BINDINGS_H_
#define SRC_CONNECTIVITY_WLAN_LIB_MLME_RUST_C_BINDING_BINDINGS_H_

// Warning:
// This file was autogenerated by cbindgen.
// Do not modify this file manually.

#include <fuchsia/hardware/wlan/softmac/c/banjo.h>
#include <fuchsia/wlan/common/c/banjo.h>
#include <fuchsia/wlan/ieee80211/c/banjo.h>
#include <fuchsia/wlan/internal/c/banjo.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

typedef struct wlan_ap_sta_t wlan_ap_sta_t;

/**
 * A STA running in Client mode.
 * The Client STA is in its early development process and does not yet manage its internal state
 * machine or track negotiated capabilities.
 */
typedef struct wlan_client_sta_t wlan_client_sta_t;

typedef struct wlan_client_mlme_t wlan_client_mlme_t;

/**
 * MlmeHandle is the only access we have to our MLME after spinning it off into its own
 * event loop thread.
 */
typedef struct wlan_mlme_handle_t wlan_mlme_handle_t;

/**
 * Manages all SNS for a STA.
 */
typedef struct mlme_sequence_manager_t mlme_sequence_manager_t;

typedef struct {
  void (*status)(void *ctx, uint32_t status);
  void (*recv)(void *ctx, const wlan_rx_packet_t *packet);
  void (*complete_tx)(void *ctx, const wlan_tx_packet_t *packet, int32_t status);
  void (*indication)(void *ctx, uint32_t ind);
  void (*report_tx_status)(void *ctx, const wlan_tx_status_t *tx_status);
  void (*scan_complete)(void *ctx, int32_t status, uint64_t scan_id);
} rust_wlanmac_ifc_protocol_ops_copy_t;

/**
 * Hand-rolled Rust version of the banjo wlanmac_ifc_protocol for communication from the driver up.
 * Note that we copy the individual fns out of this struct into the equivalent generated struct
 * in C++. Thanks to cbindgen, this gives us a compile-time confirmation that our function
 * signatures are correct.
 */
typedef struct {
  const rust_wlanmac_ifc_protocol_ops_copy_t *ops;
  void *ctx;
} rust_wlanmac_ifc_protocol_copy_t;

/**
 * An output buffer requires its owner to manage the underlying buffer's memory themselves.
 * An output buffer is used for every buffer handed from Rust to C++.
 */
typedef struct {
  /**
   * Pointer to the buffer's underlying data structure.
   */
  void *raw;
  /**
   * Pointer to the start of the buffer's data portion and the amount of bytes written.
   */
  uint8_t *data;
  uintptr_t written_bytes;
} mlme_out_buf_t;

/**
 * A `Device` allows transmitting frames and MLME messages.
 */
typedef struct {
  void *device;
  /**
   * Start operations on the underlying device and return the SME channel.
   */
  int32_t (*start)(void *device, const rust_wlanmac_ifc_protocol_copy_t *ifc,
                   zx_handle_t *out_sme_channel);
  /**
   * Request to deliver an Ethernet II frame to Fuchsia's Netstack.
   */
  int32_t (*deliver_eth_frame)(void *device, const uint8_t *data, uintptr_t len);
  /**
   * Deliver a WLAN frame directly through the firmware.
   */
  int32_t (*queue_tx)(void *device, uint32_t options, mlme_out_buf_t buf, wlan_tx_info_t tx_info);
  /**
   * Reports the current status to the ethernet driver.
   */
  void (*set_eth_status)(void *device, uint32_t status);
  /**
   * Returns the currently set WLAN channel.
   */
  wlan_channel_t (*get_wlan_channel)(void *device);
  /**
   * Request the PHY to change its channel. If successful, get_wlan_channel will return the
   * chosen channel.
   */
  int32_t (*set_wlan_channel)(void *device, wlan_channel_t channel);
  /**
   * Set a key on the device.
   * |key| is mutable because the underlying API does not take a const wlan_key_config_t.
   */
  int32_t (*set_key)(void *device, wlan_key_config_t *key);
  /**
   * Make passive scan request to the driver
   */
  zx_status_t (*start_passive_scan)(void *device,
                                    const wlanmac_passive_scan_args_t *passive_scan_args,
                                    uint64_t *out_scan_id);
  /**
   * Make active scan request to the driver
   */
  zx_status_t (*start_active_scan)(void *device, const wlanmac_active_scan_args_t *active_scan_args,
                                   uint64_t *out_scan_id);
  /**
   * Get information and capabilities of this WLAN interface
   */
  wlanmac_info_t (*get_wlanmac_info)(void *device);
  /**
   * Configure the device's BSS.
   * |cfg| is mutable because the underlying API does not take a const bss_config_t.
   */
  int32_t (*configure_bss)(void *device, bss_config_t *cfg);
  /**
   * Enable hardware offload of beaconing on the device.
   */
  int32_t (*enable_beaconing)(void *device, mlme_out_buf_t buf, uintptr_t tim_ele_offset,
                              uint16_t beacon_interval);
  /**
   * Disable beaconing on the device.
   */
  int32_t (*disable_beaconing)(void *device);
  /**
   * Reconfigure the enabled beacon on the device.
   */
  int32_t (*configure_beacon)(void *device, mlme_out_buf_t buf);
  /**
   * Sets the link status to be UP or DOWN.
   */
  int32_t (*set_link_status)(void *device, uint8_t status);
  /**
   * Configure the association context.
   * |assoc_ctx| is mutable because the underlying API does not take a const wlan_assoc_ctx_t.
   */
  int32_t (*configure_assoc)(void *device, wlan_assoc_ctx_t *assoc_ctx);
  /**
   * Clear the association context.
   */
  int32_t (*clear_assoc)(void *device, const uint8_t (*addr)[6]);
} rust_device_interface_t;

/**
 * An input buffer will always be returned to its original owner when no longer being used.
 * An input buffer is used for every buffer handed from C++ to Rust.
 */
typedef struct {
  /**
   * Returns the buffer's ownership and free it.
   */
  void (*free_buffer)(void *raw);
  /**
   * Pointer to the buffer's underlying data structure.
   */
  void *raw;
  /**
   * Pointer to the start of the buffer's data portion and its length.
   */
  uint8_t *data;
  uintptr_t len;
} mlme_in_buf_t;

typedef struct {
  /**
   * Acquire a `InBuf` with a given minimum length from the provider.
   * The provider must release the underlying buffer's ownership and transfer it to this crate.
   * The buffer will be returned via the `free_buffer` callback when it's no longer used.
   */
  mlme_in_buf_t (*get_buffer)(uintptr_t min_len);
} mlme_buffer_provider_ops_t;

/**
 * A convenient C-wrapper for read-only memory that is neither owned or managed by Rust
 */
typedef struct {
  const uint8_t *data;
  uintptr_t size;
} wlan_span_t;

/**
 * ClientConfig affects time duration used for different timeouts.
 * Originally added to more easily control behavior in tests.
 */
typedef struct {
  zx_duration_t ensure_on_channel_time;
} wlan_client_mlme_config_t;

typedef uint64_t wlan_scheduler_event_id_t;

/**
 * The power management state of a station.
 *
 * Represents the possible power states from IEEE-802.11-2016, 11.2.7.
 */
typedef struct {
  bool _0;
} wlan_power_state_t;

extern "C" wlan_mlme_handle_t *start_ap_sta(rust_device_interface_t device,
                                            mlme_buffer_provider_ops_t buf_provider,
                                            const uint8_t (*bssid)[6]);

extern "C" wlan_mlme_handle_t *start_ap_sta_for_test(rust_device_interface_t device,
                                                     mlme_buffer_provider_ops_t buf_provider,
                                                     const uint8_t (*bssid)[6]);

extern "C" void stop_and_delete_ap_sta(wlan_mlme_handle_t *sta);

extern "C" void ap_sta_queue_eth_frame_tx(wlan_mlme_handle_t *sta, wlan_span_t frame);

extern "C" void ap_mlme_advance_fake_time(wlan_mlme_handle_t *ap, int64_t nanos);

extern "C" void ap_mlme_run_until_stalled(wlan_mlme_handle_t *sta);

extern "C" wlan_mlme_handle_t *start_client_mlme(wlan_client_mlme_config_t config,
                                                 rust_device_interface_t device,
                                                 mlme_buffer_provider_ops_t buf_provider);

extern "C" wlan_mlme_handle_t *start_client_mlme_for_test(wlan_client_mlme_config_t config,
                                                          rust_device_interface_t device,
                                                          mlme_buffer_provider_ops_t buf_provider);

extern "C" void stop_and_delete_client_mlme(wlan_mlme_handle_t *mlme);

extern "C" void client_mlme_queue_eth_frame_tx(wlan_mlme_handle_t *mlme, wlan_span_t frame);

extern "C" void client_mlme_advance_fake_time(wlan_mlme_handle_t *mlme, int64_t nanos);

extern "C" void client_mlme_run_until_stalled(wlan_mlme_handle_t *mlme);

extern "C" mlme_sequence_manager_t *mlme_sequence_manager_new(void);

extern "C" void mlme_sequence_manager_delete(mlme_sequence_manager_t *mgr);

extern "C" uint32_t mlme_sequence_manager_next_sns1(mlme_sequence_manager_t *mgr,
                                                    const uint8_t (*sta_addr)[6]);

extern "C" uint32_t mlme_sequence_manager_next_sns2(mlme_sequence_manager_t *mgr,
                                                    const uint8_t (*sta_addr)[6], uint16_t tid);

#endif  // SRC_CONNECTIVITY_WLAN_LIB_MLME_RUST_C_BINDING_BINDINGS_H_
