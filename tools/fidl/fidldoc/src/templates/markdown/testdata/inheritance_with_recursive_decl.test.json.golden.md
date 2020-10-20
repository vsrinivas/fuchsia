[TOC]

# fidl.test.inheritancewithrecursivedecl


## **PROTOCOLS**

## Parent {#Parent}
*Defined in [fidl.test.inheritancewithrecursivedecl/inheritance_with_recursive_decl.test.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/inheritance_with_recursive_decl.test.fidl#3)*


### First {#fidl.test.inheritancewithrecursivedecl/Parent.First}


#### Request
<table>
    <tr><th>Name</th><th>Type</th></tr>
    <tr>
            <td><code>request</code></td>
            <td>
                <code>request&lt;<a class='link' href='#Parent'>Parent</a>&gt;</code>
            </td>
        </tr></table>



## Child {#Child}
*Defined in [fidl.test.inheritancewithrecursivedecl/inheritance_with_recursive_decl.test.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/inheritance_with_recursive_decl.test.fidl#7)*


### First {#fidl.test.inheritancewithrecursivedecl/Child.First}


#### Request
<table>
    <tr><th>Name</th><th>Type</th></tr>
    <tr>
            <td><code>request</code></td>
            <td>
                <code>request&lt;<a class='link' href='#Parent'>Parent</a>&gt;</code>
            </td>
        </tr></table>



### Second {#fidl.test.inheritancewithrecursivedecl/Child.Second}


#### Request
<table>
    <tr><th>Name</th><th>Type</th></tr>
    <tr>
            <td><code>request</code></td>
            <td>
                <code>request&lt;<a class='link' href='#Parent'>Parent</a>&gt;</code>
            </td>
        </tr></table>





## **STRUCTS**













