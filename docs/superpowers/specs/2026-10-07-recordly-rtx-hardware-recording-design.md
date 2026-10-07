# RecordlyRTX: Hardware-Accelerated Zero-Copy Screen Recording & Settings Specification

## 1. Context and Problem Statement

Recordly is an Electron + React screen recorder with automated cursor telemetry and post-recording zoom/attention animations.
Currently on Windows:
1. **CPU & PCIe Bottleneck:** The native capture pipeline (`wgc-capture`) captures desktop textures via Windows Graphics Capture (WGC) into Direct3D 11 textures. However, every frame is transferred back to CPU memory over the PCIe bus (`D3D11_USAGE_STAGING` and `context->Map(..., D3D11_MAP_READ)`), converted from BGRA to NV12 on the CPU in software (`convertBgraToBt709LimitedNv12`), and copied into an `IMFMediaBuffer` in system RAM. This causes 15-40% CPU overhead, potential frame drops at 4K/60fps, and unnecessary PCIe bandwidth usage.
2. **Fixed Recording Parameters:** Users cannot configure resolution, framerate (30 / 60 / 120 FPS), codecs (H.264 vs HEVC), or bitrates / quality presets from the UI.
3. **Cursor & Telemetry Integrity:** The user requires that cursor tracking, click telemetry, dwell detection, and auto-zoom post-editing capabilities remain 100% operational without regression or CPU bloat.

## 2. Architecture & Design Principles

### 2.1 Zero-Copy D3D11 Video Pipeline
- **No CPU Readback:** Captured frames remain exclusively in GPU VRAM throughout the capture and encode loop.
- **Direct3D 11 Video Processor / Compute Conversion:** BGRA to NV12 color conversion is performed on the GPU using `ID3D11VideoProcessor` (hardware video processing engine) or directly accepted via D3D11 textures.
- **Hardware SinkWriter Integration:** Media Foundation SinkWriter is initialized with `IMFDXGIDeviceManager` (`MF_SINK_WRITER_D3D_MANAGER`), allowing direct submission of Direct3D 11 texture surfaces via `MFCreateDXGISurfaceBuffer`.
- **NVENC ASIC Utilization:** On NVIDIA RTX GPUs, Media Foundation automatically routes D3D11 hardware surfaces to the NVIDIA NVENC MFT (`nvEncodeAPI64.dll`). The 3D render cores and CPU stay completely unloaded (< 1% CPU, ~0% 3D engine).
- **Graceful Fallback:** If D3D11 device manager initialization fails on a non-standard device, the encoder falls back seamlessly to the staging texture path.

### 2.2 Telemetry & Post-Processing Preservation
- `wgc-capture` keeps `session_.IsCursorCaptureEnabled(false)`. The recorded video contains a pristine desktop image with zero cursor baking.
- `uiohook-napi` and `cursor-monitor.exe` continue capturing global mouse clicks and cursor state asynchronously via OS hooks (`WH_MOUSE_LL` and `GetCursorInfo`), saving to `*.telemetry.json`.
- The video editor, timeline, dwell analysis (`zoomSuggestionUtils.ts`), and SVG cursor overlay remain completely unaffected.

### 2.3 RTX Recording Settings & UI Design
Following Recordly's design system (`@heroui/react`, `SettingsRow`, Tailwind CSS):
- Added to **Dashboard Settings -> Recording** category.
- **Presets:**
  - `ultra-performance`: 60 FPS, H.264, NVENC P1/P2, VBR 20 Mbps.
  - `balanced`: 60 FPS, HEVC (or H.264), NVENC P4, VBR 35 Mbps.
  - `max-quality`: 60/120 FPS, HEVC, NVENC P7, CQP (QP 19).
  - `custom`: Exposes individual controls for Codec, FPS, Rate Control, Bitrate, and CQP level.
- **Persistence:** Stored in `APP_SETTINGS_FILE` (`settings.json`) via existing `readAppSetting`/`writeAppSetting` (`app-settings:get` and `app-settings:set`).

## 3. Data Flow

```
[Screen/Window]
       │
       ▼ (WGC Direct3D 11 Texture - VRAM)
[D3D11 Captured Texture (BGRA)]
       │
       ▼ (Zero-Copy VRAM: ID3D11VideoProcessor / Direct D3D11)
[D3D11 NV12 Texture in VRAM]
       │
       ▼ (MFCreateDXGISurfaceBuffer - 0% CPU)
[IMFMediaBuffer (VRAM surface)]
       │
       ▼ (IMFDXGIDeviceManager -> NVIDIA NVENC MFT)
[Hardware H.264 / HEVC Bitstream]
       │
       ▼
[Output .mp4 File on NVMe/SSD]
```

Parallel Telemetry Flow:
```
[User Mouse Actions]
       │
       ├─► [uiohook-napi: clicks, movements] ──┐
       │                                        ├──► [*.telemetry.json] ──► [Recordly Editor Auto-Zoom]
       └─► [cursor-monitor: Win32 cursor shape] ─┘
```

## 4. Interfaces & Contracts

### 4.1 Settings Schema (`src/types/rtxSettings.ts`)
```typescript
export type RtxPreset = 'ultra-performance' | 'balanced' | 'max-quality' | 'custom';
export type RtxCodec = 'h264' | 'hevc';
export type RtxFramerate = 30 | 60 | 120;
export type RtxRateControl = 'cqp' | 'vbr' | 'cbr';

export interface RtxRecordingSettings {
    preset: RtxPreset;
    codec: RtxCodec;
    fps: RtxFramerate;
    rateControl: RtxRateControl;
    bitrateMbps: number; // 5 - 150
    cqpLevel: number;    // 16 - 32 (lower = higher quality)
    zeroCopy: boolean;   // default true
}
```

### 4.2 Native Capture Configuration (`wgc-capture` JSON protocol)
Extended fields in JSON passed to `wgc-capture.exe`:
```json
{
  "outputPath": "...",
  "fps": 60,
  "codec": "hevc",
  "bitrate": 35000000,
  "rateControl": "vbr",
  "qp": 20,
  "zeroCopy": true,
  "qualityPreset": "balanced"
}
```

## 5. Testing & Verification Strategy
1. **Unit & Store Tests:** Verify settings defaults, serialization, and IPC roundtrip.
2. **Native Capture Test:** Verify `wgc-capture.exe` starts, encodes video with Zero-Copy D3D11 surface buffer, outputs valid H.264/HEVC MP4 file.
3. **Telemetry Test:** Verify `*.telemetry.json` sidecar is correctly written and auto-zoom regions populate in the editor.
4. **Performance Verification:** Verify CPU usage drops to negligible levels during active recording.
