# Agent services integration tests

Modular's session manager can provision services by name, by looking up an
agent known to provide the service. This function is available through the
`ComponentContext` method `ConnectToAgentService()`.

This tests the implementation of the AgentServicesIndex as encoded in the
Session Manager's configuration (`SessionmgrConfig`).
