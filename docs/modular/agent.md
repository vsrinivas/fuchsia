Agent
=====

An agent is an application that implements the Agent interface, whose lifecycle
is not tied to any Story and is a singleton in User scope. An agent can be
invoked by other components or by the system in response to triggers. An agent
can terminate itself or be terminated by the system. An agent can provide /
receive services to / from other applications, send / receive messages and give
suggestions to the user.

## See also:
[Agent](../services/agent/agent.fidl)
[AgentContext](../services/agent/agent_context.fidl)
[AgentController](../services/agent/agent_controller/agent_controller.fidl)
