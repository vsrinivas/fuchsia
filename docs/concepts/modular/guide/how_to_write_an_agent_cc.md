# How-To: Write an Agent in C++

## Overview

An Agent is a component that runs without any direct user interaction. The lifetime of an Agent
component instance is bounded by its session.  It can be shared by mods across many stories. In
addition to the capabilities provided to all modular components via
`fuchsia::modular::ComponentContext`, an Agent is given additional capabilities via
`fuchsia::modular::AgentContext` as an incoming service.

Agents must expose the `fuchsia::modular::Agent` service to receive new connections and provide
services. An Agent component may implement the `fuchsia::modular::Lifecycle` service to receive termination signals and voluntarily exit.

## SimpleAgent

### fuchsia::modular::Agent Initialization

The first step to writing an Agent is setting up the scaffolding using the `modular::Agent` utility
class.

```c++
#include <lib/modular/cpp/agent.h>

int main(int /*argc*/, const char** /*argv*/) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::Create();
  modular::Agent agent(context->outgoing(), [&loop] { loop.Quit(); });
  loop.Run();
  return 0;
}
```

The `modular::Agent` utility above implements and exposes `fuchsia::modular::Agent` and
`fuchsia::modular::Lifecycle` services. Additionally,

#### `fuchsia::modular::AgentContext`

`fuchsia::modular::AgentContext` is a protocol that is exposed to all Agent components.
For example, it allows agents to schedule `Task`s that will be executed at
specific intervals.

`fuchsia::modular::AgentContext` also gives `fuchsia::modular::Agent`s access to
`fuchsia::modular::ComponentContext` which is a protocol that is exposed to all
Peridot components (i.e. `fuchsia::modular::Agent` and `Module`).
For example, `fuchsia::modular::ComponentContext` provides access to `Ledger`,
Peridot's cross-device storage solution.

### Advertising the `Simple` Protocol

In order for the `SimpleAgent` to advertise the `Simple` protocol to other modular components,
it needs to expose it as an agent service. `modular::Agent::AddService<>()` provides a way to do
this:

```c++
  class SimpleImpl : Simple {
    SimpleImpl();
    ~SimpleImpl();

    std::string message_queue_token() const { return token_; }

  private:
    // The current message queue token.
    std::string token_;
  };

  int main(int /*argc*/, const char** /*argv*/) {
    ...
    modular::Agent agent(context->outgoing(), [&loop] { loop.Quit(); });

    SimpleImpl simple_impl;
    fidl::BindingSet<Simple> simple_bindings;

    agent.AddService<Simple>(simple_bindings.GetHandler(&simple_impl));
    ...
  }
```

In the code above, `SimpleAgent` adds the `Simple` service as an agent service. Now, when a
component connects to the `SimpleAgent`, it will be able to connect to the `Simple` interface and
call methods on it. Those method calls will be delegated to the `simple_impl` object.

## Connecting to SimpleAgent

To connect to the `SimpleAgent` from a different component, a service mapping must be added to the
Modular [config](config.md) for your product:

```json
  agent_service_index = [
    {
      "service_name": "Simple",
      "agent_url": "fuchisa-pkg://url/to/your/agent"
    }
  ]
```

Then, in the component's implementation (i.e., `main.cc`):

```c++
auto sys_component_context = sys::ComponentContext::Create();
SimplePtr simple = sys_component_context->svc()->Connect<Simple>();
```

When your component asks for the `Simple` service, the `agent_service_index` is consulted.
The agent listed there is launched and is asked to provide the `Simple` service.

See the [SimpleModule](how_to_write_a_module_cc.md) guide for a more in-depth example.
