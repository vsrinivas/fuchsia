# How-To: Write an Agent in C++

## Overview

An `Agent` is a Peridot component that runs without any direct user interaction.
The lifetime of a single Agent instance is bounded by its session.  It can be
shared by mods across many stories. In addition to the capabilities provided to all
Peridot components via `ComponentContext`, an `Agent` is given additional
capabilities via its `AgentContext`.

See [the simple directory](../simple/) for a complete implementation of
the `Agent` described here.

## SimpleAgent

`SimpleAgent` is an `Agent` that periodically writes a simple message to
a `MessageQueue` (a common communication channel).

`SimpleAgent` implements the `Simple` FIDL interface which exposes the
ability to control which `MessageQueue` the messages will be sent to.

```
library simple;

[Discoverable]
interface Simple {
  // Provides the Simple interface with a message queue to which
  // messages will be written periodically.
  1: SetMessageQueue(string queue_token);
};
```

### Agent Initialization

The first step to writing an `Agent` is implementing the initializer:

```c++
#include "lib/app_driver/cpp/agent_driver.h"

class SimpleAgent {
  public:
    SimpleAgent(AgentHost* const agent_host) {
      ...
    }
};
```

The `AgentHost` parameter provides the `Agent` with an `ApplicationContext`
and an `AgentContext`.

#### `ApplicationContext`

`ApplicationContext` gives the `Agent` access to services provided to it by
other system components via `incoming_services`. It also allows the `Agent`
to provide services to other components via `outgoing_services`.

#### `AgentContext`

`AgentContext` is an interface that is exposed to all `Agents`.
For example, it allows agents to schedule `Task`s that will be executed at
specific intervals.

`AgentContext` also gives `Agent`s access to `ComponentContext` which is an
interface that is exposed to all Peridot components (i.e. `Agent` and `Module`).
For example, `ComponentContext` provides access to `Ledger`, Peridot's cross-device
storage solution.

### Advertising the `Simple` Interface

In order for the `SimpleAgent` to advertise the `Simple` interface to the system
it needs to provide it in its `outgoing_services`.

```c++
  SimpleAgent(AgentHost* const agent_host) {
    services_.AddService<Simple>(
        [this](fidl::InterfaceRequest<Simple> request) {
          simple_impl_->Connect(std::move(request));
        });
  }

  void Connect(
      fidl::InterfaceRequest<component::ServiceProvider> outgoing_services) {
    services_.AddBinding(std::move(outgoing_services));
  }

 private:
  // The services namespace that the `Simple` service is added to.
  component::ServiceNamespace services_;

  // The implementation of the Simple service.
  std::unique_ptr<SimpleImpl> simple_impl_;
```

In the initializer above, `SimpleAgent` adds the `Simple` service to its `ServiceNamespace`.
 When `SimpleAgent` receives a `Connect` call, it needs to bind the handle held by `request`
 to the concrete implementation in `services_`. It does this by calling
`AddBinding(std::move(outgoing_services))`.

Now, when a component connects to the `SimpleAgent`, it will be able to connect
to the `SimpleInterface` and call methods on it. Those method calls will be
delegated to the `simple_impl_` per the callback given to `AddService()`.

```c++
class SimpleImpl : Simple {
  SimpleImpl();
  ~SimpleImpl();

  void Connect(fidl::InterfaceRequest<Simple> request);

  std::string message_queue_token() const { return token_; }

 private:
  // |Simple| interface method.
  void SetMessageQueue(fidl::StringPtr queue_token);

  // The bindings to the Simple service.
  fidl::BindingSet<Simple> bindings_;

  // The current message queue token.
  std::string token_;
};
```

The `SimpleImpl` could be part of the `SimpleAgent` class, but it's good practice
to separate out the implementation of an `Agent` from the implementation(s) of the
interface(s) it provides.

## Running the Agent

```c++
int main(int /*argc*/, const char** /*argv*/) {
  fsl::MessageLoop loop;
  auto app_context = component::ApplicationContext::CreateFromStartupInfo();
  modular::AgentDriver<simple::SimpleAgent> driver(
      app_context.get(), [&loop] { loop.QuitNow(); });
  loop.Run();
  return 0;
}
```

`AgentDriver` is a helper class that helps manage the `Agent` lifecycle. Here
it is given a newly created `ApplicationContext` and a callback that will be
executed when the `Agent` exits. `AgentDriver` requires `SimpleAgent` to
implement three methods, one of which is `Connect` which was shown above.

The other two are:

```c++
void RunTask(const fidl::StringPtr& task_id,
             const std::function<void()>& done) {
  done();
}

void Terminate(const std::function<void()>& done) { done(); }
```

`RunTask` is called when a task, scheduled through `AgentContext`'s `ScheduleTask`,
is triggered.

`Terminate` is called when the framework requests `Agent` to
exit gracefully.

## Connecting to SimpleAgent

To connect to the `SimpleAgent` from a different component:

```c++
// The agent is guaranteed to stay alive as long as |agent_controller| stays in scope.
modular::AgentControllerPtr agent_controller;
component::ServiceProviderPtr agent_services;
SimpleServicePtr agent_service;
component_context->ConnectToAgent(agent_url,
                                  agent_services.NewRequest(),
                                  agent_controller.NewRequest());
ConnectToService(agent_services.get(), agent_service.NewRequest());
agent_service->SetMessageQueue(...);
```

Here the component context is asked to connect to the Agent at `agent_url`, and is
given a request for the services that the `SimpleAgent` will provide via `agent_services`,
and a controller for the `Agent` via `agent_controller`.

Then the client connects to the `Simple` interface by invoking `ConnectToService` with
a request for a new `SimpleServicePtr`. This interface pointer can be used immediately
to provide the agent with the token for the `MessageQueue` to send messages to.

See the [SimpleModule](how_to_write_a_mod.md) guide for a more in-depth example.
