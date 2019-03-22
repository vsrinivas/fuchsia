# Guide to Configuring the Modular Framework

## Requirements

To configure the modular framework, you will need to create a JSON file defining
the required configurations for `basemgr` and `sessionmgr` as detailed below.
The configuration file should be packaged via the build rule `modular_config`,
which will validate your file against a schema. You must then include the
modular_config() target in the product's monolith packages.

## Example
```
{
  "basemgr": {
    "disable_statistics": "true",
    "base_shell_launch_configs": {
        url: "fuchsia-pkg://fuchsia.com/dev_base_shell#meta/dev_base_shell.cmx",
    },
    "session_shell_launch_configs": {
      "url": "fuchsia-pkg://fuchsia.com/ermine_session_shell#meta/ermine_session_shell.cmx",
      "display_usage": "near",
      "screen_height": 50,
      "screen_width": 100
    }
  },
  "sessionmgr": {
    "use_memfs_for_ledger": true,
    "cloud_provider": false,
    "story_shell_factory": NONE,
    "startup_agents": [
      "fuchsia-pkg://fuchsia.com/startup_agent#meta/startup_agent.cmx"
    ],
    "session_agents": [
      "fuchsia-pkg://fuchsia.com/session_agent#meta/session_agent.cmx"
    ]
  }
}
```

## Basemgr fields

* `base_shell_launch_configs` **boolean** *(required)*
    - `url`: **string** *(required)*
        * The fuchsia package url for which base shell to use.
    - `keep_alive_after_login` **boolean** *(optional)*
        * When set to true, the base shell is kept alive after a log in. This is
          used for testing because current integration tests expect base shell
          to always be running.
        * **default**: `false`
* `session_shell_launch_configs` *(required)*
    - `url`: **string** *(required)*
        * The fuchsia package url for which session shell to use.
    - `display_usage`: **string** *(optional)*
        * The display usage policy for this session shell.
        * **Examples**: handheld, close, near, midrange, far
    - `screen_height`: **float** *(optional)*
        * The screen height in millimeters for the session shell's display.
    - `screen_width`: **float** *(optional)*
        * The screen width in millimeters for the session shell's display.
* `enable_cobalt`: **boolean** *(optional)*
    - When set to false, Cobalt statistics are disabled.
    - **default**: `true`
* `enable_presenter`: **boolean** *(optional)*
    - When set to true, the Presenter service controls management of views.
    - **default**: `false`
* `use_minfs`: **boolean** *(optional)*
    - When set to true, wait for persistent data to initialize.
    - **default**: `true`
* `use_session_shell_for_story_shell_factory`: **boolean** *(optional)*
    - Create story shells through StoryShellFactory exposed by the session shell
      instead of creating separate story shell components. When set,
      `story_shell_url` and any story shell args are ignored.
    - **default**: `false`
* `test`: **boolean** *(optional)*
    - Tells basemgr whether it is running as a part of an integration test.
    - **default**: `false`


## Sessionmgr fields

* `cloud_provider`: **string** *(optional)*
    - Options:
        * `LET_LEDGER_DECIDE`: Use a cloud provider configured by ledger.
        * `FROM_ENVIRONMENT`: Use a cloud provider available in the incoming
          namespace, rather than initializing and instance within sessionmgr.
          This can be used by Voila to inject a custom cloud provider.
        * `NONE`: Do not use a cloud provider.
    - **default**: `LET_LEDGER_DECIDE`
* `enable_cobalt`: **boolean** *(optional)*
    - When set to false, Cobalt statistics are disabled. This is used for testing.
    - **default**: `true`
* `enable_story_shell_preload`: **boolean** *(optional)*
    - When set to false, StoryShell instances are not warmed up as a startup
      latency optimization. This is used for testing.
    - **default**: `true`
* `use_memfs_for_ledger`: **boolean** *(optional)*
    - Tells the sessionmgr whether it should host+pass a memfs-backed directory to
      the ledger for the user's repository, or to use /data/LEDGER.
    - **default**: `false`
* `startup_agents`: **string[]** *(optional)*
    - A list of fuchsia package urls that specify which agents to launch at
      startup.
* `session_agents`: **string[]** *(optional)*
    - A list of fuchsia package urls that specify which agents to launch at
      startup with PuppetMaster and FocusProvider services.
* `story_shell_url`: **string** *(optional)*
    - The fuchsia package url for which story shell to use.
    - **default**: `fuchsia-pkg://fuchsia.com/mondrian#meta/mondrian.cmx`