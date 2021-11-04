// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// DI server that handles client requests and manages BR/EDR advertisements.
mod server;
pub use server::DeviceIdServer;

/// Local type for the BR/EDR Device Identification service record.
mod service_record;

/// Local type for a FIDL client's request.
mod token;
