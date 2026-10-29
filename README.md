# VR Mirror by U2VR (`obs-vr-mirror-by-u2vr`)

**English** | [Русская версия](README_RUS.md)

Native C++/DirectX 11 plugin for **OBS Studio (30.x / 31.x / 32.x)** designed specifically for capturing **SteamVR (including Meta Quest 3 via SteamLink / AirLink / Virtual Desktop)** with **hardware sensor-based 3D stabilization** and **seamless dual-eye compositing** (dominant right eye with sights/crosshairs + peripheral left eye expansion to 16:9).

---

## Key Features

1. **Direct GPU Capture of Both Eyes from SteamVR (`IVRCompositor::GetMirrorTextureD3D11`)**
   * Unlike standard SteamVR mirror window capture (where up to 40% of the vertical field of view is already cropped to 16:9), this plugin retrieves **complete square (1:1) raw textures of both left and right eyes** directly from VRAM.
   * Vertical (Pitch) and roll (Roll) stabilization operates using **actual rendered game pixels at the top and bottom**, avoiding black borders and preserving image clarity without artificial zoom.

2. **Accurate Frame Pose Synchronization (`Compositor_FrameTiming::m_HmdPose`)**
   * The plugin samples the exact tracking rotation matrix from the HMD sensors corresponding to the rendered frame ($\mathbf{R}_{\text{reproj}} = \mathbf{R}_{\text{raw}}^T \mathbf{R}_{\text{smooth}}$), eliminating phase latency and "jelly" artifacts during fast head motions.

3. **SteamVR-Style Dual Eye Mode ("Right Dominant + Left Peripheral")**
   * The right eye covers the central and primary viewport (~82–85%), ensuring collimator and optical sights align **exactly where you aim** with no double vision or parallax distortion.
   * The left eye fills the peripheral area on the left using a soft gradient blend seam (`smoothstep`), constructing a wide 16:9 image.

4. **Adaptive SLERP Filtering + Horizon Lock (Roll Lock)**
   * Damps physiological micro-tremors and vibrations from lightweight headsets (such as the Meta Quest 3).
   * Automatically accelerates during deliberate rapid 90°–180° turns (angular clamp limit), preventing the camera from dragging behind the player.
   * Dedicated **Roll Lock / Horizon Stabilizer** eliminates side-to-side horizon tilt (the primary cause of viewer motion sickness on streams).

---

## Project Structure

* [`CMakeLists.txt`](CMakeLists.txt) — CMake build configuration.
* [`src/plugin-main.cpp`](src/plugin-main.cpp) — OBS plugin entry point, dynamic OpenVR loading, and source properties.
* [`src/stabilizer_math.h`](src/stabilizer_math.h) — Quaternion mathematics, adaptive SLERP filter, and horizon stabilization (Roll Lock).
* [`src/d3d11_stabilizer.h`](src/d3d11_stabilizer.h) — D3D11 renderer and HLSL shader for 3D reprojection and dual-eye blending.
* [`Build-Plugin.ps1`](Build-Plugin.ps1) — Release x64 build script.
* [`Install-Plugin.ps1`](Install-Plugin.ps1) — Automatic installation script for OBS Studio plugin directories.

---

## Installation

### Option 1: Pre-built Release (Recommended)

1. Download the release archive (`obs-vr-mirror-by-u2vr-v*.zip`) from the [Releases](https://github.com/alextrof94/SteamVR-capture-plugin-for-OBS/releases) page.
2. Close OBS Studio if it is running.
3. Extract the archive into your OBS Studio installation root directory (e.g. `C:\Program Files\obs-studio` or `D:\Steam\steamapps\common\OBS Studio`).
   
   The plugin binaries must be placed in `obs-plugins\64bit`:
   * `<OBS_DIR>\obs-plugins\64bit\obs-vr-mirror-by-u2vr.dll`
   * `<OBS_DIR>\obs-plugins\64bit\openvr_api.dll`
4. Launch OBS Studio.

### Option 2: Build from Source

1. **Prerequisites:**
   * Windows 10/11 (64-bit)
   * Visual Studio 2022 with "Desktop development with C++" workload (MSVC v143, x64)
   * CMake 3.20+
2. **Build:**
   Run the PowerShell build script from the repository root:
   ```powershell
   .\Build-Plugin.ps1
   ```
   The compiled binaries will be output to `dist\obs-vr-mirror-by-u2vr\bin\64bit\`.
3. **Install via script:**
   ```powershell
   .\Install-Plugin.ps1 -RestartOBS
   ```
   The script locates OBS Studio installation folders automatically, stops the OBS process if running, copies the libraries, and restarts OBS Studio.

---

## OBS Studio Usage

1. In OBS Studio, click **«+»** under the **Sources** list in your scene.
2. Select **«VR Mirror by U2VR»**.
3. Recommended settings (configured as defaults):
   * **Capture Mode:** `Right Dominant + Left Peripheral (SteamVR style, right-eye aim)`
   * **Stabilization Strength (Pitch / Yaw):** `65 %`
   * **Roll Lock / Horizon Stabilizer:** `75 %`
   * **Max Compensation Angle:** `8.5 °`
   * **Crop / Zoom Margin:** `6.0 %`
   * **Second Eye Extension / Seam Offset:** `18.0 %`
   * **Seam Softness:** `8.0 %`
   * **Camera Vertical Offset:** If in shooter games the crosshair aims too low near the bottom of the screen, set to `+5 % .. +10 %` to raise the viewpoint utilizing the extra vertical margin of the square eye texture.
