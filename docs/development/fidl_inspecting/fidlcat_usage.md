# fidlcat: Guide

## Launching fidlcat

For information about launching fidlcat: [fidlcat](../fidl_inspecting).

## Disclaimer

This file only renders correctly within [fuchsia.dev](https://fuchsia.dev). If
you don't see the examples, navigate to
[https://fuchsia.dev/fuchsia-src/development/fidl_inspecting/fidlcat_usage](https://fuchsia.dev/fuchsia-src/development/fidl_inspecting/fidlcat_usage)

## Default display

The default display for fidlcat is:

<pre>echo_client_cpp_synchronous <font color="#CC0000">180768</font>:<font color="#CC0000">180781</font> zx_channel_call(handle:<font color="#4E9A06">handle</font>: <font color="#CC0000">14b21e1b</font>, options:<font color="#4E9A06">uint32</font>: <font color="#3465A4">0</font>, deadline:<font color="#4E9A06">time</font>: <font color="#3465A4">ZX_TIME_INFINITE</font>, rd_num_bytes:<font color="#4E9A06">uint32</font>: <font color="#3465A4">65536</font>, rd_num_handles:<font color="#4E9A06">uint32</font>: <font color="#3465A4">64</font>)
  <span style="background-color:#75507B"><font color="#D3D7CF">sent request</font></span> <font color="#4E9A06">fidl.examples.echo/Echo.EchoString</font> = {
    value: <font color="#4E9A06">string</font> = <font color="#CC0000">&quot;hello synchronous world&quot;</font>
  }
  -&gt; <font color="#4E9A06">ZX_OK</font>
    <span style="background-color:#75507B"><font color="#D3D7CF">received response</font></span> <font color="#4E9A06">fidl.examples.echo/Echo.EchoString</font> = {
      response: <font color="#4E9A06">string</font> = <font color="#CC0000">&quot;hello synchronous world&quot;</font>
    }
</pre>

We have the following information:

-   **echo_client_cpp_synchronous**: the name of the application which has
    generated this display.

-   **180768**: the process koid.

-   **180781**: the thread koid.

-   **zx_channel_call**: the name of the intercepted/displayed system call.

-   all the basic input parameters of the system call (here **handle** and
    **options**).

    For each one, we have:

    -   The name of the parameter in black.

    -   The type of the parameter in green.

    -   The value of the parameter (the color depends on the parameter type).

-   all the complex input parameters. Here we display a FIDL message. This is a
    request which is sent by our application.

The display stops here. It will resume when the system call returns (sometimes
it can be a very long time). For one thread, there will be no other display
between the input arguments and the returned value. However, another thread
display may be interleaved. When the system call returns, we display:

-   The returned value (-> ZX_OK)

-   The basic output parameters (there is no basic output parameters in this
    example).

-   The complex output parameters. Here we display a FIDL message. This is the
    response we received to the request we sent.

For **zx_channel_read** we can have this display:

<pre>echo_client_rust <font color="#CC0000">256109</font>:<font color="#CC0000">256122</font> zx_channel_read(handle:<font color="#4E9A06">handle</font>: <font color="#CC0000">e4c7c57f</font>, options:<font color="#4E9A06">uint32</font>: <font color="#3465A4">0</font>, num_bytes:<font color="#4E9A06">uint32</font>: <font color="#3465A4">48</font>, num_handles:<font color="#4E9A06">uint32</font>: <font color="#3465A4">0</font>)
  -&gt; <font color="#4E9A06">ZX_OK</font>
    <span style="background-color:#75507B"><font color="#D3D7CF">received response</font></span> <font color="#4E9A06">fidl.examples.echo/Echo.EchoString</font> = {
      response: <font color="#4E9A06">string</font> = <font color="#CC0000">&quot;hello world!&quot;</font>
    }
</pre>

But, if there is an error, we can have:

<pre>echo_client_rust <font color="#CC0000">256109</font>:<font color="#CC0000">256122</font> zx_channel_read(handle:<font color="#4E9A06">handle</font>: <font color="#CC0000">e4c7c57f</font>, options:<font color="#4E9A06">uint32</font>: <font color="#3465A4">0</font>, num_bytes:<font color="#4E9A06">uint32</font>: <font color="#3465A4">0</font>, num_handles:<font color="#4E9A06">uint32</font>: <font color="#3465A4">0</font>)
  -&gt; <font color="#CC0000">ZX_ERR_SHOULD_WAIT</font>
</pre>

Or:

<pre>echo_client_rust <font color="#CC0000">256109</font>:<font color="#CC0000">256122</font> zx_channel_read(handle:<font color="#4E9A06">handle</font>: <font color="#CC0000">e4c7c57f</font>, options:<font color="#4E9A06">uint32</font>: <font color="#3465A4">0</font>, num_bytes:<font color="#4E9A06">uint32</font>: <font color="#3465A4">0</font>, num_handles:<font color="#4E9A06">uint32</font>: <font color="#3465A4">0</font>)
  -&gt; <font color="#CC0000">ZX_ERR_BUFFER_TOO_SMALL</font> (actual_bytes:<font color="#4E9A06">uint32</font>: <font color="#3465A4">48</font>, actual_handles:<font color="#4E9A06">uint32</font>: <font color="#3465A4">0</font>)
</pre>

In this last case, even if the system call fails, we have some valid output
parameters. **actual_bytes** and **actual_handles** give the minimal values
which should have been used to call **zx_channel_read**.

## Modifying the display

By default, we only display the process information on the first line.

Eventually, we also display the process information before the returned value if
a system call from another thread has been displayed between the call and the
returned value:

<pre>ledger.cmx <font color="#CC0000">5859666</font>:<font color="#CC0000">5861991</font> zx_channel_write(handle:<font color="#4E9A06">handle</font>: <font color="#CC0000">035393df</font>, options:<font color="#4E9A06">uint32</font>: <font color="#3465A4">0</font>)
  <span style="background-color:#75507B"><font color="#D3D7CF">sent request</font></span> <font color="#4E9A06">fuchsia.io/Directory.Open</font> = {
    flags: <font color="#4E9A06">uint32</font> = <font color="#3465A4">12582912</font>
    mode: <font color="#4E9A06">uint32</font> = <font color="#3465A4">0</font>
    path: <font color="#4E9A06">string</font> = <font color="#CC0000">&quot;29/cache/cached_db&quot;</font>
    object: <font color="#4E9A06">handle</font> = <font color="#CC0000">03f3b46b</font>
  }

ledger.cmx <font color="#CC0000">5859666</font>:<font color="#CC0000">5859693</font> zx_channel_write(handle:<font color="#4E9A06">handle</font>: <font color="#CC0000">035393df</font>, options:<font color="#4E9A06">uint32</font>: <font color="#3465A4">0</font>)
  <span style="background-color:#75507B"><font color="#D3D7CF">sent request</font></span> <font color="#4E9A06">fuchsia.io/Directory.Open</font> = {
    flags: <font color="#4E9A06">uint32</font> = <font color="#3465A4">13107200</font>
    mode: <font color="#4E9A06">uint32</font> = <font color="#3465A4">0</font>
    path: <font color="#4E9A06">string</font> = <font color="#CC0000">&quot;.&quot;</font>
    object: <font color="#4E9A06">handle</font> = <font color="#CC0000">0053b5fb</font>
  }

ledger.cmx <font color="#CC0000">5859666</font>:<font color="#CC0000">5861991</font>   -&gt; <font color="#4E9A06">ZX_OK</font>

ledger.cmx <font color="#CC0000">5859666</font>:<font color="#CC0000">5859693</font>   -&gt; <font color="#4E9A06">ZX_OK</font>
</pre>

Using the flag **--with-process-info**, we can display the process information
on each line:

<pre>echo_client_rust <font color="#CC0000">305640</font>:<font color="#CC0000">305653</font> zx_channel_write(handle:<font color="#4E9A06">handle</font>: <font color="#CC0000">4446ec4b</font>, options:<font color="#4E9A06">uint32</font>: <font color="#3465A4">0</font>)
echo_client_rust <font color="#CC0000">305640</font>:<font color="#CC0000">305653</font>   <span style="background-color:#75507B"><font color="#D3D7CF">sent request</font></span> <font color="#4E9A06">fidl.examples.echo/Echo.EchoString</font> = {
echo_client_rust <font color="#CC0000">305640</font>:<font color="#CC0000">305653</font>     value: <font color="#4E9A06">string</font> = <font color="#CC0000">&quot;hello world!&quot;</font>
echo_client_rust <font color="#CC0000">305640</font>:<font color="#CC0000">305653</font>   }
echo_client_rust <font color="#CC0000">305640</font>:<font color="#CC0000">305653</font>   -&gt; <font color="#4E9A06">ZX_OK</font>
</pre>

This is very useful if we want to do a **grep** on the output (for example, to
only select one thread).

## Interpreting the display

Most of the time we want to link several messages to be able to understand what
our program is doing.

In this example:

<pre>ledger.cmx <font color="#CC0000">5859666</font>:<font color="#CC0000">5859693</font> zx_channel_create(options:<font color="#4E9A06">uint32</font>: <font color="#3465A4">0</font>)
  -&gt; <font color="#4E9A06">ZX_OK</font> (out0:<font color="#4E9A06">handle</font>: <font color="#CC0000">0243b493</font>, out1:<font color="#4E9A06">handle</font>: <font color="#CC0000">0163b42b</font>)

ledger.cmx <font color="#CC0000">5859666</font>:<font color="#CC0000">5859693</font> zx_channel_write(handle:<font color="#4E9A06">handle</font>: <font color="#CC0000">035393df</font>, options:<font color="#4E9A06">uint32</font>: <font color="#3465A4">0</font>)
  <span style="background-color:#75507B"><font color="#D3D7CF">sent request</font></span> <font color="#4E9A06">fuchsia.io/Directory.Open</font> = {
    flags: <font color="#4E9A06">uint32</font> = <font color="#3465A4">12582912</font>
    mode: <font color="#4E9A06">uint32</font> = <font color="#3465A4">0</font>
    path: <font color="#4E9A06">string</font> = <font color="#CC0000">&quot;29&quot;</font>
    object: <font color="#4E9A06">handle</font> = <font color="#CC0000">0163b42b</font>
  }
  -&gt; <font color="#4E9A06">ZX_OK</font>

ledger.cmx <font color="#CC0000">5859666</font>:<font color="#CC0000">5859693</font> zx_channel_read(handle:<font color="#4E9A06">handle</font>: <font color="#CC0000">0243b493</font>, options:<font color="#4E9A06">uint32</font>: <font color="#3465A4">1</font>, num_bytes:<font color="#4E9A06">uint32</font>: <font color="#3465A4">64</font>, num_handles:<font color="#4E9A06">uint32</font>: <font color="#3465A4">1</font>)
  -&gt; <font color="#4E9A06">ZX_OK</font>
    <span style="background-color:#75507B"><font color="#D3D7CF">received response</font></span> <font color="#4E9A06">fuchsia.io/Node.OnOpen</font> = {
      s: <font color="#4E9A06">int32</font> = <font color="#3465A4">-25</font>
      info: <font color="#4E9A06">fuchsia.io/NodeInfo</font> = <font color="#3465A4">null</font>
    }

ledger.cmx <font color="#CC0000">5859666</font>:<font color="#CC0000">5859693</font> zx_channel_read(handle:<font color="#4E9A06">handle</font>: <font color="#CC0000">0203b493</font>, options:<font color="#4E9A06">uint32</font>: <font color="#3465A4">1</font>, num_bytes:<font color="#4E9A06">uint32</font>: <font color="#3465A4">64</font>, num_handles:<font color="#4E9A06">uint32</font>: <font color="#3465A4">1</font>)
  -&gt; <font color="#4E9A06">ZX_OK</font>
    <span style="background-color:#75507B"><font color="#D3D7CF">received response</font></span> <font color="#4E9A06">fuchsia.io/Node.OnOpen</font> = {
      s: <font color="#4E9A06">int32</font> = <font color="#3465A4">0</font>
      info: <font color="#4E9A06">fuchsia.io/NodeInfo</font> = { directory: <font color="#4E9A06">fuchsia.io/DirectoryObject</font> = {} }
    }

ledger.cmx <font color="#CC0000">5859666</font>:<font color="#CC0000">5859693</font> zx_channel_call(handle:<font color="#4E9A06">handle</font>: <font color="#CC0000">0203b493</font>, options:<font color="#4E9A06">uint32</font>: <font color="#3465A4">0</font>, deadline:<font color="#4E9A06">time</font>: <font color="#3465A4">ZX_TIME_INFINITE</font>, rd_num_bytes:<font color="#4E9A06">uint32</font>: <font color="#3465A4">24</font>, rd_num_handles:<font color="#4E9A06">uint32</font>: <font color="#3465A4">0</font>)
  <span style="background-color:#75507B"><font color="#D3D7CF">sent request</font></span> <font color="#4E9A06">fuchsia.io/Node.Close</font> = {}
  -&gt; <font color="#4E9A06">ZX_OK</font>
    <span style="background-color:#75507B"><font color="#D3D7CF">received response</font></span> <font color="#4E9A06">fuchsia.io/Node.Close</font> = {
      s: <font color="#4E9A06">int32</font> = <font color="#3465A4">0</font>
    }
</pre>

We first create a channel. The two handles **0243b493** and **0163b42b** are
linked. That means that a write on one handle will result on a read on the other
handle.

We use handle **0163b42b** in the **Directory.Open** message. That means that
the associated handle (**0243b493**) is the handle which controls the directory
we just opened.

When we receive **Node.OnOpen** on **0243b493** we know that it's a response to
our **Directory.Open**. We also used the handle to call **Node.Close**.
