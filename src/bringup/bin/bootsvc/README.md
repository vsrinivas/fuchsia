# Bootsvc is being deprecated!

As of 2022/03/01, bootsvc is no longer doing anything useful in the boot process besides
loading component manager, and the flag `//products/kernel_cmdline:userboot-next--skip_bootsvc`
exists to load component manager directly from userboot.

Any new business logic that you were intending to put in bootsvc likely belongs in component
manager, or much less likely in userboot. If neither of these binaries satisfies your requirements,
please reach out to dahastin@ and abdulla@. For the current status of the final deprecation and
removal from the ZBI, see fxr/44784.