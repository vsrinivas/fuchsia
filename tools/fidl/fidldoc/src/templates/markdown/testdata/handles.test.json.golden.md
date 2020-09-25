[TOC]

# fidl.test.handles


## **PROTOCOLS**

## SomeProtocol {#SomeProtocol}
*Defined in [fidl.test.handles/handles.test.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/handles.test.fidl#45)*




## **STRUCTS**

### Handles {#Handles}
*Defined in [fidl.test.handles/handles.test.fidl](https://fuchsia.googlesource.com/fuchsia/+/master/handles.test.fidl#52)*



<table>
    <tr><th>Name</th><th>Type</th><th>Description</th><th>Default</th></tr><tr id="Handles.plain_handle">
            <td><code>plain_handle</code></td>
            <td>
                <code>handle&lt;handle&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr id="Handles.bti_handle">
            <td><code>bti_handle</code></td>
            <td>
                <code>handle&lt;bti&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr id="Handles.channel_handle">
            <td><code>channel_handle</code></td>
            <td>
                <code>handle&lt;channel&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr id="Handles.clock_handle">
            <td><code>clock_handle</code></td>
            <td>
                <code>handle&lt;clock&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr id="Handles.debuglog_handle">
            <td><code>debuglog_handle</code></td>
            <td>
                <code>handle&lt;debuglog&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr id="Handles.event_handle">
            <td><code>event_handle</code></td>
            <td>
                <code>handle&lt;event&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr id="Handles.eventpair_handle">
            <td><code>eventpair_handle</code></td>
            <td>
                <code>handle&lt;eventpair&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr id="Handles.exception_handle">
            <td><code>exception_handle</code></td>
            <td>
                <code>handle&lt;exception&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr id="Handles.fifo_handle">
            <td><code>fifo_handle</code></td>
            <td>
                <code>handle&lt;fifo&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr id="Handles.guest_handle">
            <td><code>guest_handle</code></td>
            <td>
                <code>handle&lt;guest&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr id="Handles.interrupt_handle">
            <td><code>interrupt_handle</code></td>
            <td>
                <code>handle&lt;interrupt&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr id="Handles.iommu_handle">
            <td><code>iommu_handle</code></td>
            <td>
                <code>handle&lt;iommu&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr id="Handles.job_handle">
            <td><code>job_handle</code></td>
            <td>
                <code>handle&lt;job&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr id="Handles.pager_handle">
            <td><code>pager_handle</code></td>
            <td>
                <code>handle&lt;pager&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr id="Handles.pcidevice_handle">
            <td><code>pcidevice_handle</code></td>
            <td>
                <code>handle&lt;pcidevice&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr id="Handles.pmt_handle">
            <td><code>pmt_handle</code></td>
            <td>
                <code>handle&lt;pmt&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr id="Handles.port_handle">
            <td><code>port_handle</code></td>
            <td>
                <code>handle&lt;port&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr id="Handles.process_handle">
            <td><code>process_handle</code></td>
            <td>
                <code>handle&lt;process&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr id="Handles.profile_handle">
            <td><code>profile_handle</code></td>
            <td>
                <code>handle&lt;profile&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr id="Handles.resource_handle">
            <td><code>resource_handle</code></td>
            <td>
                <code>handle&lt;resource&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr id="Handles.socket_handle">
            <td><code>socket_handle</code></td>
            <td>
                <code>handle&lt;socket&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr id="Handles.suspendtoken_handle">
            <td><code>suspendtoken_handle</code></td>
            <td>
                <code>handle&lt;suspendtoken&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr id="Handles.thread_handle">
            <td><code>thread_handle</code></td>
            <td>
                <code>handle&lt;thread&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr id="Handles.timer_handle">
            <td><code>timer_handle</code></td>
            <td>
                <code>handle&lt;timer&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr id="Handles.vcpu_handle">
            <td><code>vcpu_handle</code></td>
            <td>
                <code>handle&lt;vcpu&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr id="Handles.vmar_handle">
            <td><code>vmar_handle</code></td>
            <td>
                <code>handle&lt;vmar&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr id="Handles.vmo_handle">
            <td><code>vmo_handle</code></td>
            <td>
                <code>handle&lt;vmo&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr id="Handles.rights_handle">
            <td><code>rights_handle</code></td>
            <td>
                <code>handle&lt;vmo&gt;</code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr id="Handles.aliased_plain_handle_field">
            <td><code>aliased_plain_handle_field</code></td>
            <td>
                <code><a class='link' href='#aliased_plain_handle'>aliased_plain_handle</a></code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr id="Handles.aliased_subtype_handle_field">
            <td><code>aliased_subtype_handle_field</code></td>
            <td>
                <code><a class='link' href='#aliased_subtype_handle'>aliased_subtype_handle</a></code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr id="Handles.aliased_rights_handle_field">
            <td><code>aliased_rights_handle_field</code></td>
            <td>
                <code><a class='link' href='#aliased_rights_handle'>aliased_rights_handle</a></code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr id="Handles.some_protocol">
            <td><code>some_protocol</code></td>
            <td>
                <code><a class='link' href='#SomeProtocol'>SomeProtocol</a></code>
            </td>
            <td></td>
            <td>No default</td>
        </tr><tr id="Handles.request_some_protocol">
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
    <tr><th>Name</th><th>Value</th><th>Description</th></tr><tr id="obj_type.NONE">
            <td><code>NONE</code></td>
            <td><code>0</code></td>
            <td></td>
        </tr><tr id="obj_type.PROCESS">
            <td><code>PROCESS</code></td>
            <td><code>1</code></td>
            <td></td>
        </tr><tr id="obj_type.THREAD">
            <td><code>THREAD</code></td>
            <td><code>2</code></td>
            <td></td>
        </tr><tr id="obj_type.VMO">
            <td><code>VMO</code></td>
            <td><code>3</code></td>
            <td></td>
        </tr><tr id="obj_type.CHANNEL">
            <td><code>CHANNEL</code></td>
            <td><code>4</code></td>
            <td></td>
        </tr><tr id="obj_type.EVENT">
            <td><code>EVENT</code></td>
            <td><code>5</code></td>
            <td></td>
        </tr><tr id="obj_type.PORT">
            <td><code>PORT</code></td>
            <td><code>6</code></td>
            <td></td>
        </tr><tr id="obj_type.INTERRUPT">
            <td><code>INTERRUPT</code></td>
            <td><code>9</code></td>
            <td></td>
        </tr><tr id="obj_type.PCI_DEVICE">
            <td><code>PCI_DEVICE</code></td>
            <td><code>11</code></td>
            <td></td>
        </tr><tr id="obj_type.LOG">
            <td><code>LOG</code></td>
            <td><code>12</code></td>
            <td></td>
        </tr><tr id="obj_type.SOCKET">
            <td><code>SOCKET</code></td>
            <td><code>14</code></td>
            <td></td>
        </tr><tr id="obj_type.RESOURCE">
            <td><code>RESOURCE</code></td>
            <td><code>15</code></td>
            <td></td>
        </tr><tr id="obj_type.EVENTPAIR">
            <td><code>EVENTPAIR</code></td>
            <td><code>16</code></td>
            <td></td>
        </tr><tr id="obj_type.JOB">
            <td><code>JOB</code></td>
            <td><code>17</code></td>
            <td></td>
        </tr><tr id="obj_type.VMAR">
            <td><code>VMAR</code></td>
            <td><code>18</code></td>
            <td></td>
        </tr><tr id="obj_type.FIFO">
            <td><code>FIFO</code></td>
            <td><code>19</code></td>
            <td></td>
        </tr><tr id="obj_type.GUEST">
            <td><code>GUEST</code></td>
            <td><code>20</code></td>
            <td></td>
        </tr><tr id="obj_type.VCPU">
            <td><code>VCPU</code></td>
            <td><code>21</code></td>
            <td></td>
        </tr><tr id="obj_type.TIMER">
            <td><code>TIMER</code></td>
            <td><code>22</code></td>
            <td></td>
        </tr><tr id="obj_type.IOMMU">
            <td><code>IOMMU</code></td>
            <td><code>23</code></td>
            <td></td>
        </tr><tr id="obj_type.BTI">
            <td><code>BTI</code></td>
            <td><code>24</code></td>
            <td></td>
        </tr><tr id="obj_type.PROFILE">
            <td><code>PROFILE</code></td>
            <td><code>25</code></td>
            <td></td>
        </tr><tr id="obj_type.PMT">
            <td><code>PMT</code></td>
            <td><code>26</code></td>
            <td></td>
        </tr><tr id="obj_type.SUSPEND_TOKEN">
            <td><code>SUSPEND_TOKEN</code></td>
            <td><code>27</code></td>
            <td></td>
        </tr><tr id="obj_type.PAGER">
            <td><code>PAGER</code></td>
            <td><code>28</code></td>
            <td></td>
        </tr><tr id="obj_type.EXCEPTION">
            <td><code>EXCEPTION</code></td>
            <td><code>29</code></td>
            <td></td>
        </tr><tr id="obj_type.CLOCK">
            <td><code>CLOCK</code></td>
            <td><code>30</code></td>
            <td></td>
        </tr><tr id="obj_type.STREAM">
            <td><code>STREAM</code></td>
            <td><code>31</code></td>
            <td></td>
        </tr><tr id="obj_type.MSI_ALLOCATION">
            <td><code>MSI_ALLOCATION</code></td>
            <td><code>32</code></td>
            <td></td>
        </tr><tr id="obj_type.MSI_INTERRUPT">
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

