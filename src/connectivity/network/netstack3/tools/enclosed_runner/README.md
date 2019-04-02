# Recovery Netstack Enclosed Runner

`enclosed_runner` is a component that starts recovery netstack in its own environment and
optionally performs some early configuration.
Use this alongside `chrealm` to be able to manually test the netstack without having to hack your
way around `sysmgr` entries.

## Usage

A typical workflow for using `enclosed_runner` is as follows:
* Assuming we have a freshly booted system, with two ethernets available, at
  `/dev/class/ethernet/000` and `/dev/class/ethernet/001`. The main netstack
  will have picked up on both at boot and most likely `000` has interface id `2`
  and `001` has interface id `3`.
* First thing to do is to remove interface `3` so we can give it to `netstack3`:
```
$ net_cli if del 3
```
* Now that we can use ethernet `001` for ourselves, we can start `enclosed_runner` giving it
the path to the ethernet device and some fixed IP address:
```
$ run_test_component fuchsia-pkg://fuchsia.com/netstack3_tools#meta/enclosed_runner.cmx -e /dev/class/ethernet/001 -i 192.168.3.55/24 &
```
*Note the `&` at the end* of the command. `enclosed_runner` is using the
`fuchsia.test` facet to make use of `run_test_component`'s environment creation.
To keep `netstack3` alive, `enclosed_runner` must block, that's why we start it
with `&`.
* Once `enclosed_runner` is up and running, we should be able to communicate
  with `netstack3` using the statically assigned IP, but we should also still be
  able to use `fx shell`, `fx syslog` and other useful development commands that
  rely on the main netstack to work.
* If you want to start processes that use `netstack3`, like `net_cli` for
  example, you can `chrealm` into the realm created for `enclosed_runner`, like:
```
$ chrealm /hub/r/<env_name>/<koid>
```
where `<koid>` and `<env_name>` will both change between different runs. The chrealm command to run will be printed when starting `enclosed_runner`. `chrealm` will drop you into a nested shell
environment that is mapping the `svc` folder to the enclosed environment where
`netstack3` is running. You can `CTRL+D` out of `chrealm` to go back to the `sys` realm
when you're done.
* Because `enclosed_runner` will keep running in the background, you can kill it
  (and `netstack3` along with it) with `killall enclosed_runner.cmx`. And,
  because `netstack3` is killed along with it, if you re-build of `netstack3`
  and re-run `enclosed_runner` you will automatically get the new version, as
  long as you have `fx serve` running.


## Command line interface

Below are all command line options accepted by `enclosed_runner`.

```
USAGE:
    enclosed_runner [OPTIONS]

FLAGS:
    -h, --help       Prints help information
    -V, --version    Prints version information

OPTIONS:
    -e <ethernet>         Path to the ethernet device to add to netstack, usually somewhere in /dev/class/ethernet
    -i <ip_prefix>        If ethernet is provided, also set a fixed IP address and subnet prefix to it, including
                          routing table information. IP address and subnet prefix  MUST be in the form [IP]/[prefix].
```
