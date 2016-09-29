// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// For each function in the vDSO ABI, define a symbol in the linker script
// pointing to its address.  The vDSO is loaded immediately after the
// userboot DSO image's last page, which is marked by the CODE_END symbol.
// So these symbols tell the linker where each vDSO function will be found
// at runtime.  The userboot code uses normal calls to these, declared as
// have hidden visibility so they won't generate PLT entries.  This results
// in the userboot binary having simple PC-relative calls to addresses
// outside its own image, to where the vDSO will be found at runtime.
#define FUNCTION(name, address, size) \
    PROVIDE_HIDDEN(name = CODE_END + address);

#define WEAK_FUNCTION(name, address, size) FUNCTION(name, address, size)
