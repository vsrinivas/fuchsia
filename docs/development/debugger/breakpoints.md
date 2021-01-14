# Using breakpoints in zxdb

Breakpoints stop execution when some code is executed. To create a breakpoint, use the `break`
command (`b` for short) and give it a location:

```
[zxdb] break main
Breakpoint 3 (Software) on Global, Enabled, stop=All, @ main
   180
 ◉ 181 int main(int argc, char**argv) {
   182     fbl::unique_fd dirfd;
```

A location can be expressed in many different ways.

  * Plain function name. This will match functions with the name in any namespace:

    ```
    break main
    ```

  * Member function or functions inside a specific namespace or class:

    ```
    break my_namespace::MyClass::MyFunction
    break ::OtherFunction
    ```

  * Source file + line number (separate with a colon):

    ```
    break mymain.cc:22
    ```

  * Line number within the current frame’s current source file (useful when stepping):

    ```
    break 23
    ```

  * Memory address:

    ```
    break 0xf72419a01
    ```

  * Expression: Prefixing with "*" will treat the following input as an expression that evaluates to
    an address. This is most often used with hardware breakpoints.

    ```
    break --type=write *&foo
    ```

To list all breakpoints:

```
[zxdb] breakpoint
```

> Note: this is the “breakpoint” noun (a noun by itself lists the things
> associated with it). It is not plural.

To clear a specific breakpoint, give that breakpoint index as the context for the clear command (see
“Interaction model” above). Here’s we’re using the abbreviation for `breakpoint` (`bp`):

```
[zxdb] bp 2 clear
```

Or you can clear the current breakpoint:

```
[zxdb] clear
```

Whenever you create or stop on a breakpoint, that breakpoint becomes the default automatically so
`clear` always clears the one you just hit.

`clear` can also take an optional location just like a `break` command. In this way, it will try to
clear all breakpoints at that location and ignore the default breakpoint context.

> Note for GDB users: `delete <index>` is mapped to `bp <index> clear`, while `clear <number>`
> behaves the same in GDB and zxdb.

Breakpoints can also be enabled or disabled:

```
[zxdb] disable
[zxdb] bp 4 enable
```

Other properties can be modified via the "get" and "set" commands.

```
[zxdb] bp 1 set location = Frobulator::GetThing
```

### Hardware data breakpoints ("watchpoints")

The processor can be set to break execution when it reads or writes certain addresses. This can be
particularly useful to track down memory corruption. Create a hardware breakpoint by specifying
"write", "execute" or "read-write" in the "type" for a break command (unlike in some other
debuggers, hardware breakpoints are exposed as a type of breakpoint rather than as a separate
"watchpoint" concept).

```
[zxdb] break --type=read-write --size=4 0x12345670
```

As a shortcut, the "watch" command will take the contents of a variable or the result of an
expression and set a data write breakpoint over its range:

```
[zxdb] watch i
[zxdb] watch foo[5]->bar
```

Notes:

  * CPUs only support a limited number of hardware watchpoints, typically around 4.

  * The size of a watchpoint range is limited to 1, 2, 4, or 8 bytes and the address must be an even
    multiple of the size.

  * Unlike GDB, "watch" will evaluate the expression once and set a breakpoint on the result. It
    won't re-evaluate the expression. In the above example, it will trigger when "bar" changes but
    not if "foo[5]" changes to point to a different "bar".

  * If you watch a variable on the stack and nobody touches it, you will often see it hit in
    another part of the program when the stack memory is re-used. If you get a surprising breakpoint
    hit, check that execution is still in the frame you expect.

### Programmatic breakpoints

You can insert a hardcoded breakpoint in your code if you want to catch some specific condition.
Clang has a builtin (it won't work in GCC Zircon builds):

```
__builtin_debugtrap();
```

If the debugger is already attached to the process, it will stop as if a normal breakpoint was hit.
You can step or continue from there. If the debugger is not already attached, this will cause a
crash.

