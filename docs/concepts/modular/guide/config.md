# Guide to Configuring the Modular Framework

> DEPRECATION WARNING: The Modular framework is being deprecated in favor of
> the [Session Framework](/docs/concepts/session/introduction.md).

## Requirements

To configure the modular framework, you will need to create a JSON file defining
the required configurations for `basemgr` and `sessionmgr` as detailed below.
The configuration file should be packaged via the build rule `modular_config`,
which will validate your file against a schema. You must then include the
modular_config() target in the product's base packages.

The file may contain (non-standard JSON) C-style comments
(`/* block */` and `// inline`).

## Example

```
{
  /* This is a block comment.
     Comments are ignored. */
  // This is an inline comment. Comments are ignored.
  "basemgr": {
    "enable_cobalt": false,
    "use_session_shell_for_story_shell_factory": true,
    "base_shell": {
      "url": "fuchsia-pkg://fuchsia.com/dev_base_shell#meta/dev_base_shell.cmx",
    },
    "session_shells": [
      {
        "url": "fuchsia-pkg://fuchsia.com/ermine_session_shell#meta/ermine_session_shell.cmx",
        "display_usage": "near",
        "screen_height": 50.0,
        "screen_width": 100.0
      }
    ]
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
    "restart_session_on_agent_crash": [
      "fuchsia-pkg://fuchsia.com/some_agent#meta/some_agent.cmx"
    ]
  }
}
```

## Basemgr fields

- `base_shell` **boolean** _(required)_
  - `url`: **string** _(required)_
    - The fuchsia component url for which base shell to use.
  - `keep_alive_after_login` **boolean** _(optional)_
    - When set to true, the base shell is kept alive after a log in. This is
      used for testing because current integration tests expect base shell
      to always be running.
    - **default**: `false`
  - `args` **string[]** _(optional)_
    - A list of arguments to be passed to the base shell specified by url.
      Arguments must be prefixed with --.
- `session_shells` **array** _(required)_
  - List of max one session shell containing the following
    fields (is an Array type for backwards compatibility):
    - `url`: **string** _(required)_
      - The fuchsia component url for which session shell to use.
    - `display_usage`: **string** _(optional)_
      - The display usage policy for this session shell.
      - Options:
        - `handheld`: the display is used well within arm's reach.
        - `close`: the display is used at arm's reach.
        - `near`: the display is used beyond arm's reach.
        - `midrange`: the display is used beyond arm's reach.
        - `far`: the display is used well beyond arm's reach.
    - `screen_height`: **float** _(optional)_
      - The screen height in millimeters for the session shell's display.
    - `screen_width`: **float** _(optional)_
      - The screen width in millimeters for the session shell's display.
- `story_shell_url`: **string** _(optional)_
  - The fuchsia component url for which story shell to use.
  - **default**: `fuchsia-pkg://fuchsia.com/mondrian#meta/mondrian.cmx`
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
  - A list of fuchsia component urls that specify which agents to launch at
    startup.
- `session_agents`: **string[]** _(optional)_
  - A list of fuchsia component urls that specify which agents to launch at
    startup with PuppetMaster and FocusProvider services.
- `component_args`: **array** _(optional)_
  - A list of key/value pairs to construct a map from component URI to
    arguments list for that component. Presence in this list results in the
    given arguments passed to the component as its argv at launch.
    - `uri`: The component's uri.
    - `args`: A list of arguments to be passed to the component specified by
      `uri`. Arguments must be prefixed with --.
- `agent_service_index`: **array** _(optional)_
  - A list of key/value pairs mapping from service name to the serving component's
    URL. Agents and the session shell are both valid components to specify
    here.  Service names must be unique: only one component can provide any
    given service. These services are provided to modules, the session shell,
    and agents, in their incoming namespace (i.e. at the path
    "/svc/fully.qualified.ServiceName").
    - `service_name`: The name of a service offered by `agent_url`.
    - `agent_url`: A fuchsia component url that specifies which agent/shell will
      provide the named service.
- `restart_session_on_agent_crash`: array (optional)
  - A list of agent URLs that will cause the session to be restarted
    when they terminate unexpectedly. If an agent is not in this list,
    sessionmgr will restart it individually, preserving the session.

    The session shell is automatically added to this list.
