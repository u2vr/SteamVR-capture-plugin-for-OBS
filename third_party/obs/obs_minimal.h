#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32
#define OBS_Import __declspec(dllimport)
#define OBS_Export __declspec(dllexport)
#else
#define OBS_Import
#define OBS_Export
#endif

/* LIBOBS API Version 32.2.x */
#define MAKE_SEMANTIC_VERSION(major, minor, patch) \
	(((uint32_t)(major) << 24) | ((uint32_t)(minor) << 16) | (uint32_t)(patch))
#define LIBOBS_API_VER MAKE_SEMANTIC_VERSION(32, 2, 1)

/* Logging levels */
enum {
	LOG_ERROR = 100,
	LOG_WARNING = 200,
	LOG_INFO = 300,
	LOG_DEBUG = 400
};

OBS_Import void blog(int log_level, const char *format, ...);
OBS_Import void bfree(void *ptr);

/* Forward declarations */
typedef struct obs_source obs_source_t;
typedef struct obs_data obs_data_t;
typedef struct obs_properties obs_properties_t;
typedef struct obs_property obs_property_t;
typedef struct obs_module obs_module_t;
typedef struct obs_missing_files obs_missing_files_t;
typedef struct gs_effect gs_effect_t;
typedef struct gs_texture gs_texture_t;
typedef struct lookup_t lookup_t;

struct obs_source_frame;
struct obs_audio_data;
struct obs_source_audio_mix;
struct audio_output_data;
struct obs_mouse_event;
struct obs_key_event;

typedef void (*obs_source_enum_proc_t)(obs_source_t *parent, obs_source_t *child, void *param);
typedef bool (*obs_property_clicked_t)(obs_properties_t *props, obs_property_t *property, void *data);
typedef bool (*obs_property_modified_t)(obs_properties_t *props, obs_property_t *property, obs_data_t *settings);

/* Graphics subsystem */
#define GS_DEVICE_OPENGL 1
#define GS_DEVICE_DIRECT3D_11 2

enum obs_base_effect {
	OBS_EFFECT_DEFAULT,
	OBS_EFFECT_DEFAULT_RECT,
	OBS_EFFECT_OPAQUE,
	OBS_EFFECT_SOLID,
	OBS_EFFECT_BICUBIC,
	OBS_EFFECT_LANCZOS,
	OBS_EFFECT_BILINEAR_LOWRES,
	OBS_EFFECT_PREMULTIPLIED_ALPHA,
	OBS_EFFECT_REPEAT,
	OBS_EFFECT_AREA,
};

enum gs_color_space {
	GS_CS_SRGB,
	GS_CS_SRGB_16F,
	GS_CS_709_EXTENDED,
	GS_CS_709_SCRGB,
};

OBS_Import void obs_enter_graphics(void);
OBS_Import void obs_leave_graphics(void);
OBS_Import int gs_get_device_type(void);
OBS_Import void *gs_get_device_obj(void);
OBS_Import gs_texture_t *gs_texture_open_shared(uint32_t handle);
OBS_Import void gs_texture_destroy(gs_texture_t *tex);
OBS_Import gs_effect_t *obs_get_base_effect(enum obs_base_effect effect);
OBS_Import bool gs_effect_loop(gs_effect_t *effect, const char *name);
OBS_Import void obs_source_draw(gs_texture_t *image, int x, int y, uint32_t cx, uint32_t cy, bool flip);

/* Source enums and flags */
enum obs_source_type {
	OBS_SOURCE_TYPE_INPUT,
	OBS_SOURCE_TYPE_FILTER,
	OBS_SOURCE_TYPE_TRANSITION,
	OBS_SOURCE_TYPE_SCENE,
};

enum obs_icon_type {
	OBS_ICON_TYPE_UNKNOWN,
	OBS_ICON_TYPE_IMAGE,
	OBS_ICON_TYPE_COLOR,
	OBS_ICON_TYPE_SLIDESHOW,
	OBS_ICON_TYPE_AUDIO_INPUT,
	OBS_ICON_TYPE_AUDIO_OUTPUT,
	OBS_ICON_TYPE_DESKTOP_CAPTURE,
	OBS_ICON_TYPE_WINDOW_CAPTURE,
	OBS_ICON_TYPE_GAME_CAPTURE,
	OBS_ICON_TYPE_CAMERA,
	OBS_ICON_TYPE_TEXT,
	OBS_ICON_TYPE_MEDIA,
	OBS_ICON_TYPE_BROWSER,
	OBS_ICON_TYPE_CUSTOM,
	OBS_ICON_TYPE_PROCESS_AUDIO_OUTPUT,
};

enum obs_media_state {
	OBS_MEDIA_STATE_NONE,
	OBS_MEDIA_STATE_PLAYING,
	OBS_MEDIA_STATE_OPENING,
	OBS_MEDIA_STATE_BUFFERING,
	OBS_MEDIA_STATE_PAUSED,
	OBS_MEDIA_STATE_STOPPED,
	OBS_MEDIA_STATE_ENDED,
	OBS_MEDIA_STATE_ERROR,
};

#define OBS_SOURCE_VIDEO (1 << 0)
#define OBS_SOURCE_AUDIO (1 << 1)
#define OBS_SOURCE_ASYNC (1 << 2)
#define OBS_SOURCE_CUSTOM_DRAW (1 << 3)
#define OBS_SOURCE_INTERACTION (1 << 5)
#define OBS_SOURCE_COMPOSITE (1 << 6)
#define OBS_SOURCE_DO_NOT_DUPLICATE (1 << 7)
#define OBS_SOURCE_SRGB (1 << 15)

struct obs_source_info {
	const char *id;
	enum obs_source_type type;
	uint32_t output_flags;
	const char *(*get_name)(void *type_data);
	void *(*create)(obs_data_t *settings, obs_source_t *source);
	void (*destroy)(void *data);
	uint32_t (*get_width)(void *data);
	uint32_t (*get_height)(void *data);
	void (*get_defaults)(obs_data_t *settings);
	obs_properties_t *(*get_properties)(void *data);
	void (*update)(void *data, obs_data_t *settings);
	void (*activate)(void *data);
	void (*deactivate)(void *data);
	void (*show)(void *data);
	void (*hide)(void *data);
	void (*video_tick)(void *data, float seconds);
	void (*video_render)(void *data, gs_effect_t *effect);
	struct obs_source_frame *(*filter_video)(void *data, struct obs_source_frame *frame);
	struct obs_audio_data *(*filter_audio)(void *data, struct obs_audio_data *audio);
	void (*enum_active_sources)(void *data, obs_source_enum_proc_t enum_callback, void *param);
	void (*save)(void *data, obs_data_t *settings);
	void (*load)(void *data, obs_data_t *settings);
	void (*mouse_click)(void *data, const struct obs_mouse_event *event, int32_t type, bool mouse_up, uint32_t click_count);
	void (*mouse_move)(void *data, const struct obs_mouse_event *event, bool mouse_leave);
	void (*mouse_wheel)(void *data, const struct obs_mouse_event *event, int x_delta, int y_delta);
	void (*focus)(void *data, bool focus);
	void (*key_click)(void *data, const struct obs_key_event *event, bool key_up);
	void (*filter_remove)(void *data, obs_source_t *source);
	void *type_data;
	void (*free_type_data)(void *type_data);
	bool (*audio_render)(void *data, uint64_t *ts_out, struct obs_source_audio_mix *audio_output, uint32_t mixers, size_t channels, size_t sample_rate);
	void (*enum_all_sources)(void *data, obs_source_enum_proc_t enum_callback, void *param);
	void (*transition_start)(void *data);
	void (*transition_stop)(void *data);
	void (*get_defaults2)(void *type_data, obs_data_t *settings);
	obs_properties_t *(*get_properties2)(void *data, void *type_data);
	bool (*audio_mix)(void *data, uint64_t *ts_out, struct audio_output_data *audio_output, size_t channels, size_t sample_rate);
	enum obs_icon_type icon_type;
	void (*media_play_pause)(void *data, bool pause);
	void (*media_restart)(void *data);
	void (*media_stop)(void *data);
	void (*media_next)(void *data);
	void (*media_previous)(void *data);
	int64_t (*media_get_duration)(void *data);
	int64_t (*media_get_time)(void *data);
	void (*media_set_time)(void *data, int64_t miliseconds);
	enum obs_media_state (*media_get_state)(void *data);
	uint32_t version;
	const char *unversioned_id;
	obs_missing_files_t *(*missing_files)(void *data);
	enum gs_color_space (*video_get_color_space)(void *data, size_t count, const enum gs_color_space *preferred_spaces);
	void (*filter_add)(void *data, obs_source_t *source);
	const char *(*get_dark_icon)(void *type_data);
	const char *(*get_light_icon)(void *type_data);
};

#ifdef __cplusplus
static_assert(sizeof(struct obs_source_info) == 0x1A8, "obs_source_info size must match OBS 32 ABI (0x1A8)");
#endif

OBS_Import void obs_register_source_s(const struct obs_source_info *info, size_t size);
#define obs_register_source(info) obs_register_source_s(info, sizeof(struct obs_source_info))

OBS_Import const char *obs_source_get_name(const obs_source_t *source);
OBS_Import bool obs_source_active(const obs_source_t *source);
OBS_Import bool obs_source_showing(const obs_source_t *source);

/* Settings (obs_data_t) */
OBS_Import void obs_data_set_default_int(obs_data_t *data, const char *name, long long val);
OBS_Import void obs_data_set_default_double(obs_data_t *data, const char *name, double val);
OBS_Import void obs_data_set_default_bool(obs_data_t *data, const char *name, bool val);
OBS_Import void obs_data_set_default_string(obs_data_t *data, const char *name, const char *val);

OBS_Import long long obs_data_get_int(obs_data_t *data, const char *name);
OBS_Import double obs_data_get_double(obs_data_t *data, const char *name);
OBS_Import bool obs_data_get_bool(obs_data_t *data, const char *name);
OBS_Import const char *obs_data_get_string(obs_data_t *data, const char *name);

OBS_Import void obs_data_set_int(obs_data_t *data, const char *name, long long val);
OBS_Import void obs_data_set_double(obs_data_t *data, const char *name, double val);
OBS_Import void obs_data_set_bool(obs_data_t *data, const char *name, bool val);

/* Properties UI (obs_properties_t) */
enum obs_combo_format {
	OBS_COMBO_FORMAT_INVALID,
	OBS_COMBO_FORMAT_INT,
	OBS_COMBO_FORMAT_FLOAT,
	OBS_COMBO_FORMAT_STRING,
	OBS_COMBO_FORMAT_BOOL,
};

enum obs_combo_type {
	OBS_COMBO_TYPE_INVALID,
	OBS_COMBO_TYPE_EDITABLE,
	OBS_COMBO_TYPE_LIST,
	OBS_COMBO_TYPE_RADIO,
};

enum obs_text_type {
	OBS_TEXT_DEFAULT,
	OBS_TEXT_PASSWORD,
	OBS_TEXT_MULTILINE,
	OBS_TEXT_INFO,
};

OBS_Import obs_properties_t *obs_properties_create(void);
OBS_Import obs_property_t *obs_properties_get(obs_properties_t *props, const char *property);
OBS_Import obs_property_t *obs_properties_add_bool(obs_properties_t *props, const char *name, const char *description);
OBS_Import obs_property_t *obs_properties_add_int(obs_properties_t *props, const char *name, const char *description, int min, int max, int step);
OBS_Import obs_property_t *obs_properties_add_int_slider(obs_properties_t *props, const char *name, const char *description, int min, int max, int step);
OBS_Import obs_property_t *obs_properties_add_float_slider(obs_properties_t *props, const char *name, const char *description, double min, double max, double step);
OBS_Import obs_property_t *obs_properties_add_text(obs_properties_t *props, const char *name, const char *description, enum obs_text_type type);
OBS_Import obs_property_t *obs_properties_add_list(obs_properties_t *props, const char *name, const char *description, enum obs_combo_type type, enum obs_combo_format format);
OBS_Import obs_property_t *obs_properties_add_button(obs_properties_t *props, const char *name, const char *text, obs_property_clicked_t callback);

OBS_Import size_t obs_property_list_add_int(obs_property_t *p, const char *name, long long val);
OBS_Import void obs_property_set_description(obs_property_t *p, const char *description);
OBS_Import void obs_property_set_long_description(obs_property_t *p, const char *long_description);
OBS_Import void obs_property_set_visible(obs_property_t *p, bool visible);
OBS_Import void obs_property_set_modified_callback(obs_property_t *p, obs_property_modified_t modified);
OBS_Import void obs_property_float_set_suffix(obs_property_t *p, const char *suffix);

/* Module helper declarations */
OBS_Import char *obs_find_module_file(obs_module_t *module, const char *file);
OBS_Import lookup_t *obs_module_load_locale(obs_module_t *module, const char *default_locale, const char *locale);
OBS_Import bool text_lookup_getstr(lookup_t *lookup, const char *lookup_val, const char **out);
OBS_Import void text_lookup_destroy(lookup_t *lookup);

#ifdef __cplusplus
}
#endif
