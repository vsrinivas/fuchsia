// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub const SELECTOR_FORMAT_HELP: &str =
    "Selector format: <component moniker>:(in|out|exposed)[:<service name>].
Wildcards may be used anywhere in the selector.

Example: 'remote-control:out:*' would return all services in 'out' for
the component remote-control.

Note that moniker wildcards are not recursive: 'a/*/c' will only match
components named 'c' running in some sub-realm directly below 'a', and
no further.";

pub const COMPONENT_LIST_HELP: &str = "only format: 'cmx' / 'cml' / 'running' / 'stopped'.
Default option is displaying all components if no argument is entered.";

pub const COMPONENT_SHOW_HELP: &str = "Filter accepts a partial component name or url.

Example:
'appmgr', 'appmgr.cm', 'fuchsia-pkg://fuchsia.com/appmgr#meta/appmgr.cm'
will all return information about the appmgr component.";

pub const COMPONENT_BIND_HELP: &str = "Moniker format: {name}/{name}
Example: 'core/brightness_manager'";
