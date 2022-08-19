# Guide to Configuring the Modular Framework

Note: The Modular framework is being deprecated in favor of
the [Session Framework](/docs/concepts/session/introduction.md).

## Requirements

To configure the modular framework, you will need to create a JSON file defining
the required configurations for `basemgr` and `sessionmgr` as detailed below.
The configuration file should be packaged via the build rule `modular_config`,
which will validate your file against a schema. You must then include the
`modular_config()` target in the product's base packages.

The file may contain (non-standard JSON) C-style comments
(`/* block */` and `// inline`).

## Reading configuration

The configuration provided to `basemgr` is available through
the [component inspection][docs-inspect] of the `basemgr` and
`sessionmgr` components.

Use [`ffx inspect`][ffx-inspect] to query the configuration
of a running `basemgr`:

```posix-terminal
ffx inspect show basemgr.cmx:root:config
```

Use [`ffx inspect`][ffx-inspect] to query the configuration
of a running `sessionmgr`:

```posix-terminal
ffx inspect show sessionmgr.cmx:root:config
```

## Example

```json5
{
  /* This is a block comment.
     Comments are ignored. */
  // This is an inline comment. Comments are ignored.
  "basemgr": {
    "enable_cobalt": false,
    "use_session_shell_for_story_shell_factory": true,
    "session_shells": [
      {
        "url": "fuchsia-pkg://fuchsia.com/session_shell#meta/session_shell.cmx",
      }
    ],
    "story_shell_url": "fuchsia-pkg://fuchsia.com/story_shell#meta/story_shell.cmx"
  },
  "sessionmgr": {
    "startup_agents": [
      "fuchsia-pkg://fuchsia.com/startup_agent#meta/startup_agent.cmx"
    ],
    "session_agents": [
      "fuchsia-pkg://fuchsia.com/session_agent#meta/session_agent.cmx"
    ],
    "component_args": [
      {
        "uri": "fuchsia-pkg://fuchsia.com/startup_agent#meta/startup_agent.cmx",
        "args": [ "--foo", "--bar=true" ]
      }
    ],
    "agent_service_index": [
      {
        "service_name": "fuchsia.modular.SomeServiceName",
        "agent_url": "fuchsia-pkg://fuchsia.com/some_agent#meta/some_agent.cmx"
      }
    ],
    "v2_modular_agents": [
      {
        "service_name": "fuchsia.modular.Agent.foobar",
        "agent_url": "fuchsia-pkg://fuchsia.com/foobar#meta/foobar.cmx"
      }
    ],
    "restart_session_on_agent_crash": [
      "fuchsia-pkg://fuchsia.com/some_agent#meta/some_agent.cmx"
    ]
    "disable_agent_restart_on_crash": false,
    "present_mods_as_stories": false,
  }
}
```

## Basemgr fields

- `session_shells` **array** _(optional)_
  - List of zero or one session shell containing the following
    fields (is an Array type for backwards compatibility):
    - `url`: **string** _(required)_
      - The fuchsia component url for which session shell to use.
  - **default**: An empty array. sessionmgr will not launch a session shell.
- `story_shell_url`: **string** _(optional)_
  - The fuchsia component url for which story shell to use.
- `enable_cobalt`: **boolean** _(optional)_
  - When set to false, Cobalt statistics are disabled.
  - **default**: `true`
- `use_session_shell_for_story_shell_factory`: **boolean** _(optional)_
  - Create story shells through StoryShellFactory exposed by the session shell
    instead of creating separate story shell components. When set,
    `story_shell_url` and any story shell args are ignored.
  - **default**: `false`

## Sessionmgr fields

- `enable_cobalt`: **boolean** _(optional)_
  - When set to false, Cobalt statistics are disabled. This is used for
    testing.
  - **default**: `true`
- `startup_agents`: **string[]** _(optional)_
  - A list of fuchsia component URLs that specify which agents to launch at
    startup.
- `session_agents`: **string[]** _(optional)_
  - A list of fuchsia component URLs that specify which agents to launch at
    startup with PuppetMaster and FocusProvider services.
- `component_args`: **array** _(optional)_
  - A list of key/value pairs to construct a map from component URI to
    arguments list for that component. Presence in this list results in the
    given arguments passed to the component as its argv at launch.
    - `uri`: The component's uri.
    - `args`: A list of arguments to be passed to the component specified by
      `uri`. Arguments must be prefixed with --.
- `agent_service_index`: **array** _(optional)_
  - A list of key/value pairs mapping from service name to the serving
    component's URL. Agents and the session shell are both valid components to
    specify here.

    Service names must be unique: only one component can provide any
    given service.

    These services are provided to modules, the session shell,
    and agents, in their namespace (i.e. at the path
    "/svc/fully.qualified.ServiceName").

    - `service_name`: The name of a service offered by `agent_url`.
    - `agent_url`: A fuchsia component URL that specifies which agent/shell will
      provide the named service.
- `v2_modular_agents`: **array** _(optional)_
  - A list of key/value pairs mapping from a name of the `fuchsia.modular.Agent`
    protocol routed to sessionmgr from a v2 component, to a v1 agent URL.

    The protocol must be present in `additional_services_for_agents`
    passed to `fuchsia.modular/Sessionmgr.Initialize`.

    Agent URLs must be unique: only one service is associated with an agent.

    Modules, the session shell, and other agents, can connect to services
    exposed by v2 agents via
    `fuchsia.modular/ComponentContext.DeprecatedConnectToAgent`
    and `fuchsia.modular/ComponentContext.DeprecatedConnectToAgentService`.

    The agent URL and service name must be present in `agent_service_index`
    to connect to services with `DeprecatedConnectToAgentService`.

    - `service_name`: The name of a `fuchsia.modular.Agent` service.
      This name **must** start with "fuchsia.modular.Agent.",
      e.g. "fuchsia.modular.Agent.foo"
    - `agent_url`: An URL that identifies this v2 agent as a Modular agent.
      This is typically a v1 component URL that corresponds to the agent URL
      requested by DeprecatedConnectToAgent(Service) clients, or that matches
      an entry in `agent_service_index`,
      e.g. "fuchsia-pkg://fuchsia.com/test_agent#meta/test_agent.cmx"
- `restart_session_on_agent_crash`: **array** _(optional)_
  - A list of agent URLs that will cause the session to be restarted
    when they terminate unexpectedly. If an agent is not in this list,
    sessionmgr will restart it individually, preserving the session.

    The session shell is automatically added to this list.
- `disable_agent_restart_on_crash`: **boolean** _(optional)_
  - When set to true, disables any automatic restarts of agents listed in
    `session_agents` if they crash.
  - **default**: `false`
- `present_mods_as_stories`: **boolean** _(optional)_
  - When set to true, module views are presented to the session shell as
    story shell views through the GraphicalPresenter protocol.
    If the session shell exposes the SessionShell protocol instead of
    GraphicalPresenter, `present_mods_as_stories` has no effect and mod views
    will be sent to the story shell.
  - **default**: `false`

[docs-inspect]: /docs/development/diagnostics/inspect/README.md
[ffx-inspect]: https://fuchsia.dev/reference/tools/sdk/ffx.md#inspect
