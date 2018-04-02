#pragma once

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

typedef struct Service Service;
typedef struct DirectoryOpenRequest DirectoryOpenRequest;
typedef struct DirectoryUnlinkRequest DirectoryUnlinkRequest;
typedef struct DirectoryUnlinkResponse DirectoryUnlinkResponse;
typedef struct DirectoryReadDirentsRequest DirectoryReadDirentsRequest;
typedef struct DirectoryReadDirentsResponse DirectoryReadDirentsResponse;
typedef struct DirectoryRewindRequest DirectoryRewindRequest;
typedef struct DirectoryRewindResponse DirectoryRewindResponse;
typedef struct DirectoryGetTokenRequest DirectoryGetTokenRequest;
typedef struct DirectoryGetTokenResponse DirectoryGetTokenResponse;
typedef struct DirectoryRenameRequest DirectoryRenameRequest;
typedef struct DirectoryRenameResponse DirectoryRenameResponse;
typedef struct DirectoryLinkRequest DirectoryLinkRequest;
typedef struct DirectoryLinkResponse DirectoryLinkResponse;
typedef uint32_t SeekOrigin;
#define SeekOrigin_Start UINT32_C(0)
#define SeekOrigin_Current UINT32_C(1)
#define SeekOrigin_End UINT32_C(2)

typedef struct FileReadRequest FileReadRequest;
typedef struct FileReadResponse FileReadResponse;
typedef struct FileReadAtRequest FileReadAtRequest;
typedef struct FileReadAtResponse FileReadAtResponse;
typedef struct FileWriteRequest FileWriteRequest;
typedef struct FileWriteResponse FileWriteResponse;
typedef struct FileWriteAtRequest FileWriteAtRequest;
typedef struct FileWriteAtResponse FileWriteAtResponse;
typedef struct FileSeekRequest FileSeekRequest;
typedef struct FileSeekResponse FileSeekResponse;
typedef struct FileTruncateRequest FileTruncateRequest;
typedef struct FileTruncateResponse FileTruncateResponse;
typedef struct FileGetFlagsRequest FileGetFlagsRequest;
typedef struct FileGetFlagsResponse FileGetFlagsResponse;
typedef struct FileSetFlagsRequest FileSetFlagsRequest;
typedef struct FileSetFlagsResponse FileSetFlagsResponse;
typedef struct FileGetVmoRequest FileGetVmoRequest;
typedef struct FileGetVmoResponse FileGetVmoResponse;
typedef struct FileGetVmoAtRequest FileGetVmoAtRequest;
typedef struct FileGetVmoAtResponse FileGetVmoAtResponse;
typedef struct NodeAttributes NodeAttributes;
typedef struct NodeSyncRequest NodeSyncRequest;
typedef struct NodeSyncResponse NodeSyncResponse;
typedef struct NodeGetAttrRequest NodeGetAttrRequest;
typedef struct NodeGetAttrResponse NodeGetAttrResponse;
typedef struct NodeSetAttrRequest NodeSetAttrRequest;
typedef struct NodeSetAttrResponse NodeSetAttrResponse;
typedef struct NodeIoctlRequest NodeIoctlRequest;
typedef struct NodeIoctlResponse NodeIoctlResponse;
typedef struct Device Device;
typedef struct Vmofile Vmofile;
typedef struct Pipe Pipe;
typedef struct DirectoryObject DirectoryObject;
typedef struct FileObject FileObject;
typedef struct ObjectInfo ObjectInfo;
typedef struct ObjectCloneRequest ObjectCloneRequest;
typedef struct ObjectCloseRequest ObjectCloseRequest;
typedef struct ObjectCloseResponse ObjectCloseResponse;
typedef struct ObjectListInterfacesRequest ObjectListInterfacesRequest;
typedef struct ObjectListInterfacesResponse ObjectListInterfacesResponse;
typedef struct ObjectBindRequest ObjectBindRequest;
typedef struct ObjectDescribeRequest ObjectDescribeRequest;
typedef struct ObjectDescribeResponse ObjectDescribeResponse;
typedef struct ObjectOnOpenEvent ObjectOnOpenEvent;

// Extern declarations

extern const fidl_type_t DirectoryOpenRequestTable;
extern const fidl_type_t DirectoryUnlinkRequestTable;
extern const fidl_type_t DirectoryUnlinkResponseTable;
extern const fidl_type_t DirectoryReadDirentsRequestTable;
extern const fidl_type_t DirectoryReadDirentsResponseTable;
extern const fidl_type_t DirectoryRewindRequestTable;
extern const fidl_type_t DirectoryRewindResponseTable;
extern const fidl_type_t DirectoryGetTokenRequestTable;
extern const fidl_type_t DirectoryGetTokenResponseTable;
extern const fidl_type_t DirectoryRenameRequestTable;
extern const fidl_type_t DirectoryRenameResponseTable;
extern const fidl_type_t DirectoryLinkRequestTable;
extern const fidl_type_t DirectoryLinkResponseTable;
extern const fidl_type_t FileReadRequestTable;
extern const fidl_type_t FileReadResponseTable;
extern const fidl_type_t FileReadAtRequestTable;
extern const fidl_type_t FileReadAtResponseTable;
extern const fidl_type_t FileWriteRequestTable;
extern const fidl_type_t FileWriteResponseTable;
extern const fidl_type_t FileWriteAtRequestTable;
extern const fidl_type_t FileWriteAtResponseTable;
extern const fidl_type_t FileSeekRequestTable;
extern const fidl_type_t FileSeekResponseTable;
extern const fidl_type_t FileTruncateRequestTable;
extern const fidl_type_t FileTruncateResponseTable;
extern const fidl_type_t FileGetFlagsRequestTable;
extern const fidl_type_t FileGetFlagsResponseTable;
extern const fidl_type_t FileSetFlagsRequestTable;
extern const fidl_type_t FileSetFlagsResponseTable;
extern const fidl_type_t FileGetVmoRequestTable;
extern const fidl_type_t FileGetVmoResponseTable;
extern const fidl_type_t FileGetVmoAtRequestTable;
extern const fidl_type_t FileGetVmoAtResponseTable;
extern const fidl_type_t NodeSyncRequestTable;
extern const fidl_type_t NodeSyncResponseTable;
extern const fidl_type_t NodeGetAttrRequestTable;
extern const fidl_type_t NodeGetAttrResponseTable;
extern const fidl_type_t NodeSetAttrRequestTable;
extern const fidl_type_t NodeSetAttrResponseTable;
extern const fidl_type_t NodeIoctlRequestTable;
extern const fidl_type_t NodeIoctlResponseTable;
extern const fidl_type_t ObjectCloneRequestTable;
extern const fidl_type_t ObjectCloseRequestTable;
extern const fidl_type_t ObjectCloseResponseTable;
extern const fidl_type_t ObjectListInterfacesRequestTable;
extern const fidl_type_t ObjectListInterfacesResponseTable;
extern const fidl_type_t ObjectBindRequestTable;
extern const fidl_type_t ObjectDescribeRequestTable;
extern const fidl_type_t ObjectDescribeResponseTable;
extern const fidl_type_t ObjectOnOpenEventTable;

// Declarations







struct Service {
    uint8_t reserved;
};

struct DirectoryOpenRequest {
    fidl_message_header_t hdr;
    uint32_t flags;
    uint32_t mode;
    fidl_string_t path;
    zx_handle_t object;
};

struct DirectoryUnlinkRequest {
    fidl_message_header_t hdr;
    fidl_string_t path;
};

struct DirectoryUnlinkResponse {
    fidl_message_header_t hdr;
    zx_status_t s;
};

struct DirectoryReadDirentsRequest {
    fidl_message_header_t hdr;
    uint64_t max_out;
};

struct DirectoryReadDirentsResponse {
    fidl_message_header_t hdr;
    zx_status_t s;
    fidl_vector_t dirents;
};

struct DirectoryRewindRequest {
    fidl_message_header_t hdr;
};

struct DirectoryRewindResponse {
    fidl_message_header_t hdr;
    zx_status_t s;
};

struct DirectoryGetTokenRequest {
    fidl_message_header_t hdr;
};

struct DirectoryGetTokenResponse {
    fidl_message_header_t hdr;
    zx_status_t s;
    zx_handle_t token;
};

struct DirectoryRenameRequest {
    fidl_message_header_t hdr;
    fidl_string_t src;
    zx_handle_t dst_parent_token;
    fidl_string_t dst;
};

struct DirectoryRenameResponse {
    fidl_message_header_t hdr;
    zx_status_t s;
};

struct DirectoryLinkRequest {
    fidl_message_header_t hdr;
    fidl_string_t src;
    zx_handle_t dst_parent_token;
    fidl_string_t dst;
};

struct DirectoryLinkResponse {
    fidl_message_header_t hdr;
    zx_status_t s;
};























struct FileReadRequest {
    fidl_message_header_t hdr;
    uint64_t count;
};

struct FileReadResponse {
    fidl_message_header_t hdr;
    zx_status_t s;
    fidl_vector_t data;
};

struct FileReadAtRequest {
    fidl_message_header_t hdr;
    uint64_t count;
    uint64_t offset;
};

struct FileReadAtResponse {
    fidl_message_header_t hdr;
    zx_status_t s;
    fidl_vector_t data;
};

struct FileWriteRequest {
    fidl_message_header_t hdr;
    fidl_vector_t data;
};

struct FileWriteResponse {
    fidl_message_header_t hdr;
    zx_status_t s;
    uint64_t actual;
};

struct FileWriteAtRequest {
    fidl_message_header_t hdr;
    fidl_vector_t data;
    uint64_t offset;
};

struct FileWriteAtResponse {
    fidl_message_header_t hdr;
    zx_status_t s;
    uint64_t actual;
};

struct FileSeekRequest {
    fidl_message_header_t hdr;
    int64_t offset;
    SeekOrigin start;
};

struct FileSeekResponse {
    fidl_message_header_t hdr;
    zx_status_t s;
    uint64_t offset;
};

struct FileTruncateRequest {
    fidl_message_header_t hdr;
    uint64_t length;
};

struct FileTruncateResponse {
    fidl_message_header_t hdr;
    zx_status_t s;
};

struct FileGetFlagsRequest {
    fidl_message_header_t hdr;
};

struct FileGetFlagsResponse {
    fidl_message_header_t hdr;
    zx_status_t s;
    uint32_t flags;
};

struct FileSetFlagsRequest {
    fidl_message_header_t hdr;
    uint32_t flags;
};

struct FileSetFlagsResponse {
    fidl_message_header_t hdr;
    zx_status_t s;
};

struct FileGetVmoRequest {
    fidl_message_header_t hdr;
    uint32_t flags;
};

struct FileGetVmoResponse {
    fidl_message_header_t hdr;
    zx_status_t s;
    zx_handle_t vmo;
};

struct FileGetVmoAtRequest {
    fidl_message_header_t hdr;
    uint32_t flags;
    uint64_t offset;
    uint64_t length;
};

struct FileGetVmoAtResponse {
    fidl_message_header_t hdr;
    zx_status_t s;
    zx_handle_t vmo;
};





struct NodeAttributes {
    uint32_t mode;
    uint64_t id;
    uint64_t content_size;
    uint64_t storage_size;
    uint64_t link_count;
    uint64_t creation_time;
    uint64_t modification_time;
};

struct NodeSyncRequest {
    fidl_message_header_t hdr;
};

struct NodeSyncResponse {
    fidl_message_header_t hdr;
    zx_status_t s;
};

struct NodeGetAttrRequest {
    fidl_message_header_t hdr;
};

struct NodeGetAttrResponse {
    fidl_message_header_t hdr;
    zx_status_t s;
    NodeAttributes attributes;
};

struct NodeSetAttrRequest {
    fidl_message_header_t hdr;
    uint32_t flags;
    NodeAttributes attributes;
};

struct NodeSetAttrResponse {
    fidl_message_header_t hdr;
    zx_status_t s;
};

struct NodeIoctlRequest {
    fidl_message_header_t hdr;
    uint32_t opcode;
    uint64_t max_out;
    fidl_vector_t handles;
    fidl_vector_t in;
};

struct NodeIoctlResponse {
    fidl_message_header_t hdr;
    zx_status_t s;
    fidl_vector_t handles;
    fidl_vector_t out;
};

struct Device {
    zx_handle_t event;
};

struct Vmofile {
    zx_handle_t vmo;
    uint64_t offset;
    uint64_t length;
};


struct Pipe {
    zx_handle_t socket;
};

struct DirectoryObject {
    uint8_t reserved;
};

struct FileObject {
    zx_handle_t event;
};

struct ObjectInfo {
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

struct ObjectCloneRequest {
    fidl_message_header_t hdr;
    uint32_t flags;
    zx_handle_t object;
};

struct ObjectCloseRequest {
    fidl_message_header_t hdr;
};

struct ObjectCloseResponse {
    fidl_message_header_t hdr;
    zx_status_t s;
};

struct ObjectListInterfacesRequest {
    fidl_message_header_t hdr;
};

struct ObjectListInterfacesResponse {
    fidl_message_header_t hdr;
    fidl_vector_t interfaces;
};

struct ObjectBindRequest {
    fidl_message_header_t hdr;
    fidl_string_t iface;
};

struct ObjectDescribeRequest {
    fidl_message_header_t hdr;
};

struct ObjectDescribeResponse {
    fidl_message_header_t hdr;
    ObjectInfo info;
};

struct ObjectOnOpenEvent {
    fidl_message_header_t hdr;
    zx_status_t s;
    ObjectInfo* info;
};

#if defined(__cplusplus)
}
#endif
