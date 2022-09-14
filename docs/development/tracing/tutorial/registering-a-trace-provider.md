# Registering a trace provider

Trace providers must register with the trace manager in order
to participate in tracing. Drivers don't have to register as a trace provider
since the devhost process does it through `libdriver.so`.

To register a trace provider, you must do the following:

Note: For more information on the Fuchsia tracing system, see
[Fuchsia tracing system](/docs/concepts/kernel/tracing-system.md).

* [Register with the trace manager](#register-with-the-trace-manager)
* [Give the trace manager component access](#give-trace-manager-component-access)

## Register with the trace manager {#register-with-the-trace-manager}

To register as a trace provider, you can use the `libtrace-provider` library
to provide an asynchronous loop in your component's code.

Note: For more information on tracing libraries, see
[Tracing libraries](/docs/reference/tracing/libraries.md).

For example:

* {C++}

  Note: This example uses `fdio` to set up the FIDL channel with Trace Manager. For
  more information, see
  [`fdio`](/docs/concepts/filesystems/life_of_an_open.md#fdio).

  ```cpp
  #include <lib/async-loop/cpp/loop.h>
  #include <lib/async-loop/default.h>
  #include <lib/trace-provider/provider.h>
  // further includes

  int main(int argc, const char** argv) {
    // process argv

    async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
    trace::TraceProviderWithFdio trace_provider(
        loop.dispatcher(), "my_trace_provider");

    // further setup

    loop.Run();
    return 0;
  }
  ```

* {C }

  ```c
  #include <lib/async-loop/cpp/loop.h>
  #include <lib/async-loop/default.h>
  #include <lib/trace-provider/provider.h>

  int main(int argc, char** argv) {
    zx_status_t status;
    async_loop_t* loop;
    trace_provider_t* trace_provider;

    // Create a message loop.
    status = async_loop_create(&kAsyncLoopConfigNoAttachToCurrentThread, &loop);
    if (status != ZX_OK) exit(1);

    // Start a thread for the loop to run on.
    // Alternatively, use async_loop_run() to run on the current thread.
    status = async_loop_start_thread(loop, "loop", NULL);
    if (status != ZX_OK) exit(1);

    // Create the trace provider.
    async_dispatcher_t* dispatcher = async_loop_get_dispatcher(loop);
    trace_provider = trace_provider_create(dispatcher);
    if (!trace_provider) exit(1);

    // Do something...

    // Tear down.
    trace_provider_destroy(trace_provider);
    async_loop_shutdown(loop);
    return 0;
  }
  ```

## Give the trace manager component access {#give-trace-manager-component-access}

In the component manifest file (a `.cml` file) of your component, you must
specify that it needs to communicate with the Fuchsia trace manager.

Note: For information on component manifests, see
[Component Manifests](/docs/concepts/components/v2/component_manifests.md).

To give the trace manager component access, specify
the protocol `fuchsia.tracing.provider.Registry` as a `use` declaration. For
example:

* {.cml file}

  ```json5
  {
      program: {
          runner: "elf",
          binary: "bin/app",
      },
      use: [
          {
              protocol: [
                  "fuchsia.tracing.provider.Registry",
              ],
          },
      ],
  }
  ```

If your component is in core, you should route the capability from
trace\_manager to your component directly in `core.cml`. Otherwise, you
should do so in your component's core\_shard.cml.

* {.cml file}

  ```json5
  // component.core_shard.cml
  {
  offer: [
        {
            protocol: "fuchsia.tracing.provider.Registry",
            from: "#trace_manager",
            to: "#your_component",

            // Allows manifest validation to succeed if tracing is not compiled
            // into the product
            source_availability: "unknown",
        },
  ]
  }
  ```

Once you have registered your component as a trace provider, you can enable
tracing in your code. For more information, see
[Adding tracing in your code](/docs/development/tracing/tutorial/adding-tracing-in-code.md).
