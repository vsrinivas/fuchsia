[TOC]

# fidl.test.json


## **PROTOCOLS**

## Protocol {#Protocol}
*Defined in [fidl.test.json/request_flexible_envelope.test.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/request_flexible_envelope.test.fidl#13)*


### RequestStrictResponseFlexible {#fidl.test.json/Protocol.RequestStrictResponseFlexible}


#### Request
<table>
    <tr><th>Name</th><th>Type</th></tr>
    <tr>
            <td><code>s</code></td>
            <td>
                <code><a class='link' href='#StrictFoo'>StrictFoo</a></code>
            </td>
        </tr></table>


#### Response
<table>
    <tr><th>Name</th><th>Type</th></tr>
    <tr>
            <td><code>f</code></td>
            <td>
                <code><a class='link' href='#FlexibleFoo'>FlexibleFoo</a></code>
            </td>
        </tr></table>

### RequestFlexibleResponseStrict {#fidl.test.json/Protocol.RequestFlexibleResponseStrict}


#### Request
<table>
    <tr><th>Name</th><th>Type</th></tr>
    <tr>
            <td><code>s</code></td>
            <td>
                <code><a class='link' href='#FlexibleFoo'>FlexibleFoo</a></code>
            </td>
        </tr></table>


#### Response
<table>
    <tr><th>Name</th><th>Type</th></tr>
    <tr>
            <td><code>f</code></td>
            <td>
                <code><a class='link' href='#StrictFoo'>StrictFoo</a></code>
            </td>
        </tr></table>



## **STRUCTS**







## **UNIONS**

### FlexibleFoo {#FlexibleFoo}
*Defined in [fidl.test.json/request_flexible_envelope.test.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/request_flexible_envelope.test.fidl#3)*


<table>
    <tr><th>Name</th><th>Type</th><th>Description</th></tr><tr id="FlexibleFoo.s">
            <td><code>s</code></td>
            <td>
                <code>string</code>
            </td>
            <td></td>
        </tr><tr id="FlexibleFoo.i">
            <td><code>i</code></td>
            <td>
                <code>int32</code>
            </td>
            <td></td>
        </tr></table>

### StrictFoo {#StrictFoo}
*Defined in [fidl.test.json/request_flexible_envelope.test.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/request_flexible_envelope.test.fidl#8)*


<table>
    <tr><th>Name</th><th>Type</th><th>Description</th></tr><tr id="StrictFoo.s">
            <td><code>s</code></td>
            <td>
                <code>string</code>
            </td>
            <td></td>
        </tr><tr id="StrictFoo.i">
            <td><code>i</code></td>
            <td>
                <code>int32</code>
            </td>
            <td></td>
        </tr></table>







