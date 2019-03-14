# Recovery Netstack Enclosed Runner

`enclosed_runner` is a component that starts recovery netstack in its own environment and
optionally performs some early configuration.
Use this alongside `chrealm` to be able to manually test the netstack without having to hack your
way around `sysmgr` entries.

## Usage

A typical workflow for using `enclosed_runner` is as follows:
* Assuming we have a freshly booted system, with two ethernets available, at
`/dev/class/ethernet/000` and `/dev/class/ethernet/001`. The main netstack will have picked up
on both at boot and most likely `000` has interface id `3` and `001` has interface id `3`.
* First thing to do is to remove interface `3` so we can give it to the `recovery_netstack`:
```
$ net_cli if del 3
```
* Now that we can use ethernet `001` for ourselves, we can start `enclosed_runner` giving it
the path to the ethernet device and some fixed ip address:
```
$ run_test_component fuchsia-pkg://fuchsia.com/recovery_netstack_tools#meta/enclosed_runner.cmx -e /dev/class/ethernet/001 -i 192.168.3.55 &
```
*Note the `&` at the end* of the comand. `enclosed_runner` is using the `fuchsia.test` facet to
make use of `run_test_component`'s environment creation. To keep the `recovery_netstack` alive,
`enclosed_runner` must block, that's why we start it with `&`.
* Once `enclosed_runner` is up and running, we should be able to communicate with the
`recovery_netstack` using the statically assigned ip, but we should also still be able to use
`fx shell`, `fx syslog` and other useful development commands that rely on the main netstack
to work.
* If you want to start processes that use the `recovery_netstack`, like `net_cli` for example,
you can `chrealm` into the realm created for `enclosed_runner`, like:
```
$ chrealm /hub/r/sys/<koid>/r/<env_name>/<koid>
```
where `<koid>` and `<env_name>` will both change between different runs. The chrealm command to run will be printed when starting `enclosed_runner`. `chrealm` will drop you into a nested shell
environment that is mapping the `svc` folder to the enclosed environment where the
`recovery_netstack` is running. You can `CTRL+D` out of `chrealm` to go back to the `sys` realm
when you're done.
* Because `enclosed_runner` will keep running in the background, you can kill it (and the
`recovery_netstack` along with it) with `killall enclosed_runner.cmx`. And, because
`recovery_netstack` is killed along with it, if you re-build of `recovery_netstack` and re-run
`enclosed_runner` you will automatically get the new version, as long as you have `fx serve`
running.


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
