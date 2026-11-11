#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#include <shlwapi.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <mutex>
#include <algorithm>

#include "../third_party/obs/obs_minimal.h"
#include "../third_party/openvr/openvr.h"
#include "stabilizer_math.h"
#include "d3d11_stabilizer.h"

/* =========================================================================
 * Standard OBS Module Exports
 * ========================================================================= */
static obs_module_t *g_obs_module_pointer = nullptr;
static lookup_t *g_obs_locale_lookup = nullptr;

extern "C" {

OBS_Export void obs_module_set_pointer(obs_module_t *module)
{
	g_obs_module_pointer = module;
}

OBS_Export uint32_t obs_module_ver(void)
{
	return LIBOBS_API_VER;
}

OBS_Export void obs_module_set_locale(const char *locale)
{
	if (g_obs_locale_lookup) {
		text_lookup_destroy(g_obs_locale_lookup);
		g_obs_locale_lookup = nullptr;
	}
	if (g_obs_module_pointer) {
		g_obs_locale_lookup = obs_module_load_locale(g_obs_module_pointer, "en-US", locale);
	}
}

OBS_Export void obs_module_free_locale(void)
{
	if (g_obs_locale_lookup) {
		text_lookup_destroy(g_obs_locale_lookup);
		g_obs_locale_lookup = nullptr;
	}
}

OBS_Export const char *obs_module_get_string(const char *lookup_val)
{
	const char *out = lookup_val;
	if (g_obs_locale_lookup) {
		text_lookup_getstr(g_obs_locale_lookup, lookup_val, &out);
	}
	return out;
}

OBS_Export const char *obs_module_name(void)
{
	return "VR Mirror by U2VR";
}

OBS_Export const char *obs_module_description(void)
{
	return "Direct SteamVR dual-eye mirror with 3D headset stabilization for Meta Quest 3 and PCVR";
}

OBS_Export const char *obs_module_author(void)
{
	return "U2VR";
}

} // extern "C"

/* =========================================================================
 * Safe SEH Invocation Wrappers for OpenVR
 * ========================================================================= */
typedef uint32_t (*PFN_VR_InitInternal2)(vr::EVRInitError *peError, vr::EVRApplicationType eType, const char *pStartupInfo);
typedef void (*PFN_VR_ShutdownInternal)(void);
typedef void *(*PFN_VR_GetGenericInterface)(const char *pchInterfaceVersion, vr::EVRInitError *peError);
typedef const char *(*PFN_VR_GetVRInitErrorAsEnglishDescription)(vr::EVRInitError error);

static uint32_t seh_vr_init(PFN_VR_InitInternal2 fnInit, vr::EVRInitError *peError, vr::EVRApplicationType eType, const char *pStartupInfo)
{
	if (!fnInit) {
		if (peError) *peError = vr::VRInitError_Init_NotInitialized;
		return 0;
	}
	__try {
		return fnInit(peError, eType, pStartupInfo);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		if (peError) *peError = vr::VRInitError_Init_Internal;
		return 0;
	}
}

static void *seh_vr_get_interface(PFN_VR_GetGenericInterface fnGetInterface, const char *pchInterfaceVersion, vr::EVRInitError *peError)
{
	if (!fnGetInterface) {
		if (peError) *peError = vr::VRInitError_Init_NotInitialized;
		return nullptr;
	}
	__try {
		return fnGetInterface(pchInterfaceVersion, peError);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		if (peError) *peError = vr::VRInitError_Init_Internal;
		return nullptr;
	}
}

static vr::EVRInitError seh_set_sdk_version(vr::IVRSystem *sys, uint32_t nMajor, uint32_t nMinor, uint32_t nBuild)
{
	if (!sys)
		return vr::VRInitError_Init_NotInitialized;
	__try {
		return sys->SetSDKVersion(nMajor, nMinor, nBuild);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return vr::VRInitError_Init_Internal;
	}
}

static bool seh_poll_next_event(vr::IVRSystem *sys, vr::VREvent_t *pEvent, uint32_t uncbVREvent)
{
	if (!sys || !pEvent)
		return false;
	__try {
		return sys->PollNextEvent(pEvent, uncbVREvent);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

static bool seh_get_frame_timing(vr::IVRCompositor *comp, vr::Compositor_FrameTiming *pTiming, uint32_t unFramesAgo)
{
	if (!comp || !pTiming)
		return false;
	__try {
		return comp->GetFrameTiming(pTiming, unFramesAgo);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

static vr::EVRCompositorError seh_get_last_pose(vr::IVRCompositor *comp, vr::TrackedDeviceIndex_t unDeviceIndex, vr::TrackedDevicePose_t *pOutputPoseArray, vr::TrackedDevicePose_t *pActualPoseArray)
{
	if (!comp || !pOutputPoseArray)
		return vr::VRCompositorError_IndexOutOfRange;
	__try {
		return comp->GetLastPoseForTrackedDeviceIndex(unDeviceIndex, pOutputPoseArray, pActualPoseArray);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return vr::VRCompositorError_RequestFailed;
	}
}

static void seh_get_device_to_absolute_tracking_pose(vr::IVRSystem *sys, vr::ETrackingUniverseOrigin eOrigin, float fPredictedSecondsToPhotonsFromNow, vr::TrackedDevicePose_t *pTrackedDevicePoseArray, uint32_t unTrackedDevicePoseArrayCount)
{
	if (!sys || !pTrackedDevicePoseArray)
		return;
	__try {
		sys->GetDeviceToAbsoluteTrackingPose(eOrigin, fPredictedSecondsToPhotonsFromNow, pTrackedDevicePoseArray, unTrackedDevicePoseArrayCount);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		if (unTrackedDevicePoseArrayCount > 0) {
			pTrackedDevicePoseArray[0].bPoseIsValid = false;
		}
	}
}

static void seh_get_projection_raw(vr::IVRSystem *sys, vr::EVREye eEye, float *pfLeft, float *pfRight, float *pfTop, float *pfBottom)
{
	if (!sys)
		return;
	__try {
		sys->GetProjectionRaw(eEye, pfLeft, pfRight, pfTop, pfBottom);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		if (pfLeft) *pfLeft = -1.0f;
		if (pfRight) *pfRight = 1.0f;
		if (pfTop) *pfTop = -1.0f;
		if (pfBottom) *pfBottom = 1.0f;
	}
}

static vr::EVRCompositorError seh_get_mirror_texture(vr::IVRCompositor *comp, vr::EVREye eEye, void *pD3D11DeviceOrResource, void **ppD3D11ShaderResourceView)
{
	if (!comp || !ppD3D11ShaderResourceView)
		return vr::VRCompositorError_RequestFailed;
	__try {
		return comp->GetMirrorTextureD3D11(eEye, pD3D11DeviceOrResource, ppD3D11ShaderResourceView);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		*ppD3D11ShaderResourceView = nullptr;
		return vr::VRCompositorError_RequestFailed;
	}
}

static void seh_release_mirror_texture(vr::IVRCompositor *comp, void *pTexture)
{
	if (!comp || !pTexture)
		return;
	__try {
		comp->ReleaseMirrorTextureD3D11(pTexture);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		// Suppressed safely
	}
}

static void seh_vr_shutdown(PFN_VR_ShutdownInternal fnShutdown)
{
	if (!fnShutdown)
		return;
	__try {
		fnShutdown();
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		// Suppressed safely
	}
}

/* =========================================================================
 * Dynamic OpenVR Loader (loads openvr_api.dll only when vrserver.exe runs)
 * ========================================================================= */
struct OpenVRApi {
	HMODULE hModule = nullptr;
	PFN_VR_InitInternal2 InitInternal2 = nullptr;
	PFN_VR_ShutdownInternal ShutdownInternal = nullptr;
	PFN_VR_GetGenericInterface GetGenericInterface = nullptr;
	PFN_VR_GetVRInitErrorAsEnglishDescription GetErrorString = nullptr;
	vr::IVRSystem *system = nullptr;
	vr::IVRCompositor *compositor = nullptr;
	bool initialized = false;

	HANDLE hVrServer = nullptr;
	DWORD vrserver_pid = 0;

	static DWORD find_vrserver_pid() {
		HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		if (snap == INVALID_HANDLE_VALUE)
			return 0;

		PROCESSENTRY32W pe{};
		pe.dwSize = sizeof(pe);
		DWORD pid = 0;
		if (Process32FirstW(snap, &pe)) {
			do {
				if (_wcsicmp(pe.szExeFile, L"vrserver.exe") == 0) {
					pid = pe.th32ProcessID;
					break;
				}
			} while (Process32NextW(snap, &pe));
		}
		CloseHandle(snap);
		return pid;
	}

	static bool is_vrserver_running() {
		return find_vrserver_pid() != 0;
	}

	bool is_connected_server_alive() {
		if (!initialized)
			return false;
		if (hVrServer) {
			DWORD wait = WaitForSingleObject(hVrServer, 0);
			return (wait == WAIT_TIMEOUT);
		}
		return is_vrserver_running();
	}

	bool load_dll() {
		if (hModule)
			return true;

		// Find openvr_api.dll next to this plugin DLL
		HMODULE hSelf = nullptr;
		if (GetModuleHandleExW(
			    GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			    reinterpret_cast<LPCWSTR>(&find_vrserver_pid),
			    &hSelf)) {
			wchar_t module_path[MAX_PATH] = {};
			if (GetModuleFileNameW(hSelf, module_path, MAX_PATH) > 0) {
				wchar_t *slash = wcsrchr(module_path, L'\\');
				if (slash) {
					*(slash + 1) = L'\0';
					std::wstring dll_path = std::wstring(module_path) + L"openvr_api.dll";
					hModule = LoadLibraryExW(dll_path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
				}
			}
		}

		if (!hModule) {
			hModule = LoadLibraryW(L"openvr_api.dll");
		}

		if (!hModule) {
			blog(LOG_WARNING, "[VR Mirror by U2VR] Failed to load openvr_api.dll (error %lu)", GetLastError());
			return false;
		}

		InitInternal2 = reinterpret_cast<PFN_VR_InitInternal2>(GetProcAddress(hModule, "VR_InitInternal2"));
		ShutdownInternal = reinterpret_cast<PFN_VR_ShutdownInternal>(GetProcAddress(hModule, "VR_ShutdownInternal"));
		GetGenericInterface = reinterpret_cast<PFN_VR_GetGenericInterface>(GetProcAddress(hModule, "VR_GetGenericInterface"));
		GetErrorString = reinterpret_cast<PFN_VR_GetVRInitErrorAsEnglishDescription>(
			GetProcAddress(hModule, "VR_GetVRInitErrorAsEnglishDescription"));

		if (!InitInternal2 || !ShutdownInternal || !GetGenericInterface) {
			blog(LOG_ERROR, "[VR Mirror by U2VR] openvr_api.dll is missing required entry points");
			FreeLibrary(hModule);
			hModule = nullptr;
			return false;
		}
		return true;
	}

	bool connect() {
		if (initialized && system && compositor)
			return true;

		DWORD pid = find_vrserver_pid();
		if (pid == 0)
			return false;

		if (!load_dll())
			return false;

		vr::EVRInitError err = vr::VRInitError_None;
		seh_vr_init(InitInternal2, &err, vr::VRApplication_Background, nullptr);
		if (err != vr::VRInitError_None) {
			return false;
		}

		system = reinterpret_cast<vr::IVRSystem *>(seh_vr_get_interface(GetGenericInterface, vr::IVRSystem_Version, &err));
		if (!system || err != vr::VRInitError_None) {
			seh_vr_shutdown(ShutdownInternal);
			system = nullptr;
			return false;
		}

		err = seh_set_sdk_version(system, vr::k_nSteamVRVersionMajor, vr::k_nSteamVRVersionMinor, vr::k_nSteamVRVersionBuild);
		if (err != vr::VRInitError_None && err != vr::VRInitError_Init_ClientVersionAlreadyProvided) {
			blog(LOG_WARNING, "[VR Mirror by U2VR] SetSDKVersion returned %d", (int)err);
		}

		compositor = reinterpret_cast<vr::IVRCompositor *>(seh_vr_get_interface(GetGenericInterface, vr::IVRCompositor_Version, &err));
		if (!compositor || err != vr::VRInitError_None) {
			seh_vr_shutdown(ShutdownInternal);
			system = nullptr;
			compositor = nullptr;
			return false;
		}

		vrserver_pid = pid;
		if (hVrServer) {
			CloseHandle(hVrServer);
			hVrServer = nullptr;
		}
		hVrServer = OpenProcess(SYNCHRONIZE, FALSE, pid);

		initialized = true;
		blog(LOG_INFO, "[VR Mirror by U2VR] Connected to SteamVR (SDK %u.%u.%u, PID %lu)",
		     vr::k_nSteamVRVersionMajor, vr::k_nSteamVRVersionMinor, vr::k_nSteamVRVersionBuild, pid);
		return true;
	}

	void disconnect(bool call_shutdown = true) {
		if (hVrServer) {
			CloseHandle(hVrServer);
			hVrServer = nullptr;
		}
		vrserver_pid = 0;

		if (initialized && ShutdownInternal && call_shutdown) {
			seh_vr_shutdown(ShutdownInternal);
		}
		system = nullptr;
		compositor = nullptr;
		initialized = false;
	}

	void unload() {
		disconnect(true);
		if (hModule) {
			FreeLibrary(hModule);
			hModule = nullptr;
			InitInternal2 = nullptr;
			ShutdownInternal = nullptr;
			GetGenericInterface = nullptr;
			GetErrorString = nullptr;
		}
	}
};

/* =========================================================================
 * OBS Source State
 * ========================================================================= */
#define PROP_EYE_MODE        "eye_mode"
#define PROP_SMOOTH_STRENGTH "smooth_strength"
#define PROP_ROLL_LOCK       "roll_lock"
#define PROP_MAX_ANGLE       "max_angle_deg"
#define PROP_ZOOM_MARGIN     "zoom_margin"
#define PROP_STITCH_OFFSET_X "stitch_offset_x"
#define PROP_STITCH_OFFSET_Y "stitch_offset_y"
#define PROP_STITCH_FEATHER  "stitch_feather"
#define PROP_VERT_OFFSET     "vert_offset"
#define PROP_HORIZ_OFFSET    "horiz_offset"
#define PROP_OUT_RESOLUTION  "out_resolution"
#define PROP_BORDER_MODE     "border_mode"
#define PROP_RECONNECT       "reconnect_btn"

enum OutputResPreset {
	RES_1920x1080 = 0,
	RES_2560x1440 = 1,
	RES_1280x720  = 2,
	RES_3840x2160 = 3,
};

struct VRStabilizerSource {
	obs_source_t *source = nullptr;
	std::recursive_mutex state_mutex;

	bool server_quitting = false;
	DWORD dead_server_pid = 0;

	// User settings
	int eye_mode = 0;               // 0: RightDom+Left, 1: LeftDom+Right, 2: Right, 3: Left
	float smooth_strength = 65.0f;  // 0..100%
	float roll_lock = 75.0f;        // 0..100%
	float max_angle_deg = 8.5f;     // 1..25 deg
	float zoom_margin = 6.0f;       // 0..30%
	float stitch_offset_x = 18.0f;  // 0..35%
	float stitch_offset_y = 0.0f;   // -10..10%
	float stitch_feather = 8.0f;    // 1..25%
	float vert_offset = 0.0f;       // -25..25%
	float horiz_offset = 0.0f;      // -20..20%
	int out_res_preset = RES_1920x1080;
	int border_mode = 0;            // 0: Clamp, 1: Vignette, 2: Black

	uint32_t target_width = 1920;
	uint32_t target_height = 1080;

	// OpenVR & D3D11 state
	OpenVRApi vr_api;
	vr_stab::D3D11StabilizerRenderer renderer;
	vr_stab::PoseStabilizer stabilizer;

	void *raw_left_srv = nullptr;
	void *raw_right_srv = nullptr;
	vr_stab::ComPtr<ID3D11ShaderResourceView> left_unorm_srv;
	vr_stab::ComPtr<ID3D11ShaderResourceView> right_unorm_srv;
	uint32_t eye_tex_width = 2400;
	uint32_t eye_tex_height = 2400;

	float proj_left[4]  = { -1.0f, 1.0f, 1.0f, -1.0f };
	float proj_right[4] = { -1.0f, 1.0f, 1.0f, -1.0f };

	bool mirror_ready = false;
	ULONGLONG last_retry_tick = 0;
	LARGE_INTEGER qpc_freq{};
	LARGE_INTEGER last_qpc{};

	void update_resolution_from_preset() {
		switch (out_res_preset) {
		case RES_2560x1440:
			target_width = 2560;
			target_height = 1440;
			break;
		case RES_1280x720:
			target_width = 1280;
			target_height = 720;
			break;
		case RES_3840x2160:
			target_width = 3840;
			target_height = 2160;
			break;
		case RES_1920x1080:
		default:
			target_width = 1920;
			target_height = 1080;
			break;
		}
	}

	static void normalize_projection(float l, float r, float t, float b, float out_proj[4]) {
		// Ensure left < right
		float left_tan = std::min(l, r);
		float right_tan = std::max(l, r);
		if (right_tan - left_tan < 0.05f) {
			left_tan = -1.0f;
			right_tan = 1.0f;
		}

		// In OpenVR GetProjectionRaw, pfTop is typically negative and pfBottom positive.
		// In +Y-up camera space, top tangent > 0 and bottom tangent < 0.
		float top_tan_pos = (t < b) ? -t : t;
		float bot_tan_neg = (t < b) ? -b : b;
		if (top_tan_pos - bot_tan_neg < 0.05f) {
			top_tan_pos = 1.0f;
			bot_tan_neg = -1.0f;
		}

		out_proj[0] = left_tan;
		out_proj[1] = right_tan;
		out_proj[2] = top_tan_pos;
		out_proj[3] = bot_tan_neg;
	}

	void release_mirror_resources(bool notify_compositor = true) {
		left_unorm_srv.Reset();
		right_unorm_srv.Reset();
		if (vr_api.compositor && notify_compositor && vr_api.is_connected_server_alive()) {
			if (raw_left_srv) {
				seh_release_mirror_texture(vr_api.compositor, raw_left_srv);
				raw_left_srv = nullptr;
			}
			if (raw_right_srv) {
				seh_release_mirror_texture(vr_api.compositor, raw_right_srv);
				raw_right_srv = nullptr;
			}
		} else {
			raw_left_srv = nullptr;
			raw_right_srv = nullptr;
		}
		mirror_ready = false;
	}

	void full_reset(bool graceful = true) {
		bool server_alive = vr_api.is_connected_server_alive();
		bool do_cleanup_calls = graceful && server_alive;
		release_mirror_resources(do_cleanup_calls);
		vr_api.disconnect(do_cleanup_calls);
		stabilizer.reset();
		last_retry_tick = GetTickCount64();
	}

	bool try_initialize_capture(bool force_now = false) {
		if (mirror_ready)
			return true;

		ULONGLONG now = GetTickCount64();
		if (!force_now && (now - last_retry_tick < 3000))
			return false;
		last_retry_tick = now;

		DWORD current_pid = OpenVRApi::find_vrserver_pid();
		if (current_pid == 0) {
			server_quitting = false;
			dead_server_pid = 0;
			return false;
		}

		if (!force_now && (server_quitting || (dead_server_pid != 0 && current_pid == dead_server_pid))) {
			return false;
		}

		server_quitting = false;
		dead_server_pid = 0;

		if (!vr_api.connect())
			return false;

		// Ensure our D3D11 device on OBS's adapter is initialized
		if (!renderer.device) {
			obs_enter_graphics();
			int dev_type = gs_get_device_type();
			void *raw_obj = gs_get_device_obj();
			ID3D11Device *obs_dev = (dev_type == GS_DEVICE_DIRECT3D_11 || dev_type == 2)
				? reinterpret_cast<ID3D11Device *>(raw_obj)
				: nullptr;
			obs_leave_graphics();

			if (!obs_dev) {
				blog(LOG_ERROR, "[VR Mirror by U2VR] dev_type=%d, obs_dev=NULL (expected GS_DEVICE_DIRECT3D_11=%d)",
				     dev_type, GS_DEVICE_DIRECT3D_11);
				return false;
			}

			if (!renderer.init_device(obs_dev)) {
				blog(LOG_ERROR, "[VR Mirror by U2VR] Failed to initialize D3D11 stabilizer device");
				return false;
			}
		}

		if (!renderer.ensure_output_target(target_width, target_height)) {
			blog(LOG_ERROR, "[VR Mirror by U2VR] Failed to create shared output texture (%ux%u)",
			     target_width, target_height);
			return false;
		}

		// Acquire both Left and Right eye mirror textures from SteamVR
		release_mirror_resources(true);

		vr::EVRCompositorError err_l = seh_get_mirror_texture(
			vr_api.compositor, vr::Eye_Left, renderer.device.Get(), &raw_left_srv);
		vr::EVRCompositorError err_r = seh_get_mirror_texture(
			vr_api.compositor, vr::Eye_Right, renderer.device.Get(), &raw_right_srv);

		if (err_l != vr::VRCompositorError_None || !raw_left_srv ||
		    err_r != vr::VRCompositorError_None || !raw_right_srv) {
			blog(LOG_WARNING, "[VR Mirror by U2VR] GetMirrorTextureD3D11 failed: left=(err %d, srv %p), right=(err %d, srv %p)",
			     (int)err_l, raw_left_srv, (int)err_r, raw_right_srv);
			release_mirror_resources(false);
			vr_api.disconnect(true);
			return false;
		}

		uint32_t lw = 0, lh = 0, rw = 0, rh = 0;
		left_unorm_srv = vr_stab::D3D11StabilizerRenderer::create_unorm_srv(
			renderer.device.Get(), reinterpret_cast<ID3D11ShaderResourceView *>(raw_left_srv), &lw, &lh);
		right_unorm_srv = vr_stab::D3D11StabilizerRenderer::create_unorm_srv(
			renderer.device.Get(), reinterpret_cast<ID3D11ShaderResourceView *>(raw_right_srv), &rw, &rh);

		if (!left_unorm_srv || !right_unorm_srv || lw == 0 || lh == 0 || rw == 0 || rh == 0) {
			blog(LOG_WARNING, "[VR Mirror by U2VR] create_unorm_srv failed: left=(srv %p, %ux%u), right=(srv %p, %ux%u)",
			     left_unorm_srv.Get(), lw, lh, right_unorm_srv.Get(), rw, rh);
			release_mirror_resources(true);
			vr_api.disconnect(true);
			return false;
		}

		eye_tex_width = rw;
		eye_tex_height = rh;

		// Query projection frustum tangents for both eyes
		float l_left = -1.0f, l_right = 1.0f, l_top = -1.0f, l_bottom = 1.0f;
		float r_left = -1.0f, r_right = 1.0f, r_top = -1.0f, r_bottom = 1.0f;
		seh_get_projection_raw(vr_api.system, vr::Eye_Left, &l_left, &l_right, &l_top, &l_bottom);
		seh_get_projection_raw(vr_api.system, vr::Eye_Right, &r_left, &r_right, &r_top, &r_bottom);

		normalize_projection(l_left, l_right, l_top, l_bottom, proj_left);
		normalize_projection(r_left, r_right, r_top, r_bottom, proj_right);

		stabilizer.reset();
		QueryPerformanceFrequency(&qpc_freq);
		QueryPerformanceCounter(&last_qpc);

		mirror_ready = true;
		blog(LOG_INFO,
		     "[VR Mirror by U2VR] Capture active: eye=%ux%u, output=%ux%u, projR=[%.2f..%.2f, %.2f..%.2f]",
		     eye_tex_width, eye_tex_height, target_width, target_height,
		     proj_right[0], proj_right[1], proj_right[3], proj_right[2]);
		return true;
	}

	bool poll_hmd_pose(vr::HmdMatrix34_t *out_mat) {
		if (!vr_api.initialized || !vr_api.compositor || !vr_api.system)
			return false;

		// Check if SteamVR sent a Quit or restart event
		vr::VREvent_t ev{};
		while (seh_poll_next_event(vr_api.system, &ev, sizeof(ev))) {
			if (ev.eventType == vr::VREvent_Quit ||
			    ev.eventType == vr::VREvent_DriverRequestedQuit ||
			    ev.eventType == vr::VREvent_RestartRequested) {
				blog(LOG_INFO, "[VR Mirror by U2VR] SteamVR shutdown event (0x%X); disconnecting capture gracefully", ev.eventType);
				server_quitting = true;
				dead_server_pid = vr_api.vrserver_pid;
				full_reset(true);
				return false;
			}
		}

		if (!vr_api.initialized || !vr_api.compositor || !vr_api.system)
			return false;

		// 1. Prefer exact frame timing pose used to render the current compositor frame
		vr::Compositor_FrameTiming timing{};
		timing.m_nSize = sizeof(vr::Compositor_FrameTiming);
		if (seh_get_frame_timing(vr_api.compositor, &timing, 0) && timing.m_HmdPose.bPoseIsValid) {
			*out_mat = timing.m_HmdPose.mDeviceToAbsoluteTracking;
			return true;
		}

		// 2. Fallback to compositor's last HMD render pose
		vr::TrackedDevicePose_t pose{};
		if (seh_get_last_pose(vr_api.compositor, vr::k_unTrackedDeviceIndex_Hmd, &pose, nullptr) == vr::VRCompositorError_None &&
		    pose.bPoseIsValid) {
			*out_mat = pose.mDeviceToAbsoluteTracking;
			return true;
		}

		// 3. Fallback to direct HMD tracking pose (e.g. when sitting in SteamVR Home/Grid)
		vr::TrackedDevicePose_t poses[1] = {};
		seh_get_device_to_absolute_tracking_pose(vr_api.system, vr::TrackingUniverseStanding, 0.0f, poses, 1);
		if (poses[0].bPoseIsValid) {
			*out_mat = poses[0].mDeviceToAbsoluteTracking;
			return true;
		}

		return false;
	}
};

/* =========================================================================
 * OBS Source Callbacks
 * ========================================================================= */
static const char *vr_stab_get_name(void *type_data)
{
	(void)type_data;
	return "VR Mirror by U2VR";
}

static void vr_stab_update(void *data, obs_data_t *settings)
{
	auto *s = reinterpret_cast<VRStabilizerSource *>(data);
	if (!s || !settings)
		return;

	std::lock_guard<std::recursive_mutex> lock(s->state_mutex);

	s->eye_mode        = (int)obs_data_get_int(settings, PROP_EYE_MODE);
	s->smooth_strength = (float)obs_data_get_double(settings, PROP_SMOOTH_STRENGTH);
	s->roll_lock       = (float)obs_data_get_double(settings, PROP_ROLL_LOCK);
	s->max_angle_deg   = (float)obs_data_get_double(settings, PROP_MAX_ANGLE);
	s->zoom_margin     = (float)obs_data_get_double(settings, PROP_ZOOM_MARGIN);
	s->stitch_offset_x = (float)obs_data_get_double(settings, PROP_STITCH_OFFSET_X);
	s->stitch_offset_y = (float)obs_data_get_double(settings, PROP_STITCH_OFFSET_Y);
	s->stitch_feather  = (float)obs_data_get_double(settings, PROP_STITCH_FEATHER);
	s->vert_offset     = (float)obs_data_get_double(settings, PROP_VERT_OFFSET);
	s->horiz_offset    = (float)obs_data_get_double(settings, PROP_HORIZ_OFFSET);
	s->out_res_preset  = (int)obs_data_get_int(settings, PROP_OUT_RESOLUTION);
	s->border_mode     = (int)obs_data_get_int(settings, PROP_BORDER_MODE);

	uint32_t old_w = s->target_width;
	uint32_t old_h = s->target_height;
	s->update_resolution_from_preset();

	if (s->mirror_ready && (old_w != s->target_width || old_h != s->target_height)) {
		s->renderer.ensure_output_target(s->target_width, s->target_height);
	}
}

static void *vr_stab_create(obs_data_t *settings, obs_source_t *source)
{
	auto *s = new VRStabilizerSource();
	s->source = source;
	QueryPerformanceFrequency(&s->qpc_freq);
	QueryPerformanceCounter(&s->last_qpc);
	vr_stab_update(s, settings);
	return s;
}

static void vr_stab_destroy(void *data)
{
	auto *s = reinterpret_cast<VRStabilizerSource *>(data);
	if (!s)
		return;

	{
		std::lock_guard<std::recursive_mutex> lock(s->state_mutex);
		s->full_reset(true);
		s->renderer.shutdown();
		s->vr_api.unload();
	}
	delete s;
}

static uint32_t vr_stab_get_width(void *data)
{
	auto *s = reinterpret_cast<VRStabilizerSource *>(data);
	return s ? s->target_width : 1920;
}

static uint32_t vr_stab_get_height(void *data)
{
	auto *s = reinterpret_cast<VRStabilizerSource *>(data);
	return s ? s->target_height : 1080;
}

static void vr_stab_get_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, PROP_EYE_MODE, 0);              // Right Dominant + Left Stitch
	obs_data_set_default_double(settings, PROP_SMOOTH_STRENGTH, 65.0); // 65% Pitch/Yaw smoothing
	obs_data_set_default_double(settings, PROP_ROLL_LOCK, 75.0);       // 75% Roll horizon lock
	obs_data_set_default_double(settings, PROP_MAX_ANGLE, 8.5);        // 8.5 deg max excursion
	obs_data_set_default_double(settings, PROP_ZOOM_MARGIN, 6.0);      // 6% zoom margin
	obs_data_set_default_double(settings, PROP_STITCH_OFFSET_X, 18.0); // 18% left eye extension
	obs_data_set_default_double(settings, PROP_STITCH_OFFSET_Y, 0.0);  // 0% vertical align
	obs_data_set_default_double(settings, PROP_STITCH_FEATHER, 8.0);   // 8% soft seam feather
	obs_data_set_default_double(settings, PROP_VERT_OFFSET, 0.0);      // 0% vertical center shift
	obs_data_set_default_double(settings, PROP_HORIZ_OFFSET, 0.0);     // 0% horizontal center shift
	obs_data_set_default_int(settings, PROP_OUT_RESOLUTION, RES_1920x1080);
	obs_data_set_default_int(settings, PROP_BORDER_MODE, 0);           // Clamp to edge
}

static bool on_reconnect_clicked(obs_properties_t *props, obs_property_t *property, void *data)
{
	(void)props;
	(void)property;
	auto *s = reinterpret_cast<VRStabilizerSource *>(data);
	if (s) {
		std::lock_guard<std::recursive_mutex> lock(s->state_mutex);
		s->server_quitting = false;
		s->dead_server_pid = 0;
		s->full_reset(true);
		s->try_initialize_capture(true);
	}
	return true;
}

static obs_properties_t *vr_stab_get_properties(void *data)
{
	(void)data;
	obs_properties_t *props = obs_properties_create();

	obs_property_t *p_eye = obs_properties_add_list(
		props, PROP_EYE_MODE, "Режим захвата глаз (Eye Mode)",
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p_eye, "Правый ведущий + Левый слева (Как в SteamVR, прицел правым)", 0);
	obs_property_list_add_int(p_eye, "Левый ведущий + Правый справа (Прицел левым)", 1);
	obs_property_list_add_int(p_eye, "Только правый глаз (16:9)", 2);
	obs_property_list_add_int(p_eye, "Только левый глаз (16:9)", 3);

	obs_property_t *p_smooth = obs_properties_add_float_slider(
		props, PROP_SMOOTH_STRENGTH, "Сила стабилизации (Pitch / Yaw)", 0.0, 100.0, 1.0);
	obs_property_float_set_suffix(p_smooth, " %");

	obs_property_t *p_roll = obs_properties_add_float_slider(
		props, PROP_ROLL_LOCK, "Стабилизация крена / Горизонт (Roll Lock)", 0.0, 100.0, 1.0);
	obs_property_float_set_suffix(p_roll, " %");

	obs_property_t *p_angle = obs_properties_add_float_slider(
		props, PROP_MAX_ANGLE, "Макс. угол компенсации (Лимит при резких поворотах)", 1.0, 25.0, 0.5);
	obs_property_float_set_suffix(p_angle, " °");

	obs_property_t *p_zoom = obs_properties_add_float_slider(
		props, PROP_ZOOM_MARGIN, "Запас кадрирования / Зум (Zoom Margin)", 0.0, 30.0, 0.5);
	obs_property_float_set_suffix(p_zoom, " %");

	obs_property_t *p_stitch_x = obs_properties_add_float_slider(
		props, PROP_STITCH_OFFSET_X, "Расширение вторым глазом / Сдвиг сшивки (По горизонтали)", 0.0, 35.0, 0.5);
	obs_property_float_set_suffix(p_stitch_x, " %");

	obs_property_t *p_stitch_y = obs_properties_add_float_slider(
		props, PROP_STITCH_OFFSET_Y, "Калибровка сшивки по вертикали", -10.0, 10.0, 0.2);
	obs_property_float_set_suffix(p_stitch_y, " %");

	obs_property_t *p_feather = obs_properties_add_float_slider(
		props, PROP_STITCH_FEATHER, "Мягкость шва между глазами (Seam Feather)", 1.0, 25.0, 0.5);
	obs_property_float_set_suffix(p_feather, " %");

	obs_property_t *p_vert = obs_properties_add_float_slider(
		props, PROP_VERT_OFFSET, "Смещение камеры вверх/вниз (Вертикальный центр)", -25.0, 25.0, 0.5);
	obs_property_float_set_suffix(p_vert, " %");

	obs_property_t *p_horiz = obs_properties_add_float_slider(
		props, PROP_HORIZ_OFFSET, "Смещение камеры влево/вправо (Горизонтальный центр)", -20.0, 20.0, 0.5);
	obs_property_float_set_suffix(p_horiz, " %");

	obs_property_t *p_res = obs_properties_add_list(
		props, PROP_OUT_RESOLUTION, "Разрешение вывода",
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p_res, "1920x1080 (Full HD 16:9)", RES_1920x1080);
	obs_property_list_add_int(p_res, "2560x1440 (QHD 16:9)", RES_2560x1440);
	obs_property_list_add_int(p_res, "1280x720 (HD 16:9)", RES_1280x720);
	obs_property_list_add_int(p_res, "3840x2160 (4K 16:9)", RES_3840x2160);

	obs_property_t *p_border = obs_properties_add_list(
		props, PROP_BORDER_MODE, "Обработка краёв при экстремальных рывках",
		OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p_border, "Растягивать край (Clamp — без черных рамок)", 0);
	obs_property_list_add_int(p_border, "Мягкая виньетка", 1);
	obs_property_list_add_int(p_border, "Черные поля (Для отладки зума)", 2);

	obs_properties_add_button(
		props, PROP_RECONNECT,
		"Переподключить к SteamVR / Сбросить стабилизатор",
		on_reconnect_clicked);

	return props;
}

static void vr_stab_video_tick(void *data, float seconds)
{
	(void)seconds;
	auto *s = reinterpret_cast<VRStabilizerSource *>(data);
	if (!s)
		return;

	std::lock_guard<std::recursive_mutex> lock(s->state_mutex);

	if (s->vr_api.initialized && s->vr_api.system) {
		// Poll OpenVR events on EVERY frame, even when this source is NOT visible/rendered
		// (e.g. user switched to 2D scene, hidden source, pause screen)
		vr::VREvent_t ev{};
		while (seh_poll_next_event(s->vr_api.system, &ev, sizeof(ev))) {
			if (ev.eventType == vr::VREvent_Quit ||
			    ev.eventType == vr::VREvent_DriverRequestedQuit ||
			    ev.eventType == vr::VREvent_RestartRequested) {
				blog(LOG_INFO, "[VR Mirror by U2VR] SteamVR shutdown event (0x%X) received in video_tick; disconnecting capture gracefully", ev.eventType);
				s->server_quitting = true;
				s->dead_server_pid = s->vr_api.vrserver_pid;
				s->full_reset(true);
				return;
			}
		}

		// Check if vrserver.exe process ended unexpectedly
		if (!s->vr_api.is_connected_server_alive()) {
			blog(LOG_INFO, "[VR Mirror by U2VR] SteamVR process ended; resetting capture");
			s->server_quitting = true;
			s->dead_server_pid = s->vr_api.vrserver_pid;
			s->full_reset(false);
			return;
		}
	}
}

static void vr_stab_video_render(void *data, gs_effect_t *effect)
{
	(void)effect;
	auto *s = reinterpret_cast<VRStabilizerSource *>(data);
	if (!s)
		return;

	std::lock_guard<std::recursive_mutex> lock(s->state_mutex);

	if (!s->mirror_ready) {
		if (!s->try_initialize_capture(false))
			return;
	}

	if (!s->vr_api.is_connected_server_alive()) {
		blog(LOG_INFO, "[VR Mirror by U2VR] SteamVR no longer running during render, resetting");
		s->server_quitting = true;
		s->dead_server_pid = s->vr_api.vrserver_pid;
		s->full_reset(false);
		return;
	}

	// Calculate high-precision delta time
	LARGE_INTEGER now_qpc{};
	QueryPerformanceCounter(&now_qpc);
	float dt = 1.0f / 60.0f;
	if (s->qpc_freq.QuadPart > 0 && s->last_qpc.QuadPart > 0) {
		dt = (float)(double(now_qpc.QuadPart - s->last_qpc.QuadPart) / double(s->qpc_freq.QuadPart));
	}
	s->last_qpc = now_qpc;

	// Fetch HMD pose and compute 3D stabilization reprojection matrix
	vr_stab::Mat3 r_reproj = vr_stab::Mat3::identity();
	vr::HmdMatrix34_t hmd_pose{};
	if (s->poll_hmd_pose(&hmd_pose)) {
		r_reproj = s->stabilizer.update(
			hmd_pose, dt,
			s->smooth_strength,
			s->roll_lock,
			s->max_angle_deg);
	} else if (!s->mirror_ready) {
		return;
	}

	if (!s->left_unorm_srv || !s->right_unorm_srv) {
		return;
	}

	// Fill shader constant buffer
	vr_stab::ShaderParamsCB cb{};
	cb.rot_row0[0] = r_reproj.m[0][0];
	cb.rot_row0[1] = r_reproj.m[0][1];
	cb.rot_row0[2] = r_reproj.m[0][2];
	cb.rot_row0[3] = 0.0f;

	cb.rot_row1[0] = r_reproj.m[1][0];
	cb.rot_row1[1] = r_reproj.m[1][1];
	cb.rot_row1[2] = r_reproj.m[1][2];
	cb.rot_row1[3] = 0.0f;

	cb.rot_row2[0] = r_reproj.m[2][0];
	cb.rot_row2[1] = r_reproj.m[2][1];
	cb.rot_row2[2] = r_reproj.m[2][2];
	cb.rot_row2[3] = 0.0f;

	for (int i = 0; i < 4; ++i) {
		cb.proj_left[i]  = s->proj_left[i];
		cb.proj_right[i] = s->proj_right[i];
	}

	cb.stitch_params[0] = s->stitch_offset_x * 0.01f;
	cb.stitch_params[1] = s->stitch_offset_y * 0.01f;
	cb.stitch_params[2] = s->stitch_feather * 0.01f;
	cb.stitch_params[3] = s->zoom_margin * 0.01f;

	cb.view_params[0] = (s->eye_tex_height > 0)
		? ((float)s->eye_tex_width / (float)s->eye_tex_height)
		: 1.0f;
	cb.view_params[1] = (s->target_height > 0)
		? ((float)s->target_width / (float)s->target_height)
		: (16.0f / 9.0f);
	cb.view_params[2] = s->vert_offset * 0.01f;
	cb.view_params[3] = s->horiz_offset * 0.01f;

	cb.mode_flags[0] = s->eye_mode;
	cb.mode_flags[1] = s->border_mode;
	cb.mode_flags[2] = 0;
	cb.mode_flags[3] = 0;

	// Render stabilized dual-eye frame into shared D3D11 texture
	s->renderer.render(s->left_unorm_srv.Get(), s->right_unorm_srv.Get(), cb);

	// Draw shared texture into OBS canvas
	if (s->renderer.gs_out_tex) {
		gs_effect_t *opaque = obs_get_base_effect(OBS_EFFECT_OPAQUE);
		if (opaque) {
			while (gs_effect_loop(opaque, "Draw")) {
				obs_source_draw(s->renderer.gs_out_tex, 0, 0, s->target_width, s->target_height, false);
			}
		}
	}
}

/* =========================================================================
 * Module Load / Unload
 * ========================================================================= */
extern "C" OBS_Export bool obs_module_load(void)
{
	struct obs_source_info info{};
	info.id             = "vr_mirror_by_u2vr";
	info.type           = OBS_SOURCE_TYPE_INPUT;
	info.output_flags   = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW;
	info.get_name       = vr_stab_get_name;
	info.create         = vr_stab_create;
	info.destroy        = vr_stab_destroy;
	info.get_width      = vr_stab_get_width;
	info.get_height     = vr_stab_get_height;
	info.get_defaults   = vr_stab_get_defaults;
	info.get_properties = vr_stab_get_properties;
	info.update         = vr_stab_update;
	info.video_tick     = vr_stab_video_tick;
	info.video_render   = vr_stab_video_render;
	info.icon_type      = OBS_ICON_TYPE_GAME_CAPTURE;

	obs_register_source(&info);

	// Also register legacy ID as alias for backwards compatibility
	info.id = "steamvr_stabilized_mirror";
	obs_register_source(&info);

	blog(LOG_INFO, "[VR Mirror by U2VR] Loaded plugin v1.0.0");
	return true;
}

extern "C" OBS_Export void obs_module_unload(void)
{
	blog(LOG_INFO, "[VR Mirror by U2VR] Unloaded plugin");
}
