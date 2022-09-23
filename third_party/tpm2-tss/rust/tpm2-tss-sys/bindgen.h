// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This header provides the list of headers from the tpm2-tss library
// that we wish to consume in the generation of the tpm2-tss-sys
// library. This is a subset of the headers that are required by
// the tpm-agent component.

// TPM Command Marshalling Support.
#include "tss2_mu.h"
// Fuchsia TCTI Interface.
#include "tss2_tcti_fuchsia.h"
// TSS Enhanced System Interface.
#include "tss2_esys.h"
