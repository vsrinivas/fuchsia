// Fuchsia display api
// A work in progress, sketching out the interface

struct display_t {
	uint id
    string name
    edid_t edid
	uint width // native resolution
	uint height // native resolution
	display_engine_t display_engine
}

// Where does this fit in?
struct display_engine_t {
	color_format_t supported_formats[]
	color_space_t supported_color_spaces[]
	tiling_format_t supported_tiling_formats[]
}

struct image_descriptor_t {
    uint width
    uint height
    uint stride
    tiling_format_t tiling
    color_format_t format
    color_space_t color_space
}

enum update_type {
	INITIAL, // contains list of displays (may be empty) connected at the time the callback was registered)
	DISPLAY_HOTPLUG, // an early message, display is in process of coming up
	DISPLAY_HOTUNPLUG,
}

void display_state_update_callback_t(update_type, update_info)

void register_callback(display_state_update_callback_t)

void scanout_buffer(display_id, buffer_id, image_descriptor_t)
