# Evaluating and printing expressions in zxdb

The `print` command can evaluate simple C/C++ expressions in the context of a stack frame. When a
thread is suspended (see “Working with threads” above) just type:

```
[zxdb] print i
34
```

Expressions can use most simple C/C++ syntax:

```
[zxdb] print &foo->bar[baz]
(const MyStruct*) 0x59f4e1268f70

```

You can also evaluate expressions in the context of other stack frames without switching to them
(see “Interaction model” above for more):

```
[zxdb] frame 2 print argv[0]
"/bin/cowsay"
```

Often you will want to see all local variables:

```
[zxdb] locals
argc = 1
argv = (const char* const*) 0x59999ec02dc0
```

You can also set variables:

```
[zxdb] print done_flag = true
true
[zddb] print i = 56
56
```
