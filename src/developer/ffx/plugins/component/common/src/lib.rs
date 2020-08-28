// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub const SELECTOR_FORMAT_HELP: &str = "Selector format: <component moniker>:(in|out|exposed)[:<service name>]. Wildcards may be used anywhere in the selector.
Example: 'remote-control:out:*' would return all services in 'out' for the component remote-control.

Note that moniker wildcards are not recursive: 'a/*/c' will only match components named 'c' running in some sub-realm directly below 'a', and no further.";
