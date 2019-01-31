## Agents

An `Agent` is a singleton-per-session component which runs outside of
the scope of a Story without any graphical UI.

Agents can schedule tasks (i.e. they can register to be woken up by the
framework to perform work), and provide services to other modular components.

Any modular component can connect to an agent and access its services (including
modules, shells, and other agents).

### Environment

An agent is given access to two services provided by the modular framework in
its incoming namespace:

*   `fuchsia.modular.ComponentContext` which gives the agent access to
    functionality which is shared across components run under the modular
    framework (e.g. modules, shells, agents).
*   `fuchsia.modular.AgentContext` which gives agents access to agent specific
    functionality, like creating entity references and scheduling tasks.

An agent is expected to provide two services to the modular framework in its
outgoing namespace:

*   `fuchsia.modular.Agent` which allows the framework to forward connection
    requests from other components and tell the agent to run tasks.
*   `fuchsia.modular.Lifecycle` which allows the framework to signal the agent
    to terminate gracefully.

The aforementioned services enable communication between agents and the modular
framework, but agents can also expose custom FIDL services to components. For a
more detailed explanation of the mechanism which enables this service exchange
see Communication Mechanisms below.

### Lifecycle

For most agents, when a component connects to an agent the framework will give
the component an `AgentController`. When the connecting component drops the
`AgentController` connection, and there are no outstanding connections, the
agent will be killed by the framework.

There are some agents for which the `sessionmgr` maintains an `AgentController`,
and thus the agent remains alive for the duration of the session. These
"session" agents also get access to `fuchsia.modular.PuppetMaster` in their
incoming namespace.

### Communication mechanisms

Components can communicate with agents in two different ways: either by
connecting to a FIDL service exposed by the agent, or over a `MessageQueue`.

Which communication method is appropriate depends on the semantics of the
messages being passed. FIDL requires both agent and client to be running,
whereas message queues allow the life cycles of the sender and receiver to be
different.

#### FIDL Services

The modular framework will forward a `fuchsia.sys.ServiceProvider` request via
`fuchsia::modular::Agent.Connect` call, and will also provide the agent with an
identifier for the client which is requesting the service provider.

Any services added to the service provider will be exposed directly to the
connecting component.

To illustrate this, consider a module connecting to an agent:

The module calls `ConnectToAgent` on its `ComponentContext`, which contains a
`ServiceProvider` request as well as an `AgentController` request.

The agent controller request is used by the framework to keep the agent alive
until the agent controller is closed by the client. If more than one client is
connected to the same agent, the agent will be kept alive until all agent
controllers have been closed.

The service provider request is forwarded to the agent, along with a string
which identifies the client connecting to the agent.

#### Message Queues

Messages sent over message queues are untyped, but allow the life cycles of the
reader and writer to be decoupled. For example, an agent may provide a module
with a message queue which it can use to send messages to the agent. The agent
can then register to be woken up when a message is delivered on the queue.
