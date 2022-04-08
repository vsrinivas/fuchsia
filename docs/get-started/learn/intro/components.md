# Component-based software

<<../../_common/intro/_components_intro.md>>

<<../../_common/intro/_components_manager.md>>

<<../../_common/intro/_components_capabilities.md>>

<<../../_common/intro/_components_organization.md>>

## Exercise: Components

In this exercise, you'll explore the component instance tree and look in detail
at capability routing in action using some core system components.

<<../_common/_start_femu.md>>

### Explore system components

Open another terminal window and use the `component list` command to dump the
system's component tree:


```posix-terminal
ffx component list
```

You should see output similar to the (truncated) list below:

```none {:.devsite-disable-click-to-copy}
/
/bootstrap
/bootstrap/archivist
/bootstrap/base_resolver
/bootstrap/console
/bootstrap/console-launcher
/bootstrap/decompressor
/bootstrap/device_name_provider
/bootstrap/driver_manager
/bootstrap/fshost
/bootstrap/miscsvc
/bootstrap/netsvc
/bootstrap/power_manager
/bootstrap/ptysvc
/bootstrap/pwrbtn-monitor
/bootstrap/shutdown_shim
/bootstrap/svchost
/bootstrap/sysinfo
/bootstrap/virtual_console
/core
/core/activity
/core/appmgr
...
/core/debug_serial
/core/detect
/core/font_provider
/core/log-stats
/core/remote-control
/core/remote-diagnostics-bridge
/core/sampler
/core/system-update-committer
/core/temperature-logger
/core/test_manager
/core/full-resolver
/startup
```

This list represents the **component instance tree**, with organizational
components like `bootstrap`, `core`, and `startup` forming sub-trees
underneath the root.

The `component show` command provides more details about each component.

Use this command to see the details of `fshost` — the Fuchsia filesystem manager:

```posix-terminal
ffx component show fshost
```


The command outputs the following report:


```none {:.devsite-disable-click-to-copy}
               Moniker: /bootstrap/fshost
                   URL: fuchsia-boot:///#meta/fshost.cm
                  Type: CML static component
       Component State: Resolved
 Incoming Capabilities: boot
                        dev
                        fuchsia.boot.Arguments
                        fuchsia.boot.Items
                        fuchsia.cobalt.LoggerFactory
                        fuchsia.device.manager.Administrator
                        fuchsia.feedback.CrashReporter
                        fuchsia.logger.LogSink
                        fuchsia.process.Launcher
                        fuchsia.tracing.provider.Registry
                        svc_blobfs
  Exposed Capabilities: bin
                        blob
                        build-info
                        config-data
                        diagnostics
                        durable
                        factory
                        fuchsia.fshost.Admin
                        fuchsia.fshost.BlockWatcher
                        fuchsia.fshost.Loader
                        fuchsia.update.verify.BlobfsVerifier
                        install
                        minfs
                        mnt
                        pkgfs
                        pkgfs-delayed
                        pkgfs-packages-delayed
                        root-ssl-certificates
                        system
                        system-delayed
                        tmp
       Execution State: Running
          Start reason: '/bootstrap/base_resolver' requested capability 'pkgfs-packages-delayed'
           Running for: 1807734933 ticks
                Job ID: 2546
            Process ID: 2716
 Outgoing Capabilities: delayed
                        diagnostics
                        fs
                        fuchsia.fshost.Admin
                        fuchsia.fshost.BlockWatcher
                        fuchsia.fshost.Loader
                        fuchsia.update.verify.BlobfsVerifier
```

Notice a few of the details reported here:

1.  A unique identifier for the component instance (called a **moniker**).
1.  The package URL where this component was loaded from.
1.  The execution state of the component.
1.  The current job/process ID where the instance is running.
1.  A set of requested and exposed capabilities for the component.


### Trace a capability route

In the previous output, there are three capability groups listed:

* **Incoming Capabilities**: Capabilities that the component declares with
  `use`. These are provided to the component through its **namespace**.
* **Outgoing Capabilities**: Capabilities the component has published to its
  **outgoing directory**.
* **Exposed Capabilities**: Capabilities the component declares with
  `expose`. These are the component's **exposed services**.

One of the capabilities exposed by `fshost` to its parent **realm** is
[fuchsia.fshost.Admin](https://fuchsia.dev/reference/fidl/fuchsia.fshost#Admin).
This enables other components to access directories in the registered
filesystems on the device.

Use the `component select` command determine how many components use this
capability (i.e., have it listed under **Incoming Capabilities**):

```posix-terminal
ffx component select moniker '*/*:in:fuchsia.fshost.Admin'
```

The command lists all the matching components:


```none {:.devsite-disable-click-to-copy}
bootstrap/driver_manager
|
--in
   |
   --fuchsia.fshost.Admin
```


Looks like this protocol is consumed by the `driver_manager` component. The
common ancestor between these components is `bootstrap`, which handles the
routing of this capability to the necessary children.


<aside class="key-point">
  <b>Extra credit</b>
  <p>A lot of components use the <code>fuchsia.logger.LogSink</code> capability,
  which is needed to read the system logs. You can list them using the same
  <code>component select</code> search for incoming capabilities.</p>
  <p>Can you find which component exposes this capability?</p>
</aside>
