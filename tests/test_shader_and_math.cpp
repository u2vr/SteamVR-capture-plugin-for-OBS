#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstring>
#include <cassert>

#include "../src/stabilizer_math.h"
#include "../src/d3d11_stabilizer.h"

using Microsoft::WRL::ComPtr;

int main()
{
	// 1. Verify HLSL Vertex Shader & Pixel Shader compile with zero errors
	ComPtr<ID3DBlob> vs_blob, ps_blob, err_blob;
	HRESULT hr = D3DCompile(
		vr_stab::kStabilizerHLSL,
		std::strlen(vr_stab::kStabilizerHLSL),
		"StabilizerShader",
		nullptr,
		nullptr,
		"VSMain",
		"vs_5_0",
		D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_WARNINGS_ARE_ERRORS,
		0,
		&vs_blob,
		&err_blob
	);
	if (FAILED(hr)) {
		std::printf("VS Compile Failed: %s\n", err_blob ? (const char *)err_blob->GetBufferPointer() : "unknown");
		return 1;
	}
	std::printf("[PASS] Vertex Shader compiled (%zu bytes)\n", vs_blob->GetBufferSize());

	err_blob.Reset();
	hr = D3DCompile(
		vr_stab::kStabilizerHLSL,
		std::strlen(vr_stab::kStabilizerHLSL),
		"StabilizerShader",
		nullptr,
		nullptr,
		"PSMain",
		"ps_5_0",
		D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_WARNINGS_ARE_ERRORS,
		0,
		&ps_blob,
		&err_blob
	);
	if (FAILED(hr)) {
		std::printf("PS Compile Failed: %s\n", err_blob ? (const char *)err_blob->GetBufferPointer() : "unknown");
		return 1;
	}
	std::printf("[PASS] Pixel Shader compiled (%zu bytes)\n", ps_blob->GetBufferSize());

	// 2. Verify PoseStabilizer math
	vr_stab::PoseStabilizer stab;
	vr::HmdMatrix34_t ident{};
	ident.m[0][0] = 1.0f;
	ident.m[1][1] = 1.0f;
	ident.m[2][2] = 1.0f;

	vr_stab::Mat3 r0 = stab.update(ident, 1.0f / 90.0f, 65.0f, 75.0f, 8.5f);
	assert(std::fabs(r0.m[0][0] - 1.0f) < 1e-4f);
	assert(std::fabs(r0.m[1][1] - 1.0f) < 1e-4f);
	assert(std::fabs(r0.m[2][2] - 1.0f) < 1e-4f);

	// Simulate sudden 2-degree pitch UP of the HMD (+alpha around +X)
	float alpha = 2.0f * (3.1415926535f / 180.0f);
	vr::HmdMatrix34_t pitch_up = ident;
	pitch_up.m[1][1] =  std::cos(alpha);
	pitch_up.m[1][2] = -std::sin(alpha);
	pitch_up.m[2][1] =  std::sin(alpha);
	pitch_up.m[2][2] =  std::cos(alpha);

	vr_stab::Mat3 r_reproj = stab.update(pitch_up, 1.0f / 90.0f, 80.0f, 75.0f, 8.5f);
	// Forward ray (0, 0, -1) in virtual camera should map to negative Y in HMD space (lower half of HMD view)
	float hmd_y = r_reproj.m[1][2] * (-1.0f);
	assert(hmd_y < -0.02f);
	std::printf("[PASS] Stabilizer pitch-up compensation verified (hmd_y = %.4f)\n", hmd_y);

	std::printf("[ALL TESTS PASSED]\n");
	return 0;
}
