[TOC]

# test.name


## **PROTOCOLS**

## WithAndWithoutRequestResponse {#WithAndWithoutRequestResponse}
*Defined in [test.name/protocols.test.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/protocols.test.fidl#17)*


### NoRequestNoResponse {#NoRequestNoResponse}


#### Request
<table>
    <tr><th>Name</th><th>Type</th></tr>
    </table>



### NoRequestEmptyResponse {#NoRequestEmptyResponse}


#### Request
<table>
    <tr><th>Name</th><th>Type</th></tr>
    </table>


#### Response
<table>
    <tr><th>Name</th><th>Type</th></tr>
    </table>

### NoRequestWithResponse {#NoRequestWithResponse}


#### Request
<table>
    <tr><th>Name</th><th>Type</th></tr>
    </table>


#### Response
<table>
    <tr><th>Name</th><th>Type</th></tr>
    <tr>
            <td><code>ret</code></td>
            <td>
                <code>string</code>
            </td>
        </tr></table>

### WithRequestNoResponse {#WithRequestNoResponse}


#### Request
<table>
    <tr><th>Name</th><th>Type</th></tr>
    <tr>
            <td><code>arg</code></td>
            <td>
                <code>string</code>
            </td>
        </tr></table>



### WithRequestEmptyResponse {#WithRequestEmptyResponse}


#### Request
<table>
    <tr><th>Name</th><th>Type</th></tr>
    <tr>
            <td><code>arg</code></td>
            <td>
                <code>string</code>
            </td>
        </tr></table>


#### Response
<table>
    <tr><th>Name</th><th>Type</th></tr>
    </table>

### WithRequestWithResponse {#WithRequestWithResponse}


#### Request
<table>
    <tr><th>Name</th><th>Type</th></tr>
    <tr>
            <td><code>arg</code></td>
            <td>
                <code>string</code>
            </td>
        </tr></table>


#### Response
<table>
    <tr><th>Name</th><th>Type</th></tr>
    <tr>
            <td><code>ret</code></td>
            <td>
                <code>string</code>
            </td>
        </tr></table>

### OnEmptyResponse {#OnEmptyResponse}




#### Response
<table>
    <tr><th>Name</th><th>Type</th></tr>
    </table>

### OnWithResponse {#OnWithResponse}




#### Response
<table>
    <tr><th>Name</th><th>Type</th></tr>
    <tr>
            <td><code>ret</code></td>
            <td>
                <code>string</code>
            </td>
        </tr></table>

## WithErrorSyntax {#WithErrorSyntax}
*Defined in [test.name/protocols.test.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/protocols.test.fidl#33)*


### ResponseAsStruct {#ResponseAsStruct}


#### Request
<table>
    <tr><th>Name</th><th>Type</th></tr>
    </table>


#### Response
<table>
    <tr><th>Name</th><th>Type</th></tr>
    <tr>
            <td><code>result</code></td>
            <td>
                <code><a class='link' href='#WithErrorSyntax_ResponseAsStruct_Result'>WithErrorSyntax_ResponseAsStruct_Result</a></code>
            </td>
        </tr></table>

### ErrorAsPrimitive {#ErrorAsPrimitive}


#### Request
<table>
    <tr><th>Name</th><th>Type</th></tr>
    </table>


#### Response
<table>
    <tr><th>Name</th><th>Type</th></tr>
    <tr>
            <td><code>result</code></td>
            <td>
                <code><a class='link' href='#WithErrorSyntax_ErrorAsPrimitive_Result'>WithErrorSyntax_ErrorAsPrimitive_Result</a></code>
            </td>
        </tr></table>

### ErrorAsEnum {#ErrorAsEnum}


#### Request
<table>
    <tr><th>Name</th><th>Type</th></tr>
    </table>


#### Response
<table>
    <tr><th>Name</th><th>Type</th></tr>
    <tr>
            <td><code>result</code></td>
            <td>
                <code><a class='link' href='#WithErrorSyntax_ErrorAsEnum_Result'>WithErrorSyntax_ErrorAsEnum_Result</a></code>
            </td>
        </tr></table>

## ChannelProtocol {#ChannelProtocol}
*Defined in [test.name/protocols.test.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/protocols.test.fidl#40)*


### MethodA {#MethodA}


#### Request
<table>
    <tr><th>Name</th><th>Type</th></tr>
    <tr>
            <td><code>a</code></td>
            <td>
                <code>int64</code>
            </td>
        </tr><tr>
            <td><code>b</code></td>
            <td>
                <code>int64</code>
            </td>
        </tr></table>



### EventA {#EventA}




#### Response
<table>
    <tr><th>Name</th><th>Type</th></tr>
    <tr>
            <td><code>a</code></td>
            <td>
                <code>int64</code>
            </td>
        </tr><tr>
            <td><code>b</code></td>
            <td>
                <code>int64</code>
            </td>
        </tr></table>

### MethodB {#MethodB}


#### Request
<table>
    <tr><th>Name</th><th>Type</th></tr>
    <tr>
            <td><code>a</code></td>
            <td>
                <code>int64</code>
            </td>
        </tr><tr>
            <td><code>b</code></td>
            <td>
                <code>int64</code>
            </td>
        </tr></table>


#### Response
<table>
    <tr><th>Name</th><th>Type</th></tr>
    <tr>
            <td><code>result</code></td>
            <td>
                <code>int64</code>
            </td>
        </tr></table>

### MutateSocket {#MutateSocket}


#### Request
<table>
    <tr><th>Name</th><th>Type</th></tr>
    <tr>
            <td><code>a</code></td>
            <td>
                <code>handle&lt;socket&gt;</code>
            </td>
        </tr></table>


#### Response
<table>
    <tr><th>Name</th><th>Type</th></tr>
    <tr>
            <td><code>b</code></td>
            <td>
                <code>handle&lt;socket&gt;</code>
            </td>
        </tr></table>

## Transitional {#Transitional}
*Defined in [test.name/protocols.test.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/protocols.test.fidl#47)*


### Request {#Request}


#### Request
<table>
    <tr><th>Name</th><th>Type</th></tr>
    <tr>
            <td><code>x</code></td>
            <td>
                <code>int64</code>
            </td>
        </tr></table>


#### Response
<table>
    <tr><th>Name</th><th>Type</th></tr>
    <tr>
            <td><code>y</code></td>
            <td>
                <code>int64</code>
            </td>
        </tr></table>

### OneWay {#OneWay}


#### Request
<table>
    <tr><th>Name</th><th>Type</th></tr>
    <tr>
            <td><code>x</code></td>
            <td>
                <code>int64</code>
            </td>
        </tr></table>



### Event {#Event}




#### Response
<table>
    <tr><th>Name</th><th>Type</th></tr>
    <tr>
            <td><code>x</code></td>
            <td>
                <code>int64</code>
            </td>
        </tr></table>



## **STRUCTS**

### WithErrorSyntax_ResponseAsStruct_Response {#WithErrorSyntax_ResponseAsStruct_Response}
*Defined in [test.name/protocols.test.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/protocols.test.fidl#34)*



<table>
    <tr><th>Name</th><th>Type</th><th>Description</th><th>Default</th></tr><tr>
            <td><code>a</code></td>
            <td>
                <code>int64</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>b</code></td>
            <td>
                <code>int64</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>c</code></td>
            <td>
                <code>int64</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr>
</table>

### WithErrorSyntax_ErrorAsPrimitive_Response {#WithErrorSyntax_ErrorAsPrimitive_Response}
*Defined in [test.name/protocols.test.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/protocols.test.fidl#35)*



<table>
    <tr><th>Name</th><th>Type</th><th>Description</th><th>Default</th></tr>
</table>

### WithErrorSyntax_ErrorAsEnum_Response {#WithErrorSyntax_ErrorAsEnum_Response}
*Defined in [test.name/protocols.test.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/protocols.test.fidl#36)*



<table>
    <tr><th>Name</th><th>Type</th><th>Description</th><th>Default</th></tr>
</table>



## **ENUMS**

### obj_type {#obj_type}
Type: <code>uint32</code>

*Defined in [test.name/protocols.test.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/protocols.test.fidl#6)*



<table>
    <tr><th>Name</th><th>Value</th><th>Description</th></tr><tr>
            <td><code>NONE</code></td>
            <td><code>0</code></td>
            <td></td>
        </tr><tr>
            <td><code>SOCKET</code></td>
            <td><code>14</code></td>
            <td></td>
        </tr></table>

### ErrorEnun {#ErrorEnun}
Type: <code>uint32</code>

*Defined in [test.name/protocols.test.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/protocols.test.fidl#28)*



<table>
    <tr><th>Name</th><th>Value</th><th>Description</th></tr><tr>
            <td><code>ERR_FOO</code></td>
            <td><code>1</code></td>
            <td></td>
        </tr><tr>
            <td><code>ERR_BAR</code></td>
            <td><code>2</code></td>
            <td></td>
        </tr></table>





## **UNIONS**

### WithErrorSyntax_ResponseAsStruct_Result {#WithErrorSyntax_ResponseAsStruct_Result}
*Defined in [test.name/protocols.test.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/protocols.test.fidl#34)*


<table>
    <tr><th>Name</th><th>Type</th><th>Description</th></tr><tr>
            <td><code>response</code></td>
            <td>
                <code><a class='link' href='#WithErrorSyntax_ResponseAsStruct_Response'>WithErrorSyntax_ResponseAsStruct_Response</a></code>
            </td>
            <td></td>
        </tr><tr>
            <td><code>err</code></td>
            <td>
                <code>uint32</code>
            </td>
            <td></td>
        </tr></table>

### WithErrorSyntax_ErrorAsPrimitive_Result {#WithErrorSyntax_ErrorAsPrimitive_Result}
*Defined in [test.name/protocols.test.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/protocols.test.fidl#35)*


<table>
    <tr><th>Name</th><th>Type</th><th>Description</th></tr><tr>
            <td><code>response</code></td>
            <td>
                <code><a class='link' href='#WithErrorSyntax_ErrorAsPrimitive_Response'>WithErrorSyntax_ErrorAsPrimitive_Response</a></code>
            </td>
            <td></td>
        </tr><tr>
            <td><code>err</code></td>
            <td>
                <code>uint32</code>
            </td>
            <td></td>
        </tr></table>

### WithErrorSyntax_ErrorAsEnum_Result {#WithErrorSyntax_ErrorAsEnum_Result}
*Defined in [test.name/protocols.test.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/protocols.test.fidl#36)*


<table>
    <tr><th>Name</th><th>Type</th><th>Description</th></tr><tr>
            <td><code>response</code></td>
            <td>
                <code><a class='link' href='#WithErrorSyntax_ErrorAsEnum_Response'>WithErrorSyntax_ErrorAsEnum_Response</a></code>
            </td>
            <td></td>
        </tr><tr>
            <td><code>err</code></td>
            <td>
                <code><a class='link' href='#ErrorEnun'>ErrorEnun</a></code>
            </td>
            <td></td>
        </tr></table>







