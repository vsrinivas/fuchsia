# erminectl automation tool

## About erminectl

An tool used by test scripts to automate OOBE [Out Of Box Experience] and shell experience in Ermine. The following user flows can be automated:

- Set a password to authenticate to the user shell
- Login using the same password to authenticate to the user shell
- Skip any current or future OOBE page that is not available for automation.
- Launch an application from the user shell
- Close all currently open application

## Prerequisites

- A build that includes `//src/experiences/session_shells/ermine/tools` dependency
- A build that allows `fx shell` access over SSH
- A build running the workstation product configuration

## How to use

erminectl is program with a command line interface (CLI):
```sh
$ fx shell erminectl
```

## Return codes

The primary mechanism to determine if the tool reported an error is to look at the returned error code.

```sh
$ fx shell erminectl oobe login "wrong-password"
Error: InvalidArgs

$ echo $?
1
```

The tool will also return more detailed error response in addition to setting the return error code. This response can be capture and logged by the test script. Typically error conditions are when an automation command is invoked in invalid state or supplied with invalid arguments. Examples:
```sh
$ fx shell erminectl shell launch Chromium
Error: A FIDL client`s channel to the service ermine.tools.ShellAutomator was closed: NOT_FOUND

Caused by:
    NOT_FOUND
```
```sh
$ fx shell erminectl oobe login xxxx
Error: InvalidState # or Error: InvalidArgs
```

## Usage

Return the current state of Ermine: Oobe or Shell
```sh
$ fx shell erminectl
# Returns one of: Shell | Oobe
```
 Usage of erminectl:
```sh
$ fx shell erminectl --help
Usage: erminectl [<command>] [<args>]

Various operations to control Ermine user experience.

Options:
  --help            display usage information

Commands:
  oobe              Control OOBE UI.
  shell             Control shell UI.

```

Return the current OOBE screen:
```sh
$ fx shell erminectl oobe
# Returns one of: SetPassword | Login | Unknown
```

Usage of `oobe` subcommand:
```sh
$ fx shell erminectl oobe --help
Usage: erminectl oobe [<command>] [<args>]

Control OOBE UI.

Options:
  --help            display usage information

Commands:
  set_password      Create password in OOBE
  login             Login password in OOBE
  skip              Skip current screen.
```

Usage of `oobe set_password` subcommand:
```sh
$ fx shell erminectl oobe set_password --help
Usage: erminectl oobe set_password <password>

Create password in OOBE

Positional Arguments:
  password

Options:
  --help            display usage information
```

Usage of `oobe login` subcommand:
```sh
$ fx shell erminectl oobe login --help
Usage: erminectl oobe login <password>

Login password in OOBE

Positional Arguments:
  password

Options:
  --help            display usage information
```

Usage of `oobe skip` subcommand:
```sh
$ fx shell erminectl oobe skip --help
Usage: erminectl oobe skip

Skip current screen.

Options:
  --help            display usage information
```

Usage of `shell` subcommand:
```sh
$ fx shell erminectl shell --help
Usage: erminectl shell <command> [<args>]

Control shell UI.

Options:
  --help            display usage information

Commands:
  launch            Launch an application.
  closeAll          Close all running applications.
```

Usage of `shell.launch` subcommand:
```sh
$ fx shell erminectl shell launch --help
Usage: erminectl shell launch <app_name>

Launch an application.

Positional Arguments:
  app_name

Options:
  --help            display usage information
```

Usage of `shell.closeAll` subcommand:
```sh
$ fx shell erminectl shell closeAll --help
Usage: erminectl shell closeAll

Launch an application.

Options:
  --help            display usage information
```