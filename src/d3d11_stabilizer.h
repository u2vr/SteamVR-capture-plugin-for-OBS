#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstdint>
#include <cstring>
#include <algorithm>

#include "../third_party/obs/obs_minimal.h"
#include "stabilizer_math.h"

namespace vr_stab {

using Microsoft::WRL::ComPtr;

// Must be 16-byte aligned for D3D11 Constant Buffer
struct alignas(16) ShaderParamsCB {
	// 3x3 Reprojection matrix rows (padded to float4)
	float rot_row0[4]; // m[0][0], m[0][1], m[0][2], 0
	float rot_row1[4]; // m[1][0], m[1][1], m[1][2], 0
	float rot_row2[4]; // m[2][0], m[2][1], m[2][2], 0

	// Frustum tangents: (left_tan, right_tan, top_tan_pos, bottom_tan_neg)
	float proj_left[4];
	float proj_right[4];

	// x = stitch_offset_x (0..0.40)
	// y = stitch_offset_y (-0.15..0.15)
	// z = stitch_feather  (0.01..0.30)
	// w = zoom_margin     (0..0.35)
	float stitch_params[4];

	// x = eye_aspect (tex_w / tex_h)
	// y = out_aspect (out_w / out_h)
	// z = vertical_offset (-0.30..0.30)
	// w = horizontal_offset (-0.25..0.25)
	float view_params[4];

	// x = eye_mode (0 = RightDom+Left, 1 = LeftDom+Right, 2 = RightOnly, 3 = LeftOnly)
	// y = border_mode (0 = Clamp, 1 = Soft Vignette, 2 = Black)
	// z = seam_position (default 0.025)
	// w = reserved
	int32_t mode_flags[4];
};

static const char *kStabilizerHLSL = R"HLSL(
cbuffer StabilizerCB : register(b0)
{
    float4 rot_row0;
    float4 rot_row1;
    float4 rot_row2;
    float4 proj_left;
    float4 proj_right;
    float4 stitch_params; // x: offset_x, y: offset_y, z: feather, w: zoom_margin
    float4 view_params;   // x: eye_aspect, y: out_aspect, z: vert_offset, w: horiz_offset
    int4   mode_flags;    // x: eye_mode, y: border_mode, z: unused, w: unused
};

Texture2D tex_left  : register(t0);
Texture2D tex_right : register(t1);
SamplerState samp_linear : register(s0);

struct VSOut
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

VSOut VSMain(uint vid : SV_VertexID)
{
    VSOut o;
    // Fullscreen triangle
    o.uv = float2((vid << 1) & 2, vid & 2);
    o.pos = float4(o.uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return o;
}

float3 RotateRay(float3 d)
{
    return float3(
        dot(rot_row0.xyz, d),
        dot(rot_row1.xyz, d),
        dot(rot_row2.xyz, d)
    );
}

float ComputeBorderWeight(float2 uv, int border_mode)
{
    if (border_mode == 0)
        return 1.0; // Clamp to edge without darkening

    float2 dist = min(uv, 1.0 - uv);
    float min_d = min(dist.x, dist.y);

    if (border_mode == 2)
        return (min_d >= 0.0) ? 1.0 : 0.0; // Hard black border outside [0, 1]

    // Soft vignette near edges
    return smoothstep(-0.015, 0.025, min_d);
}

float4 PSMain(VSOut pin) : SV_Target
{
    int eye_mode    = mode_flags.x;
    int border_mode = mode_flags.y;

    float stitch_dx   = stitch_params.x;
    float stitch_dy   = stitch_params.y;
    float feather     = max(stitch_params.z, 0.005);
    float zoom_margin = stitch_params.w;

    float eye_aspect  = max(view_params.x, 0.1);
    float out_aspect  = max(view_params.y, 0.1);
    float vert_offset = view_params.z;
    float horiz_offset = view_params.w;

    float scale = 1.0 / (1.0 + max(zoom_margin, 0.0));

    // Choose dominant eye projection frustum for virtual camera ray construction
    // proj = (tan_left, tan_right, tan_top_pos, tan_bottom_neg)
    float4 dom_proj = (eye_mode == 1 || eye_mode == 3) ? proj_left : proj_right;

    // Determine horizontal UV span in dominant eye's normalized coordinate space
    float u_min = 0.0;
    float u_max = 1.0;
    if (eye_mode == 0)
    {
        // Right eye dominant [0, 1], Left eye extends the left side to [-stitch_dx, 0]
        u_min = -stitch_dx;
        u_max = 1.0;
    }
    else if (eye_mode == 1)
    {
        // Left eye dominant [0, 1], Right eye extends the right side to [1, 1 + stitch_dx]
        u_min = 0.0;
        u_max = 1.0 + stitch_dx;
    }

    float u_center = 0.5 * (u_min + u_max) + horiz_offset;
    float u_half   = 0.5 * (u_max - u_min) * scale;

    // Preserve 1:1 geometric aspect ratio (no stretching on 16:9 canvas)
    float v_half   = u_half * (eye_aspect / out_aspect);
    float v_center = 0.5 - vert_offset;

    // Virtual camera UV before 3D rotation
    float u_cam = u_center + (pin.uv.x * 2.0 - 1.0) * u_half;
    float v_cam = v_center + (pin.uv.y * 2.0 - 1.0) * v_half;

    // Map virtual camera UV to 3D ray in virtual camera space (+X right, +Y up, -Z forward)
    float tan_x_cam = lerp(dom_proj.x, dom_proj.y, u_cam);
    float tan_y_cam = lerp(dom_proj.z, dom_proj.w, v_cam);
    float3 ray_cam  = float3(tan_x_cam, tan_y_cam, -1.0);

    // Rotate 3D ray into instantaneous HMD space using R_reproj = R_raw^T * R_smooth
    float3 ray_hmd = RotateRay(ray_cam);
    float z_fwd    = max(-ray_hmd.z, 0.001);
    float tan_x_hmd = ray_hmd.x / z_fwd;
    float tan_y_hmd = ray_hmd.y / z_fwd;

    // Reproject back into dominant eye UV coordinates
    float u_dom = (tan_x_hmd - dom_proj.x) / max(dom_proj.y - dom_proj.x, 1e-4);
    float v_dom = (dom_proj.z - tan_y_hmd) / max(dom_proj.z - dom_proj.w, 1e-4);

    float4 final_color = float4(0.0, 0.0, 0.0, 1.0);

    if (eye_mode == 2)
    {
        // Right Eye Only
        float2 uv_r = float2(u_dom, v_dom);
        float w_b = ComputeBorderWeight(uv_r, border_mode);
        final_color = tex_right.Sample(samp_linear, saturate(uv_r)) * w_b;
    }
    else if (eye_mode == 3)
    {
        // Left Eye Only
        float2 uv_l = float2(u_dom, v_dom);
        float w_b = ComputeBorderWeight(uv_l, border_mode);
        final_color = tex_left.Sample(samp_linear, saturate(uv_l)) * w_b;
    }
    else if (eye_mode == 0)
    {
        // Mode 0: Right Eye Dominant + Left Eye peripheral stitch on the left (SteamVR style)
        float2 uv_r = float2(u_dom, v_dom);
        float2 uv_l = float2(u_dom + stitch_dx, v_dom + stitch_dy);

        // Smooth blend seam near the left edge of the Right Eye [seam_start .. seam_end]
        float seam_start = 0.02;
        float seam_end   = seam_start + feather;
        float w_right    = smoothstep(seam_start, seam_end, uv_r.x);

        // If Left Eye runs out of valid right-side pixels, use Right Eye
        if (uv_l.x > 0.985)
            w_right = max(w_right, smoothstep(0.985, 0.998, uv_l.x));

        float4 col_r = tex_right.Sample(samp_linear, saturate(uv_r));
        float4 col_l = tex_left.Sample(samp_linear, saturate(uv_l));

        float2 combined_uv = float2((u_dom - u_min) / max(u_max - u_min, 1e-4), v_dom);
        float w_b = ComputeBorderWeight(combined_uv, border_mode);

        final_color = lerp(col_l, col_r, w_right) * w_b;
    }
    else
    {
        // Mode 1: Left Eye Dominant + Right Eye peripheral stitch on the right
        float2 uv_l = float2(u_dom, v_dom);
        float2 uv_r = float2(u_dom - stitch_dx, v_dom - stitch_dy);

        float seam_end   = 0.98;
        float seam_start = seam_end - feather;
        float w_left     = 1.0 - smoothstep(seam_start, seam_end, uv_l.x);

        if (uv_r.x < 0.015)
            w_left = max(w_left, 1.0 - smoothstep(0.002, 0.015, uv_r.x));

        float4 col_l = tex_left.Sample(samp_linear, saturate(uv_l));
        float4 col_r = tex_right.Sample(samp_linear, saturate(uv_r));

        float2 combined_uv = float2((u_dom - u_min) / max(u_max - u_min, 1e-4), v_dom);
        float w_b = ComputeBorderWeight(combined_uv, border_mode);

        final_color = lerp(col_r, col_l, w_left) * w_b;
    }

    final_color.a = 1.0;
    return final_color;
}
)HLSL";

class D3D11StabilizerRenderer {
public:
	ComPtr<ID3D11Device> device;
	ComPtr<ID3D11DeviceContext> context;
	ComPtr<ID3D11VertexShader> vs;
	ComPtr<ID3D11PixelShader> ps;
	ComPtr<ID3D11Buffer> cb;
	ComPtr<ID3D11SamplerState> sampler;

	ComPtr<ID3D11Texture2D> out_tex;
	ComPtr<ID3D11RenderTargetView> out_rtv;
	gs_texture_t *gs_out_tex = nullptr;
	uint32_t out_width = 0;
	uint32_t out_height = 0;

	bool init_device(ID3D11Device *obs_device) {
		if (!obs_device) {
			blog(LOG_ERROR, "[VR Mirror by U2VR] init_device: obs_device is NULL");
			return false;
		}

		ComPtr<IDXGIDevice> dxgi_dev;
		HRESULT hr = obs_device->QueryInterface(__uuidof(IDXGIDevice), (void **)&dxgi_dev);
		if (FAILED(hr)) {
			blog(LOG_ERROR, "[VR Mirror by U2VR] QueryInterface IDXGIDevice failed: 0x%08X", (unsigned)hr);
			return false;
		}

		ComPtr<IDXGIAdapter> adapter;
		hr = dxgi_dev->GetAdapter(&adapter);
		if (FAILED(hr)) {
			blog(LOG_ERROR, "[VR Mirror by U2VR] GetAdapter failed: 0x%08X", (unsigned)hr);
			return false;
		}

		DXGI_ADAPTER_DESC ad_desc{};
		if (SUCCEEDED(adapter->GetDesc(&ad_desc))) {
			blog(LOG_INFO, "[VR Mirror by U2VR] Creating D3D11 device on adapter: %ls", ad_desc.Description);
		}

		D3D_FEATURE_LEVEL levels[] = {
			D3D_FEATURE_LEVEL_11_1,
			D3D_FEATURE_LEVEL_11_0,
		};
		D3D_FEATURE_LEVEL achieved = D3D_FEATURE_LEVEL_11_0;

		hr = D3D11CreateDevice(
			adapter.Get(),
			D3D_DRIVER_TYPE_UNKNOWN,
			nullptr,
			D3D11_CREATE_DEVICE_BGRA_SUPPORT,
			levels,
			2,
			D3D11_SDK_VERSION,
			&device,
			&achieved,
			&context
		);
		if (FAILED(hr)) {
			hr = D3D11CreateDevice(
				adapter.Get(),
				D3D_DRIVER_TYPE_UNKNOWN,
				nullptr,
				D3D11_CREATE_DEVICE_BGRA_SUPPORT,
				&levels[1],
				1,
				D3D11_SDK_VERSION,
				&device,
				&achieved,
				&context
			);
			if (FAILED(hr)) {
				blog(LOG_ERROR, "[VR Mirror by U2VR] D3D11CreateDevice failed: 0x%08X", (unsigned)hr);
				return false;
			}
		}

		blog(LOG_INFO, "[VR Mirror by U2VR] D3D11 device created successfully (feature level 0x%X)", (unsigned)achieved);
		return init_pipeline();
	}

	bool init_pipeline() {
		ComPtr<ID3DBlob> vs_blob, ps_blob, err_blob;
		HRESULT hr = D3DCompile(
			kStabilizerHLSL,
			std::strlen(kStabilizerHLSL),
			"StabilizerShader",
			nullptr,
			nullptr,
			"VSMain",
			"vs_5_0",
			D3DCOMPILE_OPTIMIZATION_LEVEL3,
			0,
			&vs_blob,
			&err_blob
		);
		if (FAILED(hr)) {
			if (err_blob)
				blog(LOG_ERROR, "[VR Mirror by U2VR] VS compile error: %s", (const char *)err_blob->GetBufferPointer());
			else
				blog(LOG_ERROR, "[VR Mirror by U2VR] VS D3DCompile failed: 0x%08X", (unsigned)hr);
			return false;
		}

		err_blob.Reset();
		hr = D3DCompile(
			kStabilizerHLSL,
			std::strlen(kStabilizerHLSL),
			"StabilizerShader",
			nullptr,
			nullptr,
			"PSMain",
			"ps_5_0",
			D3DCOMPILE_OPTIMIZATION_LEVEL3,
			0,
			&ps_blob,
			&err_blob
		);
		if (FAILED(hr)) {
			if (err_blob)
				blog(LOG_ERROR, "[VR Mirror by U2VR] PS compile error: %s", (const char *)err_blob->GetBufferPointer());
			else
				blog(LOG_ERROR, "[VR Mirror by U2VR] PS D3DCompile failed: 0x%08X", (unsigned)hr);
			return false;
		}

		if (FAILED(device->CreateVertexShader(vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), nullptr, &vs))) {
			blog(LOG_ERROR, "[VR Mirror by U2VR] CreateVertexShader failed");
			return false;
		}
		if (FAILED(device->CreatePixelShader(ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(), nullptr, &ps))) {
			blog(LOG_ERROR, "[VR Mirror by U2VR] CreatePixelShader failed");
			return false;
		}

		D3D11_BUFFER_DESC bd{};
		bd.ByteWidth = sizeof(ShaderParamsCB);
		bd.Usage = D3D11_USAGE_DYNAMIC;
		bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		if (FAILED(device->CreateBuffer(&bd, nullptr, &cb))) {
			blog(LOG_ERROR, "[VR Mirror by U2VR] CreateBuffer constant buffer failed");
			return false;
		}

		D3D11_SAMPLER_DESC sd{};
		sd.Filter = D3D11_FILTER_ANISOTROPIC;
		sd.MaxAnisotropy = 4;
		sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		sd.MaxLOD = D3D11_FLOAT32_MAX;
		if (FAILED(device->CreateSamplerState(&sd, &sampler)))
			return false;

		return true;
	}

	bool ensure_output_target(uint32_t width, uint32_t height) {
		if (out_tex && out_rtv && gs_out_tex && out_width == width && out_height == height)
			return true;

		release_output_target();

		D3D11_TEXTURE2D_DESC td{};
		td.Width = width;
		td.Height = height;
		td.MipLevels = 1;
		td.ArraySize = 1;
		td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		td.SampleDesc.Count = 1;
		td.Usage = D3D11_USAGE_DEFAULT;
		td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		td.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

		if (FAILED(device->CreateTexture2D(&td, nullptr, &out_tex))) {
			blog(LOG_ERROR, "[VR Mirror by U2VR] ensure_output_target: CreateTexture2D failed (%ux%u)", width, height);
			return false;
		}

		if (FAILED(device->CreateRenderTargetView(out_tex.Get(), nullptr, &out_rtv))) {
			blog(LOG_ERROR, "[VR Mirror by U2VR] ensure_output_target: CreateRenderTargetView failed");
			out_tex.Reset();
			return false;
		}

		ComPtr<IDXGIResource> dxgi_res;
		if (FAILED(out_tex.As(&dxgi_res))) {
			blog(LOG_ERROR, "[VR Mirror by U2VR] ensure_output_target: QueryInterface IDXGIResource failed");
			release_output_target();
			return false;
		}

		HANDLE shared_handle = nullptr;
		if (FAILED(dxgi_res->GetSharedHandle(&shared_handle)) || !shared_handle) {
			blog(LOG_ERROR, "[VR Mirror by U2VR] ensure_output_target: GetSharedHandle failed");
			release_output_target();
			return false;
		}

		obs_enter_graphics();
		gs_out_tex = gs_texture_open_shared((uint32_t)(uintptr_t)shared_handle);
		obs_leave_graphics();

		if (!gs_out_tex) {
			blog(LOG_ERROR, "[VR Mirror by U2VR] ensure_output_target: gs_texture_open_shared failed for handle %p", shared_handle);
			release_output_target();
			return false;
		}

		out_width = width;
		out_height = height;
		return true;
	}

	static ComPtr<ID3D11ShaderResourceView> create_unorm_srv(ID3D11Device *dev, ID3D11ShaderResourceView *orig_srv, uint32_t *out_w, uint32_t *out_h) {
		if (!dev || !orig_srv)
			return nullptr;

		ComPtr<ID3D11Resource> res;
		orig_srv->GetResource(&res);
		if (!res)
			return orig_srv;

		ComPtr<ID3D11Texture2D> tex;
		if (FAILED(res.As(&tex)) || !tex)
			return orig_srv;

		D3D11_TEXTURE2D_DESC desc{};
		tex->GetDesc(&desc);
		if (out_w) *out_w = desc.Width;
		if (out_h) *out_h = desc.Height;

		DXGI_FORMAT srv_fmt = desc.Format;
		switch (desc.Format) {
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
		case DXGI_FORMAT_R8G8B8A8_TYPELESS:
			srv_fmt = DXGI_FORMAT_R8G8B8A8_UNORM;
			break;
		case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
		case DXGI_FORMAT_B8G8R8A8_TYPELESS:
			srv_fmt = DXGI_FORMAT_B8G8R8A8_UNORM;
			break;
		case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
		case DXGI_FORMAT_B8G8R8X8_TYPELESS:
			srv_fmt = DXGI_FORMAT_B8G8R8X8_UNORM;
			break;
		default:
			return orig_srv;
		}

		D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
		sd.Format = srv_fmt;
		sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		sd.Texture2D.MostDetailedMip = 0;
		sd.Texture2D.MipLevels = 1;

		ComPtr<ID3D11ShaderResourceView> unorm_srv;
		if (SUCCEEDED(dev->CreateShaderResourceView(tex.Get(), &sd, &unorm_srv)) && unorm_srv)
			return unorm_srv;

		return orig_srv;
	}

	void render(ID3D11ShaderResourceView *left_srv, ID3D11ShaderResourceView *right_srv, const ShaderParamsCB &params) {
		if (!context || !out_rtv || !vs || !ps || !cb || !sampler)
			return;

		D3D11_MAPPED_SUBRESOURCE mapped{};
		if (SUCCEEDED(context->Map(cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
			std::memcpy(mapped.pData, &params, sizeof(ShaderParamsCB));
			context->Unmap(cb.Get(), 0);
		}

		D3D11_VIEWPORT vp{};
		vp.Width = (float)out_width;
		vp.Height = (float)out_height;
		vp.MinDepth = 0.0f;
		vp.MaxDepth = 1.0f;

		ID3D11RenderTargetView *rtvs[1] = { out_rtv.Get() };
		ID3D11Buffer *cbs[1] = { cb.Get() };
		ID3D11ShaderResourceView *srvs[2] = { left_srv, right_srv };
		ID3D11SamplerState *samps[1] = { sampler.Get() };

		context->OMSetRenderTargets(1, rtvs, nullptr);
		context->RSSetViewports(1, &vp);
		context->IASetInputLayout(nullptr);
		context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		context->VSSetShader(vs.Get(), nullptr, 0);
		context->PSSetShader(ps.Get(), nullptr, 0);
		context->PSSetConstantBuffers(0, 1, cbs);
		context->PSSetShaderResources(0, 2, srvs);
		context->PSSetSamplers(0, 1, samps);

		context->Draw(3, 0);

		// Unbind SRVs so SteamVR compositor can write next frame freely
		ID3D11ShaderResourceView *null_srvs[2] = { nullptr, nullptr };
		context->PSSetShaderResources(0, 2, null_srvs);
		context->Flush();
	}

	void release_output_target() {
		if (gs_out_tex) {
			obs_enter_graphics();
			gs_texture_destroy(gs_out_tex);
			obs_leave_graphics();
			gs_out_tex = nullptr;
		}
		out_rtv.Reset();
		out_tex.Reset();
		out_width = 0;
		out_height = 0;
	}

	void shutdown() {
		release_output_target();
		sampler.Reset();
		cb.Reset();
		ps.Reset();
		vs.Reset();
		context.Reset();
		device.Reset();
	}
};

} // namespace vr_stab
