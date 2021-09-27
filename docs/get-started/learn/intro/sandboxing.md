# Software isolation model

In this section, you will learn how the Zircon kernel objects enable Fuchsia to
follow the **principle of least privilege**, isolating processes and granting
them only the capabilities they require.

## Sandboxing

When a new process is created, it has no capabilities. The process relies
entirely on its creator to provide capabilities through the set of
[handles][glossary.handle] passed to it. One might also say that an empty
process has no **ambient authority**.

Because of this, processes are usually created with some initial resources
and capabilities. The `fuchsia.process.Launcher` protocol provides the
low-level interface to create new processes on the system from an executable
and a set of kernel object handles. Most software uses the component framework,
which simplifies the work of setting up a new process to execute some code with
a standard set of initial capabilities. You will explore components in more
detail later on.


<aside class="key-point">
  <b>Handles have rights</b>
  <p>Previously you saw that handles are unique references to objects in the
  kernel. Each handle also contains the rights the handle has to perform
  certain actions, such as <code>ZX_RIGHT_READ</code>,
  <code>ZX_RIGHT_WRITE</code>, or <code>ZX_RIGHT_EXECUTE</code>.</p>

  <p>During process creation, the rights of each handle can be reduced to suit
  the requirements (and restrictions) of the new process using the
  <code>zx_handle_replace()</code> or <code>zx_handle_duplicate()</code>
   operations.

  <p>The creating process can then write the new handles across the IPC channel
  to set the initial capabilities of the new process.</p>
</aside>


Some initial handles given to a process are directories that the process mounts
into its **namespace**.

## Namespaces

The namespace of a process contains its private view of the world, and controls
how much of the Fuchsia system the process can influence. This effectively
defines the rules of the sandbox in which that process runs.

Namespaces are populated with various resource objects, including:

* **Files**: Objects which contain binary data.
* **Directories**: Objects which contain other objects.
* **Sockets**: Objects which establish connections when opened, like named
  pipes.
* **Protocols and services**: Objects which provide structured services when
  opened.
* **Devices**: Objects which provide access to hardware resources.

The ​​creator of the process populates the contents of a namespace based on the
set of required capabilities. A process cannot add objects to its own
namespace, as this would essentially amount to that process self-granting the
capabilities to access those objects.

<aside class="key-point">
  <b>No global filesystem</b>
  <p>In many ways, the contents of a namespace resemble the filesystem resources
  exposed by POSIX-oriented operating systems where "everything is a file".
  However, there are some very important differences to keep in mind.<p>

  <p>Namespaces are defined per-process and unlike other operating systems,
  Fuchsia does not have a "root filesystem". Instead, the path location
  <code>/</code> refers to the root of its private namespace. This also
  means Fuchsia does not have a concept of chroot environments, since every
  process effectively has its own private "root".

  <p>This also affects directory traversal, and how filesystem servers resolve
  paths containing <code>../.</code> For more details, see
  <a href="/docs/concepts/filesystems/dotdot">dot-dot considered harmful</a>.<p>
</aside>

## Exercise: Namespaces

In this exercise, you'll explore the contents of a component's namespace in
more detail using the shell.

<<../_common/_start_femu.md>>

### Find a component in the hub

Fuchsia provides the [Hub](/docs/concepts/components/v2/hub.md) as a
diagnostic interface to obtain information about component instances running
on the system. You can explore the components and their namespaces using the
hub's directory structure.


<aside class="key-point">
The contents of the hub are organized according to the hierarchy of
{{ widgets.glossary_simple ('realm', 'component realms') }}in the system.
You'll explore more about what this structure means shortly.
</aside>


From the device shell prompt, enter the `ls` command to list the components of
the `core` realm under `/hub-v2/children/core/children`:

```posix-terminal
ls /hub-v2/children/core/children
```

```none {:.devsite-disable-click-to-copy}
activity
appmgr
brightness_manager
bt-avrcp
build-info
...
```

This is a list of many of the core Fuchsia system components. To see
more details about a specific component, list its directory contents.

Try this for the `http-client` component:

```posix-terminal
ls /hub-v2/children/core/children/http-client
```

```none {:.devsite-disable-click-to-copy}
children
component_type
debug
deleting
exec
id
resolved
url
```

### Explore the namespace and outgoing directory

You'll find a running component's **namespace** under the `exec/in` path inside
the hub.

```posix-terminal
ls /hub-v2/children/core/children/http-client/exec/in
```

```none {:.devsite-disable-click-to-copy}
config
pkg
svc
```

Here are some quick highlights of each element:

*   `config/`: configuration data for the component
*   `pkg/`: the contents of the component's package
*   `svc/`: system services available to the component

List the contents of the incoming `svc/` directory. This
directory contains
[service nodes](https://fuchsia.dev/reference/fidl/fuchsia.io#NodeInfo)
representing the system services provided to this component.

```posix-terminal
ls /hub-v2/children/core/children/http-client/exec/in/svc
```

```none {:.devsite-disable-click-to-copy}
fuchsia.logger.LogSink
fuchsia.net.name.Lookup
fuchsia.posix.socket.Provider
```

Each of these services is accessible over a well-known protocol defined by a
[Fuchsia Interface Definition Language (FIDL)][glossary.FIDL] interface.
Components provide system services through their **outgoing directory**, which
is mapped to the `exec/out` path inside the hub.

List the contents of the outgoing `svc/` directory to see the system services
this component provides.

```posix-terminal
ls /hub-v2/children/core/children/http-client/exec/out/svc
```

```none {:.devsite-disable-click-to-copy}
fuchsia.net.http.Loader
```

We'll explore FIDL protocols and how to access various services in more detail
later on.

<aside class="key-point">
  <b>Extra Credit</b>
  <p>Take a look at the other directory entries in the hub and see what else
  you can discover!</p>
</aside>

[glossary.handle]: /docs/glossary/README.md#handle