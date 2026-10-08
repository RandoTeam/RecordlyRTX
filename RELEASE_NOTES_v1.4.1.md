# RecordlyRTX v1.4.1 — NVIDIA RTX Hardware-Accelerated Screen Recording

First official release of **RecordlyRTX**, bringing native NVIDIA RTX GPU acceleration to Recordly for high-resolution, high-framerate screen recording with virtually zero CPU overhead.

---

### Highlights & Features

- **NVIDIA RTX Zero-Copy GPU Pipeline**:
  - Direct Windows Graphics Capture (WGC) frame acquisition onto GPU DXGI surfaces.
  - Hardware-accelerated RGB to NV12 color conversion utilizing `ID3D11VideoProcessor` directly on VRAM.
  - Zero host-memory (CPU RAM) roundtrips during capture and frame feeding.
  - Hardware MFT encoder pipeline using DXGI Device Manager (`IMFDXGIDeviceManager`), leveraging NVIDIA NVENC hardware directly.

- **Granular Recording Presets & RTX Controls**:
  - Integrated directly into the Settings Dashboard adhering strictly to the Recordly HeroUI & Tailwind CSS design system.
  - **Presets**:
    - **Ultra Performance**: Optimal for gaming or heavy workloads, minimal GPU/encoder overhead.
    - **Balanced (Default)**: Sweet-spot efficiency and visual fidelity.
    - **Max Quality**: High-bitrate capture preserving crystal clear text and detail.
    - **Custom**: Total control over all encoder parameters.
  - **Granular Settings**:
    - Codecs: HEVC (H.265) and H.264 (AVC) hardware encoding.
    - Framerates: 30 FPS, 60 FPS, 120 FPS.
    - Rate Control Modes: VBR (Variable Bitrate), CQP (Constant Quality), CBR (Constant Bitrate).
    - Bitrates: Configurable target and peak bitrates (up to 150 Mbps).

- **Full Editor & Telemetry Fidelity**:
  - Zero impact on telemetry: asynchronous cursor tracking (`cursor-monitor.exe` + `uiohook-napi`) continues writing high-frequency pointer coordinates to `*.telemetry.json`.
  - Screen capture keeps `IsCursorCaptureEnabled(false)` so mouse cursors are never baked into video frames, retaining 100% full post-recording editor capabilities (dynamic zoom, cursor styles, clicks, sound effects, motion blur).

---

### Downloads & Verification

| Asset | Description |
| :--- | :--- |
| **`RecordlyRTX-windows-x64.exe`** | Standalone Windows x64 Installer |
| **`RecordlyRTX-windows-x64.exe.blockmap`** | Blockmap for differential auto-updates |
| **`latest.yml`** | Auto-updater metadata manifest |
| **`SHA256SUMS.txt`** | SHA-256 verification checksums |
