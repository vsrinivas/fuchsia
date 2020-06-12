[TOC]

# fidl.test.handles


## **PROTOCOLS**

## SomeProtocol {#SomeProtocol}
*Defined in [fidl.test.handles/handles.test.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/handles.test.fidl#45)*




## **STRUCTS**

### Handles {#Handles}
*Defined in [fidl.test.handles/handles.test.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/handles.test.fidl#52)*



<table>
    <tr><th>Name</th><th>Type</th><th>Description</th><th>Default</th></tr><tr>
            <td><code>plain_handle</code></td>
            <td>
                <code>handle&lt;handle&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>bti_handle</code></td>
            <td>
                <code>handle&lt;bti&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>channel_handle</code></td>
            <td>
                <code>handle&lt;channel&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>clock_handle</code></td>
            <td>
                <code>handle&lt;clock&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>debuglog_handle</code></td>
            <td>
                <code>handle&lt;debuglog&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>event_handle</code></td>
            <td>
                <code>handle&lt;event&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>eventpair_handle</code></td>
            <td>
                <code>handle&lt;eventpair&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>exception_handle</code></td>
            <td>
                <code>handle&lt;exception&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>fifo_handle</code></td>
            <td>
                <code>handle&lt;fifo&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>guest_handle</code></td>
            <td>
                <code>handle&lt;guest&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>interrupt_handle</code></td>
            <td>
                <code>handle&lt;interrupt&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>iommu_handle</code></td>
            <td>
                <code>handle&lt;iommu&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>job_handle</code></td>
            <td>
                <code>handle&lt;job&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>pager_handle</code></td>
            <td>
                <code>handle&lt;pager&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>pcidevice_handle</code></td>
            <td>
                <code>handle&lt;pcidevice&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>pmt_handle</code></td>
            <td>
                <code>handle&lt;pmt&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>port_handle</code></td>
            <td>
                <code>handle&lt;port&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>process_handle</code></td>
            <td>
                <code>handle&lt;process&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>profile_handle</code></td>
            <td>
                <code>handle&lt;profile&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>resource_handle</code></td>
            <td>
                <code>handle&lt;resource&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>socket_handle</code></td>
            <td>
                <code>handle&lt;socket&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>suspendtoken_handle</code></td>
            <td>
                <code>handle&lt;suspendtoken&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>thread_handle</code></td>
            <td>
                <code>handle&lt;thread&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>timer_handle</code></td>
            <td>
                <code>handle&lt;timer&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>vcpu_handle</code></td>
            <td>
                <code>handle&lt;vcpu&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>vmar_handle</code></td>
            <td>
                <code>handle&lt;vmar&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>vmo_handle</code></td>
            <td>
                <code>handle&lt;vmo&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>rights_handle</code></td>
            <td>
                <code>handle&lt;vmo&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>aliased_plain_handle_field</code></td>
            <td>
                <code><a class='link' href='#aliased_plain_handle'>aliased_plain_handle</a></code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>aliased_subtype_handle_field</code></td>
            <td>
                <code><a class='link' href='#aliased_subtype_handle'>aliased_subtype_handle</a></code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>aliased_rights_handle_field</code></td>
            <td>
                <code><a class='link' href='#aliased_rights_handle'>aliased_rights_handle</a></code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>some_protocol</code></td>
            <td>
                <code><a class='link' href='#SomeProtocol'>SomeProtocol</a></code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr>
            <td><code>request_some_protocol</code></td>
            <td>
                <code>request&lt;<a class='link' href='#SomeProtocol'>SomeProtocol</a>&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr>
</table>



## **ENUMS**

### obj_type {#obj_type}
Type: <code>uint32</code>

*Defined in [fidl.test.handles/handles.test.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/handles.test.fidl#6)*



<table>
    <tr><th>Name</th><th>Value</th><th>Description</th></tr><tr>
            <td><code>NONE</code></td>
            <td><code>0</code></td>
            <td></td>
        </tr><tr>
            <td><code>PROCESS</code></td>
            <td><code>1</code></td>
            <td></td>
        </tr><tr>
            <td><code>THREAD</code></td>
            <td><code>2</code></td>
            <td></td>
        </tr><tr>
            <td><code>VMO</code></td>
            <td><code>3</code></td>
            <td></td>
        </tr><tr>
            <td><code>CHANNEL</code></td>
            <td><code>4</code></td>
            <td></td>
        </tr><tr>
            <td><code>EVENT</code></td>
            <td><code>5</code></td>
            <td></td>
        </tr><tr>
            <td><code>PORT</code></td>
            <td><code>6</code></td>
            <td></td>
        </tr><tr>
            <td><code>INTERRUPT</code></td>
            <td><code>9</code></td>
            <td></td>
        </tr><tr>
            <td><code>PCI_DEVICE</code></td>
            <td><code>11</code></td>
            <td></td>
        </tr><tr>
            <td><code>LOG</code></td>
            <td><code>12</code></td>
            <td></td>
        </tr><tr>
            <td><code>SOCKET</code></td>
            <td><code>14</code></td>
            <td></td>
        </tr><tr>
            <td><code>RESOURCE</code></td>
            <td><code>15</code></td>
            <td></td>
        </tr><tr>
            <td><code>EVENTPAIR</code></td>
            <td><code>16</code></td>
            <td></td>
        </tr><tr>
            <td><code>JOB</code></td>
            <td><code>17</code></td>
            <td></td>
        </tr><tr>
            <td><code>VMAR</code></td>
            <td><code>18</code></td>
            <td></td>
        </tr><tr>
            <td><code>FIFO</code></td>
            <td><code>19</code></td>
            <td></td>
        </tr><tr>
            <td><code>GUEST</code></td>
            <td><code>20</code></td>
            <td></td>
        </tr><tr>
            <td><code>VCPU</code></td>
            <td><code>21</code></td>
            <td></td>
        </tr><tr>
            <td><code>TIMER</code></td>
            <td><code>22</code></td>
            <td></td>
        </tr><tr>
            <td><code>IOMMU</code></td>
            <td><code>23</code></td>
            <td></td>
        </tr><tr>
            <td><code>BTI</code></td>
            <td><code>24</code></td>
            <td></td>
        </tr><tr>
            <td><code>PROFILE</code></td>
            <td><code>25</code></td>
            <td></td>
        </tr><tr>
            <td><code>PMT</code></td>
            <td><code>26</code></td>
            <td></td>
        </tr><tr>
            <td><code>SUSPEND_TOKEN</code></td>
            <td><code>27</code></td>
            <td></td>
        </tr><tr>
            <td><code>PAGER</code></td>
            <td><code>28</code></td>
            <td></td>
        </tr><tr>
            <td><code>EXCEPTION</code></td>
            <td><code>29</code></td>
            <td></td>
        </tr><tr>
            <td><code>CLOCK</code></td>
            <td><code>30</code></td>
            <td></td>
        </tr><tr>
            <td><code>STREAM</code></td>
            <td><code>31</code></td>
            <td></td>
        </tr><tr>
            <td><code>MSI_ALLOCATION</code></td>
            <td><code>32</code></td>
            <td></td>
        </tr><tr>
            <td><code>MSI_INTERRUPT</code></td>
            <td><code>33</code></td>
            <td></td>
        </tr></table>











## **TYPE ALIASES**

<table>
    <tr><th>Name</th><th>Value</th><th>Description</th></tr><tr id="aliased_plain_handle">
            <td><a href="https://fuchsia.googlesource.com/fuchsia/+/master/handles.test.fidl#48">aliased_plain_handle</a></td>
            <td>
                <code>handle</code></td>
            <td></td>
        </tr><tr id="aliased_subtype_handle">
            <td><a href="https://fuchsia.googlesource.com/fuchsia/+/master/handles.test.fidl#49">aliased_subtype_handle</a></td>
            <td>
                <code>handle</code></td>
            <td></td>
        </tr><tr id="aliased_rights_handle">
            <td><a href="https://fuchsia.googlesource.com/fuchsia/+/master/handles.test.fidl#50">aliased_rights_handle</a></td>
            <td>
                <code>handle</code></td>
            <td></td>
        </tr></table>

