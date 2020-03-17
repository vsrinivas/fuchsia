# Runtime Allowlist Policies
## Overview
This directory contains sets of allowlists that are read by the `appmgr` to
limit which components can access certain services and features at runtime. This
runtime enforcement enables the appmgr to block the launch of unauthorized
components from requesting the `RootResource` service or the
`deprecated_ambient_replace_as_executable` feature.

## Policy Variants
There are three variants of allowlists held in the policy directory:
1. allowlist_user: what the product minimally needs to work.
2. allowlist_userdebug: additional components required for debugging and
   limited integration testing.
3. allowlist_eng: additional components to support all testing, and any
   additional development or components not used in user builds.
