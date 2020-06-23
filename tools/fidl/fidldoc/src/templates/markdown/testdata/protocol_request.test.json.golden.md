[TOC]

# test.name


## **PROTOCOLS**

## Child {#Child}
*Defined in [test.name/protocol_request.test.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/protocol_request.test.fidl#3)*


## Parent {#Parent}
*Defined in [test.name/protocol_request.test.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/protocol_request.test.fidl#6)*


### GetChild {#test.name/Parent.GetChild}


#### Request
<table>
    <tr><th>Name</th><th>Type</th></tr>
    </table>


#### Response
<table>
    <tr><th>Name</th><th>Type</th></tr>
    <tr>
            <td><code>c</code></td>
            <td>
                <code><a class='link' href='#Child'>Child</a></code>
            </td>
        </tr></table>

### GetChildRequest {#test.name/Parent.GetChildRequest}


#### Request
<table>
    <tr><th>Name</th><th>Type</th></tr>
    </table>


#### Response
<table>
    <tr><th>Name</th><th>Type</th></tr>
    <tr>
            <td><code>r</code></td>
            <td>
                <code>request&lt;<a class='link' href='#Child'>Child</a>&gt;</code>
            </td>
        </tr></table>

### TakeChild {#test.name/Parent.TakeChild}


#### Request
<table>
    <tr><th>Name</th><th>Type</th></tr>
    <tr>
            <td><code>c</code></td>
            <td>
                <code><a class='link' href='#Child'>Child</a></code>
            </td>
        </tr></table>



### TakeChildRequest {#test.name/Parent.TakeChildRequest}


#### Request
<table>
    <tr><th>Name</th><th>Type</th></tr>
    <tr>
            <td><code>r</code></td>
            <td>
                <code>request&lt;<a class='link' href='#Child'>Child</a>&gt;</code>
            </td>
        </tr></table>





## **STRUCTS**













