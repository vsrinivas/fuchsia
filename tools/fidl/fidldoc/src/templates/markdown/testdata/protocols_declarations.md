
## **PROTOCOLS**

## Handler {#Handler}
*Defined in [fuchsia.exception/handler.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/zircon/system/fidl/fuchsia-exception/handler.fidl#12)*

<p>Protocol meant for clients interested in handling exceptions for a
particular service.</p>

### OnException {#OnException}

<p>This exception mirrors closely the information provided by exception
channels. The design is to have clients of this API behave as closely as
possible to native exception handlers that are listening to an exception
channel.</p>
<p><code>exception</code> is an exception handle, which controls the exception's
lifetime. See exception zircon docs for more information.</p>
<p><code>info</code> represents basic exception information as provided by the
exception channel.</p>

#### Request
<table>
    <tr><th>Name</th><th>Type</th></tr>
    <tr>
            <td><code>exception</code></td>
            <td>
                <code>handle&lt;exception&gt;</code>
            </td>
        </tr><tr>
            <td><code>info</code></td>
            <td>
                <code><a class='link' href='#ExceptionInfo'>ExceptionInfo</a></code>
            </td>
        </tr></table>


#### Response
<table>
    <tr><th>Name</th><th>Type</th></tr>
    </table>

## ProcessLimbo {#ProcessLimbo}
*Defined in [fuchsia.exception/process_limbo.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/zircon/system/fidl/fuchsia-exception/process_limbo.fidl#17)*

<p>Protocol meant for clients interested in obtaining processes that are
suspended waiting for an exception handler (in limbo). This is the core
feature that enables Just In Time (JIT) debugging.</p>

### ListProcessesWaitingOnException {#ListProcessesWaitingOnException}

<p>Returns information on all the processes currently waiting on an exception.
The information provided is intended to correctly identify an exception
and determine whether the caller wants to actually handle it.
To retrieve an exception, use the |GetException| call.</p>
<p>NOTE: The |process| and |thread| handle will only have the ZX_RIGHT_READ
right, so no modification will be able to be done on them.</p>

#### Request
<table>
    <tr><th>Name</th><th>Type</th></tr>
    </table>


#### Response
<table>
    <tr><th>Name</th><th>Type</th></tr>
    <tr>
            <td><code>exception_list</code></td>
            <td>
                <code>vector&lt;<a class='link' href='#ProcessExceptionMetadata'>ProcessExceptionMetadata</a>&gt;[32]</code>
            </td>
        </tr></table>

### RetrieveException {#RetrieveException}

<p>Removes the process from limbo and retrieves the exception handle and
associated metadata from an exception.</p>
<p>Use |ListProcessesWaitingOnException| to choose a |process_koid| from the
list of available exceptions.</p>
<p>Returns ZX_ERR_NOT_FOUND if the process is not waiting on an exception.</p>

#### Request
<table>
    <tr><th>Name</th><th>Type</th></tr>
    <tr>
            <td><code>process_koid</code></td>
            <td>
                <code>uint64</code>
            </td>
        </tr></table>


#### Response
<table>
    <tr><th>Name</th><th>Type</th></tr>
    <tr>
            <td><code>result</code></td>
            <td>
                <code><a class='link' href='#ProcessLimbo_RetrieveException_Result'>ProcessLimbo_RetrieveException_Result</a></code>
            </td>
        </tr></table>
