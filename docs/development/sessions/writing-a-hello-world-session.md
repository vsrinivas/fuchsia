# Writing a "Hello World" session {#writing-a-hello-world-session}

Sessions are regular components that the `session_manager` can launch at
startup. This means that creating a session component follows all of the same
steps as creating any other component. This document discusses creating an
example session that launches at startup and prints "Hello World!" to the system
log.

## Create the directory structure {#create-the-directory-structure}

Components require a specific directory structure. The `fx` tool provides a
generator that creates this structure for you. It takes the name of the
component and the language you want to use as arguments. For example, this
component is called `hello-world-session` and is written in Rust.

Run the following command to create the directory structure for this example:

```posix-terminal
fx create component --path hello-world-session --lang rust
```

This command creates the following directory structure with a template for a
component offering a service:

```none
hello-world-session
  |- meta
  |   |- hello-world-session.cml
  |
  |- src
  |   |- main.rs
  |
  |- BUILD.gn
```

## Create a component manifest {#create-a-component-manifest}

The component manifest file (`hello-world-session.cml`) declares the component
to Fuchsia. For this example, the default manifest is sufficient but take a
moment to explore the following lines of code from `hello-world-session.cml`:

1. The file starts by including other cml files if needed.

   ```json5
   {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/session/examples/hello-world-session/meta/hello-world-session.cml" region_tag="include_block" adjust_indentation="auto" %}
   ```

   This `include` key lets the session component use the
   `fuchsia.logger.LogSink` capability so that it can print to the system log.

1. Next is the `program` block.

   ```json5
   {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/session/examples/hello-world-session/meta/hello-world-session.cml" region_tag="program_block" adjust_indentation="auto" %}
   ```

   The `program` block tells the `component_manager` where the binary for the
   session component can be found. The `runner` key tells the `component_manager`
   that is should run the component binary using the ELF runner.

1. Finally the component manifest describes additional capabilities that the
   component can `use`, `offer`, or `expose`.

   ```json5
   {% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/session/examples/hello-world-session/meta/hello-world-session.cml" region_tag="capabilities_block" adjust_indentation="auto" %}
   ```

## Create a session config {#create-a-session-config}

`session_manager` needs to know to which session component to launch at startup.
To do this create a session config JSON file in the `meta` directory that
contains the URL of the session component.

Component URLs follow the format:

<pre><code>fuchsia-pkg://fuchsia.com/<var>package_name</var>#meta/<var>your_session.cm</var></code></pre>

Notice that the path points to a `.cm` file. `.cm` files are compiled versions
of `.cml` files that are generated when `fx build` is run. So, in this case, the
component URL is:

```none
fuchsia-pkg://fuchsia.com/hello-world-session#meta/hello-world-session.cm
```

The whole session config file looks like this:

```json
{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/session/examples/hello-world-session/meta/hello-world-session-config.json" adjust_indentation="auto" %}
```

## Writing a session in Rust {#writing-a-session-in-rust}

Now you can write the implementation for the session component. Inside the
`src/main.rs` file that was generated there is a lot of code that isn't needed
for this example.

Replace the contents of `src/main.rs` with the following code:

```rust
{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/session/examples/hello-world-session/src/main.rs" region_tag="main" adjust_indentation="auto" %}
```

This code initializes the system log and then prints "Hello World!".
`tracing::info!` is a macro that prints to the log with a level of `info`.
There are similar macros for `error` and `warn`.

## Writing the `BUILD.gn` {#writing-the-build-gn}

The last file to modify is the `BUILD.gn`. This tells the compiler how to build
the the session component.

### Imports {#imports}

The file starts by importing GN templates that are used in this `BUILD.gn`. To
build a session component, import the `session_config.gni`:

```gn
{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/session/examples/hello-world-session/BUILD.gn" region_tag="session_import" adjust_indentation="auto" %}
```

### Session config {#session-config}

The added import statement gives the `BUILD.gn` access to the `session_config`
command. This command tells the build where to find the `session_config.json`
for this component.

Add the `session_config` to the `BUILD.gn` file:

```gn
{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/session/examples/hello-world-session/BUILD.gn" region_tag="session_config" adjust_indentation="auto" %}
```

### Rust binary {#rust-binary}

The next section describes the actual Rust binary. It tells the compiler what
the name of the binary should be, that it includes unit tests, what dependencies
it has, and where it's source is located. For this example, the default set of
dependencies are sufficient:

```gn
{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/session/examples/hello-world-session/BUILD.gn" region_tag="session_binary" adjust_indentation="auto" %}
```

The `fuchsia_component()` and `fuchsia_package()` templates tell Fuchsia more
about the component including what it is called, where to find the manifest,
and what dependencies the package and component have:

```gn
{% includecode gerrit_repo="fuchsia/fuchsia" gerrit_path="src/session/examples/hello-world-session/BUILD.gn" region_tag="component_package" adjust_indentation="auto" %}
```

## Building the session {#building-the-session}

To build the session `fx set` must first be used to configure the build so that
`session_manager`, your session component, and the session config are included
in the base package set. This is done using the `--with-base` flag.

```posix-terminal
fx set core.x64 --with-base //src/session \
    --with-base {{ '<var label="session path">//path/to/your/session</var>' }} \
    --with-base {{ '<var label="config path">//path/to/your/session:your_session_config</var>' }}
```

If you are using the example project from the `//src/session/examples` directory,
the `fx set` command would be:

```posix-terminal
fx set core.x64 --with-base //src/session \
    --with-base //src/session/examples/hello-world-session \
    --with-base //src/session/examples/hello-world-session:hello-world-session-config.json
```

Once that's done and built `session_manager` should automatically start your
session on boot. You should see the "Hello" message in the system log.

```none {:.devsite-disable-click-to-copy}
$ fx log --only hello
[session_manager] INFO: Launching session: fuchsia-pkg://fuchsia.com/hello-world-session#meta/hello-world-session.cm
[hello_world_session] INFO: Hello World!
```
