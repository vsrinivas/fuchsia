[TOC]

# fidl.test.json


## **PROTOCOLS**

## Example {#Example}
*Defined in [fidl.test.json/error.test.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/error.test.fidl#3)*


### foo {#fidl.test.json/Example.foo}


#### Request
<table>
    <tr><th>Name</th><th>Type</th></tr>
    <tr>
            <td><code>s</code></td>
            <td>
                <code>string</code>
            </td>
        </tr></table>


#### Response
<table>
    <tr><th>Name</th><th>Type</th></tr>
    <tr>
            <td><code>result</code></td>
            <td>
                <code><a class='link' href='#Example_foo_Result'>Example_foo_Result</a></code>
            </td>
        </tr></table>



## **STRUCTS**

### Example_foo_Response {#Example_foo_Response}
*Defined in [fidl.test.json/error.test.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/error.test.fidl#4)*



<table>
    <tr><th>Name</th><th>Type</th><th>Description</th><th>Default</th></tr><tr id="Example_foo_Response.y">
            <td><code>y</code></td>
            <td>
                <code>int64</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr>
</table>







## **UNIONS**

### Example_foo_Result {#Example_foo_Result}
*Defined in [fidl.test.json/error.test.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/error.test.fidl#4)*


<table>
    <tr><th>Name</th><th>Type</th><th>Description</th></tr><tr id="Example_foo_Result.response">
            <td><code>response</code></td>
            <td>
                <code><a class='link' href='#Example_foo_Response'>Example_foo_Response</a></code>
            </td>
            <td></td>
        </tr><tr id="Example_foo_Result.err">
            <td><code>err</code></td>
            <td>
                <code>uint32</code>
            </td>
            <td></td>
        </tr></table>







