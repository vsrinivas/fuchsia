// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

// The definitions provided in this file are intended to make it easier to override the return
// value from function calls at runtime to simplify the process of writing tests for handling
// unexpected conditions.
//
// To use, wrap a function call with BRCMF_CALL(fn, args);
// This will allow tests to replace the function call with a static value. If not overridden, the
// function will still be called with the specified arguments.
//
// To override the result in a function call, create a declaration in the global namespace and
// specify the override value:
//   BRCMF_DECLARE_OVERRIDE(fn, value);
//
// Finally, if needed the value of the override can be changed at runtime:
//   BRCMF_SET_VALUE(fn, new_value);

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_CALLS_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_CALLS_H_

// Invoke a function call with the ability to override during testing. The syntax is simply:
// BRCMF_CALL(fn, args);
#define BRCMF_CALL(fn, ...)                                         \
  ({                                                                \
    decltype(fn(__VA_ARGS__)) tmp_result;                           \
    extern __WEAK decltype(fn(__VA_ARGS__)) BRCMF_CALL_RESULT_##fn; \
    if (&BRCMF_CALL_RESULT_##fn != nullptr) {                       \
      tmp_result = BRCMF_CALL_RESULT_##fn;                          \
    } else {                                                        \
      tmp_result = fn(__VA_ARGS__);                                 \
    }                                                               \
    tmp_result;                                                     \
  })

// Create an override. Note that once an override value has been created, everywhere that the
// function is called with BRCMF_CALL, the function will no longer be called and the value will
// simply be used instead.
#define BRCMF_DECLARE_OVERRIDE(fn, default) decltype(default) BRCMF_CALL_RESULT_##fn = default

// Change the override value dynamically.
#define BRCMF_SET_VALUE(fn, value)  \
  do {                              \
    BRCMF_CALL_RESULT_##fn = value; \
  } while (0)

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_CALLS_H_
