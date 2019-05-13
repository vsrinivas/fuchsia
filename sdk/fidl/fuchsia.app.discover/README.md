# Discover FIDL API

Experimental.

## DiscoverManager

Sessionmgr routes modules' requests for ModuleOutputWriter to discovermgr via
the DiscoverRegistry protocol. While doing so, it provides the discovermgr with
a unique identifier for the module.

This interface is meant to be used only by sessionmgr.

## ModuleOutputWriter

Provided to modules to write to output parameters. Entities written to these outputs
will be used in suggested action inputs by the discovermgr.

This interface is meant to be used by modules.
