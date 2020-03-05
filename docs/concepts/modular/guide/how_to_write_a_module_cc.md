# How-To: Write a Module in C++

> DEPRECATION WARNING: The Modular framework is being deprecated in favor of
> the [Session Framework](/docs/concepts/session/introduction.md).

## Overview

A `Module` is a UI component that can participate in a [Story](link to story doc),
potentially composed of many different `Module`s. A `Module`'s lifecycle is tightly
bound to the story to which it was added. In addition to the capabilities
provided to all Peridot components via `fuchsia::modular::ComponentContext`, a `Module` is given
additional capabilities via its `fuchsia::modular::ModuleContext`.

## `SimpleMod`

### Mod Initialization

The first step to writing a `Module` is implementing the initializer.

```c++
#include <lib/sys/cpp/component_context.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <ui/cpp/fidl.h>

#include "src/modular/lib/app_driver/cpp/module_driver.h"

namespace simple {

class SimpleModule : fuchsia::ui::app::ViewProvider {
 public:
	SimpleModule(
			modular::ModuleHost* module_host,
			fidl::InterfaceRequest<fuchsia::ui::app::ViewProvider> view_provider_request)
			: view_provider_binding_(this) {
		view_provider_binding_.Bind(std::move(view_provider_request));
}

 private:
	modular::ModuleHost* module_host_;
	fidl::Binding<fuchsia::ui::app::ViewProvider> view_provider_binding_;
	std::set<std::unique_ptr<SimpleView>> views_;
};

}  // namespace simple
```

The `ModuleHost` provides `SimpleModule` with its `StartupContext` and
`fuchsia::modular::ModuleContext`.

The `ViewProvider` request allows the system to connect to `SimpleModule`'s view.
TODO: Update guide to explain view connections.

### Connecting to `SimpleAgent`

In order to provide `SimpleAgent` with a message queue `SimpleModule` first
needs to connect to the agent. See the instructions in (how to write an agent)[how_to_write_an_agent_cc.md]

### Running the Module

```c++
int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::Create();
  modular::ModuleDriver<simple::SimpleModule> driver(context.get(),
                                                     [&loop] { loop.Quit(); });
  loop.Run();
  return 0;
}
```

`ModuleDriver` is a helper class that manages the `Module`'s lifecyle. Here it is
given a newly created `StartupContext` and a callback that will be executed
when the `Module` exits. `ModuleDriver` requires `SimpleModule` to implement the
constructor shown above, as well as a `Terminate`:

```c++
void Terminate(fit::function<void()> done);
```

The module is responsible for calling `done` once its shutdown sequence is complete.

