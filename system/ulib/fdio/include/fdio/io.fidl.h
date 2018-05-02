#pragma once

#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <lib/fidl/coding.h>
#include <zircon/fidl.h>
#include <zircon/syscalls/object.h>
#include <zircon/types.h>

#if defined(__cplusplus)
extern "C" {
#endif


// Forward declarations

typedef struct NodeAttributes NodeAttributes;
#define NodeSyncOrdinal ((uint32_t)2164260865)
typedef struct ioNodeSyncRequest ioNodeSyncRequest;
typedef struct ioNodeSyncResponse ioNodeSyncResponse;
#define NodeGetAttrOrdinal ((uint32_t)2164260866)
typedef struct ioNodeGetAttrRequest ioNodeGetAttrRequest;
typedef struct ioNodeGetAttrResponse ioNodeGetAttrResponse;
#define NodeSetAttrOrdinal ((uint32_t)2164260867)
typedef struct ioNodeSetAttrRequest ioNodeSetAttrRequest;
typedef struct ioNodeSetAttrResponse ioNodeSetAttrResponse;
#define NodeIoctlOrdinal ((uint32_t)2164260868)
typedef struct ioNodeIoctlRequest ioNodeIoctlRequest;
typedef struct ioNodeIoctlResponse ioNodeIoctlResponse;
typedef struct Device Device;
typedef struct Vmofile Vmofile;
typedef struct Pipe Pipe;
typedef struct DirectoryObject DirectoryObject;
typedef struct FileObject FileObject;
#define DirectoryOpenOrdinal ((uint32_t)2197815297)
typedef struct ioDirectoryOpenRequest ioDirectoryOpenRequest;
#define DirectoryUnlinkOrdinal ((uint32_t)2197815298)
typedef struct ioDirectoryUnlinkRequest ioDirectoryUnlinkRequest;
typedef struct ioDirectoryUnlinkResponse ioDirectoryUnlinkResponse;
#define DirectoryReadDirentsOrdinal ((uint32_t)2197815299)
typedef struct ioDirectoryReadDirentsRequest ioDirectoryReadDirentsRequest;
typedef struct ioDirectoryReadDirentsResponse ioDirectoryReadDirentsResponse;
#define DirectoryRewindOrdinal ((uint32_t)2197815300)
typedef struct ioDirectoryRewindRequest ioDirectoryRewindRequest;
typedef struct ioDirectoryRewindResponse ioDirectoryRewindResponse;
#define DirectoryGetTokenOrdinal ((uint32_t)2197815301)
typedef struct ioDirectoryGetTokenRequest ioDirectoryGetTokenRequest;
typedef struct ioDirectoryGetTokenResponse ioDirectoryGetTokenResponse;
#define DirectoryRenameOrdinal ((uint32_t)2197815302)
typedef struct ioDirectoryRenameRequest ioDirectoryRenameRequest;
typedef struct ioDirectoryRenameResponse ioDirectoryRenameResponse;
#define DirectoryLinkOrdinal ((uint32_t)2197815303)
typedef struct ioDirectoryLinkRequest ioDirectoryLinkRequest;
typedef struct ioDirectoryLinkResponse ioDirectoryLinkResponse;
typedef struct Service Service;
typedef struct ObjectInfo ObjectInfo;
#define ObjectCloneOrdinal ((uint32_t)2147483649)
typedef struct ioObjectCloneRequest ioObjectCloneRequest;
#define ObjectCloseOrdinal ((uint32_t)2147483650)
typedef struct ioObjectCloseRequest ioObjectCloseRequest;
typedef struct ioObjectCloseResponse ioObjectCloseResponse;
#define ObjectListInterfacesOrdinal ((uint32_t)2147483652)
typedef struct ioObjectListInterfacesRequest ioObjectListInterfacesRequest;
typedef struct ioObjectListInterfacesResponse ioObjectListInterfacesResponse;
#define ObjectBindOrdinal ((uint32_t)2147483653)
typedef struct ioObjectBindRequest ioObjectBindRequest;
#define ObjectDescribeOrdinal ((uint32_t)2147483654)
typedef struct ioObjectDescribeRequest ioObjectDescribeRequest;
typedef struct ioObjectDescribeResponse ioObjectDescribeResponse;
#define ObjectOnOpenOrdinal ((uint32_t)2147483655)
typedef struct ioObjectOnOpenEvent ioObjectOnOpenEvent;
typedef uint32_t SeekOrigin;
#define SeekOrigin_Start UINT32_C(0)
#define SeekOrigin_Current UINT32_C(1)
#define SeekOrigin_End UINT32_C(2)

#define FileReadOrdinal ((uint32_t)2181038081)
typedef struct ioFileReadRequest ioFileReadRequest;
typedef struct ioFileReadResponse ioFileReadResponse;
#define FileReadAtOrdinal ((uint32_t)2181038082)
typedef struct ioFileReadAtRequest ioFileReadAtRequest;
typedef struct ioFileReadAtResponse ioFileReadAtResponse;
#define FileWriteOrdinal ((uint32_t)2181038083)
typedef struct ioFileWriteRequest ioFileWriteRequest;
typedef struct ioFileWriteResponse ioFileWriteResponse;
#define FileWriteAtOrdinal ((uint32_t)2181038084)
typedef struct ioFileWriteAtRequest ioFileWriteAtRequest;
typedef struct ioFileWriteAtResponse ioFileWriteAtResponse;
#define FileSeekOrdinal ((uint32_t)2181038085)
typedef struct ioFileSeekRequest ioFileSeekRequest;
typedef struct ioFileSeekResponse ioFileSeekResponse;
#define FileTruncateOrdinal ((uint32_t)2181038086)
typedef struct ioFileTruncateRequest ioFileTruncateRequest;
typedef struct ioFileTruncateResponse ioFileTruncateResponse;
#define FileGetFlagsOrdinal ((uint32_t)2181038087)
typedef struct ioFileGetFlagsRequest ioFileGetFlagsRequest;
typedef struct ioFileGetFlagsResponse ioFileGetFlagsResponse;
#define FileSetFlagsOrdinal ((uint32_t)2181038088)
typedef struct ioFileSetFlagsRequest ioFileSetFlagsRequest;
typedef struct ioFileSetFlagsResponse ioFileSetFlagsResponse;
#define FileGetVmoOrdinal ((uint32_t)2181038089)
typedef struct ioFileGetVmoRequest ioFileGetVmoRequest;
typedef struct ioFileGetVmoResponse ioFileGetVmoResponse;
#define FileGetVmoAtOrdinal ((uint32_t)2181038090)
typedef struct ioFileGetVmoAtRequest ioFileGetVmoAtRequest;
typedef struct ioFileGetVmoAtResponse ioFileGetVmoAtResponse;

// Extern declarations

extern const fidl_type_t ioNodeSyncRequestTable;
extern const fidl_type_t ioNodeSyncResponseTable;
extern const fidl_type_t ioNodeGetAttrRequestTable;
extern const fidl_type_t ioNodeGetAttrResponseTable;
extern const fidl_type_t ioNodeSetAttrRequestTable;
extern const fidl_type_t ioNodeSetAttrResponseTable;
extern const fidl_type_t ioNodeIoctlRequestTable;
extern const fidl_type_t ioNodeIoctlResponseTable;
extern const fidl_type_t ioDirectoryOpenRequestTable;
extern const fidl_type_t ioDirectoryUnlinkRequestTable;
extern const fidl_type_t ioDirectoryUnlinkResponseTable;
extern const fidl_type_t ioDirectoryReadDirentsRequestTable;
extern const fidl_type_t ioDirectoryReadDirentsResponseTable;
extern const fidl_type_t ioDirectoryRewindRequestTable;
extern const fidl_type_t ioDirectoryRewindResponseTable;
extern const fidl_type_t ioDirectoryGetTokenRequestTable;
extern const fidl_type_t ioDirectoryGetTokenResponseTable;
extern const fidl_type_t ioDirectoryRenameRequestTable;
extern const fidl_type_t ioDirectoryRenameResponseTable;
extern const fidl_type_t ioDirectoryLinkRequestTable;
extern const fidl_type_t ioDirectoryLinkResponseTable;
extern const fidl_type_t ioObjectCloneRequestTable;
extern const fidl_type_t ioObjectCloseRequestTable;
extern const fidl_type_t ioObjectCloseResponseTable;
extern const fidl_type_t ioObjectListInterfacesRequestTable;
extern const fidl_type_t ioObjectListInterfacesResponseTable;
extern const fidl_type_t ioObjectBindRequestTable;
extern const fidl_type_t ioObjectDescribeRequestTable;
extern const fidl_type_t ioObjectDescribeResponseTable;
extern const fidl_type_t ioObjectOnOpenEventTable;
extern const fidl_type_t ioFileReadRequestTable;
extern const fidl_type_t ioFileReadResponseTable;
extern const fidl_type_t ioFileReadAtRequestTable;
extern const fidl_type_t ioFileReadAtResponseTable;
extern const fidl_type_t ioFileWriteRequestTable;
extern const fidl_type_t ioFileWriteResponseTable;
extern const fidl_type_t ioFileWriteAtRequestTable;
extern const fidl_type_t ioFileWriteAtResponseTable;
extern const fidl_type_t ioFileSeekRequestTable;
extern const fidl_type_t ioFileSeekResponseTable;
extern const fidl_type_t ioFileTruncateRequestTable;
extern const fidl_type_t ioFileTruncateResponseTable;
extern const fidl_type_t ioFileGetFlagsRequestTable;
extern const fidl_type_t ioFileGetFlagsResponseTable;
extern const fidl_type_t ioFileSetFlagsRequestTable;
extern const fidl_type_t ioFileSetFlagsResponseTable;
extern const fidl_type_t ioFileGetVmoRequestTable;
extern const fidl_type_t ioFileGetVmoResponseTable;
extern const fidl_type_t ioFileGetVmoAtRequestTable;
extern const fidl_type_t ioFileGetVmoAtResponseTable;

// Declarations

struct NodeAttributes {
    FIDL_ALIGNDECL
    uint32_t mode;
    uint64_t id;
    uint64_t content_size;
    uint64_t storage_size;
    uint64_t link_count;
    uint64_t creation_time;
    uint64_t modification_time;
};

struct ioNodeSyncRequest {
    FIDL_ALIGNDECL
    fidl_message_header_t hdr;
};

struct ioNodeSyncResponse {
    FIDL_ALIGNDECL
    fidl_message_header_t hdr;
    zx_status_t s;
};

struct ioNodeGetAttrRequest {
    FIDL_ALIGNDECL
    fidl_message_header_t hdr;
};

struct ioNodeGetAttrResponse {
    FIDL_ALIGNDECL
    fidl_message_header_t hdr;
    zx_status_t s;
    NodeAttributes attributes;
};

struct ioNodeSetAttrRequest {
    FIDL_ALIGNDECL
    fidl_message_header_t hdr;
    uint32_t flags;
    NodeAttributes attributes;
};

struct ioNodeSetAttrResponse {
    FIDL_ALIGNDECL
    fidl_message_header_t hdr;
    zx_status_t s;
};

struct ioNodeIoctlRequest {
    FIDL_ALIGNDECL
    fidl_message_header_t hdr;
    uint32_t opcode;
    uint64_t max_out;
    fidl_vector_t handles;
    fidl_vector_t in;
};

struct ioNodeIoctlResponse {
    FIDL_ALIGNDECL
    fidl_message_header_t hdr;
    zx_status_t s;
    fidl_vector_t handles;
    fidl_vector_t out;
};

struct Device {
    FIDL_ALIGNDECL
    zx_handle_t event;
};

struct Vmofile {
    FIDL_ALIGNDECL
    zx_handle_t vmo;
    uint64_t offset;
    uint64_t length;
};

struct Pipe {
    FIDL_ALIGNDECL
    zx_handle_t socket;
};

struct DirectoryObject {
    FIDL_ALIGNDECL
    uint8_t reserved;
};

struct FileObject {
    FIDL_ALIGNDECL
    zx_handle_t event;
};

struct ioDirectoryOpenRequest {
    FIDL_ALIGNDECL
    fidl_message_header_t hdr;
    uint32_t flags;
    uint32_t mode;
    fidl_string_t path;
    zx_handle_t object;
};

struct ioDirectoryUnlinkRequest {
    FIDL_ALIGNDECL
    fidl_message_header_t hdr;
    fidl_string_t path;
};

struct ioDirectoryUnlinkResponse {
    FIDL_ALIGNDECL
    fidl_message_header_t hdr;
    zx_status_t s;
};

struct ioDirectoryReadDirentsRequest {
    FIDL_ALIGNDECL
    fidl_message_header_t hdr;
    uint64_t max_out;
};

struct ioDirectoryReadDirentsResponse {
    FIDL_ALIGNDECL
    fidl_message_header_t hdr;
    zx_status_t s;
    fidl_vector_t dirents;
};

struct ioDirectoryRewindRequest {
    FIDL_ALIGNDECL
    fidl_message_header_t hdr;
};

struct ioDirectoryRewindResponse {
    FIDL_ALIGNDECL
    fidl_message_header_t hdr;
    zx_status_t s;
};

struct ioDirectoryGetTokenRequest {
    FIDL_ALIGNDECL
    fidl_message_header_t hdr;
};

struct ioDirectoryGetTokenResponse {
    FIDL_ALIGNDECL
    fidl_message_header_t hdr;
    zx_status_t s;
    zx_handle_t token;
};

struct ioDirectoryRenameRequest {
    FIDL_ALIGNDECL
    fidl_message_header_t hdr;
    fidl_string_t src;
    zx_handle_t dst_parent_token;
    fidl_string_t dst;
};

struct ioDirectoryRenameResponse {
    FIDL_ALIGNDECL
    fidl_message_header_t hdr;
    zx_status_t s;
};

struct ioDirectoryLinkRequest {
    FIDL_ALIGNDECL
    fidl_message_header_t hdr;
    fidl_string_t src;
    zx_handle_t dst_parent_token;
    fidl_string_t dst;
};

struct ioDirectoryLinkResponse {
    FIDL_ALIGNDECL
    fidl_message_header_t hdr;
    zx_status_t s;
};

struct Service {
    FIDL_ALIGNDECL
    uint8_t reserved;
};

struct ObjectInfo {
    FIDL_ALIGNDECL
    fidl_union_tag_t tag;
    union {
        Service service;
        FileObject file;
        DirectoryObject directory;
        Pipe pipe;
        Vmofile vmofile;
        Device device;
    };
};
#define ObjectInfoTagservice UINT32_C(0)
#define ObjectInfoTagfile UINT32_C(1)
#define ObjectInfoTagdirectory UINT32_C(2)
#define ObjectInfoTagpipe UINT32_C(3)
#define ObjectInfoTagvmofile UINT32_C(4)
#define ObjectInfoTagdevice UINT32_C(5)

struct ioObjectCloneRequest {
    FIDL_ALIGNDECL
    fidl_message_header_t hdr;
    uint32_t flags;
    zx_handle_t object;
};

struct ioObjectCloseRequest {
    FIDL_ALIGNDECL
    fidl_message_header_t hdr;
};

struct ioObjectCloseResponse {
    FIDL_ALIGNDECL
    fidl_message_header_t hdr;
    zx_status_t s;
};

struct ioObjectListInterfacesRequest {
    FIDL_ALIGNDECL
    fidl_message_header_t hdr;
};

struct ioObjectListInterfacesResponse {
    FIDL_ALIGNDECL
    fidl_message_header_t hdr;
    fidl_vector_t interfaces;
};

struct ioObjectBindRequest {
    FIDL_ALIGNDECL
    fidl_message_header_t hdr;
    fidl_string_t iface;
};

struct ioObjectDescribeRequest {
    FIDL_ALIGNDECL
    fidl_message_header_t hdr;
};

struct ioObjectDescribeResponse {
    FIDL_ALIGNDECL
    fidl_message_header_t hdr;
    ObjectInfo info;
};

struct ioObjectOnOpenEvent {
    FIDL_ALIGNDECL
    fidl_message_header_t hdr;
    zx_status_t s;
    ObjectInfo* info;
};

struct ioFileReadRequest {
    FIDL_ALIGNDECL
    fidl_message_header_t hdr;
    uint64_t count;
};

struct ioFileReadResponse {
    FIDL_ALIGNDECL
    fidl_message_header_t hdr;
    zx_status_t s;
    fidl_vector_t data;
};

struct ioFileReadAtRequest {
    FIDL_ALIGNDECL
    fidl_message_header_t hdr;
    uint64_t count;
    uint64_t offset;
};

struct ioFileReadAtResponse {
    FIDL_ALIGNDECL
    fidl_message_header_t hdr;
    zx_status_t s;
    fidl_vector_t data;
};

struct ioFileWriteRequest {
    FIDL_ALIGNDECL
    fidl_message_header_t hdr;
    fidl_vector_t data;
};

struct ioFileWriteResponse {
    FIDL_ALIGNDECL
    fidl_message_header_t hdr;
    zx_status_t s;
    uint64_t actual;
};

struct ioFileWriteAtRequest {
    FIDL_ALIGNDECL
    fidl_message_header_t hdr;
    fidl_vector_t data;
    uint64_t offset;
};

struct ioFileWriteAtResponse {
    FIDL_ALIGNDECL
    fidl_message_header_t hdr;
    zx_status_t s;
    uint64_t actual;
};

struct ioFileSeekRequest {
    FIDL_ALIGNDECL
    fidl_message_header_t hdr;
    int64_t offset;
    SeekOrigin start;
};

struct ioFileSeekResponse {
    FIDL_ALIGNDECL
    fidl_message_header_t hdr;
    zx_status_t s;
    uint64_t offset;
};

struct ioFileTruncateRequest {
    FIDL_ALIGNDECL
    fidl_message_header_t hdr;
    uint64_t length;
};

struct ioFileTruncateResponse {
    FIDL_ALIGNDECL
    fidl_message_header_t hdr;
    zx_status_t s;
};

struct ioFileGetFlagsRequest {
    FIDL_ALIGNDECL
    fidl_message_header_t hdr;
};

struct ioFileGetFlagsResponse {
    FIDL_ALIGNDECL
    fidl_message_header_t hdr;
    zx_status_t s;
    uint32_t flags;
};

struct ioFileSetFlagsRequest {
    FIDL_ALIGNDECL
    fidl_message_header_t hdr;
    uint32_t flags;
};

struct ioFileSetFlagsResponse {
    FIDL_ALIGNDECL
    fidl_message_header_t hdr;
    zx_status_t s;
};

struct ioFileGetVmoRequest {
    FIDL_ALIGNDECL
    fidl_message_header_t hdr;
    uint32_t flags;
};

struct ioFileGetVmoResponse {
    FIDL_ALIGNDECL
    fidl_message_header_t hdr;
    zx_status_t s;
    zx_handle_t vmo;
};

struct ioFileGetVmoAtRequest {
    FIDL_ALIGNDECL
    fidl_message_header_t hdr;
    uint32_t flags;
    uint64_t offset;
    uint64_t length;
};

struct ioFileGetVmoAtResponse {
    FIDL_ALIGNDECL
    fidl_message_header_t hdr;
    zx_status_t s;
    zx_handle_t vmo;
};


































#if defined(__cplusplus)
}
#endif
