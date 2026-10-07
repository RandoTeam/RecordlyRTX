# RecordlyRTX Hardware-Accelerated Recording Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Transform Recordly screen recording into a GPU-native, zero-copy RTX pipeline with customizable presets (FPS, Bitrate, Codec, CQP/VBR) while preserving 100% of cursor telemetry and auto-zoom post-editing features.

**Architecture:** Maintain a pristine desktop video capture via Windows Graphics Capture into Direct3D 11 textures, feed textures directly into hardware NVENC / Media Foundation via DXGI surface buffers without CPU readback, expose full granular settings in the UI, and keep the asynchronous cursor telemetry pipeline untouched.

**Tech Stack:** TypeScript, React, HeroUI, Tailwind CSS v4, Electron IPC, C++20, Direct3D 11, Windows Media Foundation (NVENC / MFT), CMake.

**Spec:** `docs/superpowers/specs/2026-10-07-recordly-rtx-hardware-recording-design.md`

## Global Constraints
- Target platform: Windows 10/11 x64 with NVIDIA RTX GPU support.
- Zero CPU copies for captured video frames on RTX hardware.
- Preserve cursor exclusion in video (`session_.IsCursorCaptureEnabled(false)`) and telemetry sidecar (`*.telemetry.json`).
- UI must follow Recordly's design system (`SettingsRow`, HeroUI, Tailwind CSS, dark theme).
- Native C++ builds must pass through `scripts/build-windows-capture.mjs`.

---

### Task 1: RTX Settings Types and Defaults

**Files:**
- Create: `src/types/rtxSettings.ts`
- Create: `src/lib/rtxSettings.ts`
- Test: `src/lib/rtxSettings.test.ts`

**Interfaces:**
- Produces: `RtxRecordingSettings`, `DEFAULT_RTX_SETTINGS`, `loadRtxSettings()`, `saveRtxSettings()`, `applyRtxPreset()`

- [ ] **Step 1: Write tests for RTX settings helper and presets**

```typescript
// src/lib/rtxSettings.test.ts
import { describe, it, expect, vi, beforeEach } from 'vitest';
import {
  DEFAULT_RTX_SETTINGS,
  loadRtxSettings,
  saveRtxSettings,
  applyRtxPreset,
} from './rtxSettings';
import * as appSettings from './appSettings';

vi.mock('./appSettings', () => ({
  loadAppSetting: vi.fn(),
  saveAppSetting: vi.fn(),
}));

describe('rtxSettings', () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it('returns default settings when nothing is stored', () => {
    vi.mocked(appSettings.loadAppSetting).mockReturnValue(null);
    const settings = loadRtxSettings();
    expect(settings).toEqual(DEFAULT_RTX_SETTINGS);
    expect(settings.preset).toBe('balanced');
    expect(settings.fps).toBe(60);
    expect(settings.codec).toBe('hevc');
  });

  it('correctly calculates preset configurations', () => {
    const perf = applyRtxPreset('ultra-performance');
    expect(perf.fps).toBe(60);
    expect(perf.codec).toBe('h264');
    expect(perf.rateControl).toBe('vbr');

    const maxQ = applyRtxPreset('max-quality');
    expect(maxQ.fps).toBe(60);
    expect(maxQ.codec).toBe('hevc');
    expect(maxQ.rateControl).toBe('cqp');
  });

  it('saves settings through appSettings', () => {
    saveRtxSettings({ ...DEFAULT_RTX_SETTINGS, fps: 120 });
    expect(appSettings.saveAppSetting).toHaveBeenCalledWith(
      'rtxRecordingSettings',
      expect.objectContaining({ fps: 120 })
    );
  });
});
```

- [ ] **Step 2: Run test to verify it fails**

Run: `npm test src/lib/rtxSettings.test.ts`
Expected: FAIL (module not found)

- [ ] **Step 3: Implement `src/types/rtxSettings.ts` and `src/lib/rtxSettings.ts`**

```typescript
// src/types/rtxSettings.ts
export type RtxPreset = 'ultra-performance' | 'balanced' | 'max-quality' | 'custom';
export type RtxCodec = 'h264' | 'hevc';
export type RtxFramerate = 30 | 60 | 120;
export type RtxRateControl = 'cqp' | 'vbr' | 'cbr';

export interface RtxRecordingSettings {
  preset: RtxPreset;
  codec: RtxCodec;
  fps: RtxFramerate;
  rateControl: RtxRateControl;
  bitrateMbps: number;
  cqpLevel: number;
  zeroCopy: boolean;
}
```

```typescript
// src/lib/rtxSettings.ts
import { loadAppSetting, saveAppSetting } from './appSettings';
import type { RtxPreset, RtxRecordingSettings } from '../types/rtxSettings';

export const RTX_SETTINGS_KEY = 'rtxRecordingSettings';

export const DEFAULT_RTX_SETTINGS: RtxRecordingSettings = Object.freeze({
  preset: 'balanced',
  codec: 'hevc',
  fps: 60,
  rateControl: 'vbr',
  bitrateMbps: 35,
  cqpLevel: 20,
  zeroCopy: true,
});

export function applyRtxPreset(preset: RtxPreset): RtxRecordingSettings {
  switch (preset) {
    case 'ultra-performance':
      return {
        preset: 'ultra-performance',
        codec: 'h264',
        fps: 60,
        rateControl: 'vbr',
        bitrateMbps: 20,
        cqpLevel: 26,
        zeroCopy: true,
      };
    case 'max-quality':
      return {
        preset: 'max-quality',
        codec: 'hevc',
        fps: 60,
        rateControl: 'cqp',
        bitrateMbps: 60,
        cqpLevel: 18,
        zeroCopy: true,
      };
    case 'balanced':
    default:
      return {
        ...DEFAULT_RTX_SETTINGS,
        preset: 'balanced',
      };
  }
}

export function loadRtxSettings(): RtxRecordingSettings {
  const stored = loadAppSetting<Partial<RtxRecordingSettings>>(RTX_SETTINGS_KEY);
  if (!stored || typeof stored !== 'object') {
    return { ...DEFAULT_RTX_SETTINGS };
  }
  return {
    ...DEFAULT_RTX_SETTINGS,
    ...stored,
  };
}

export function saveRtxSettings(settings: RtxRecordingSettings): boolean {
  return saveAppSetting(RTX_SETTINGS_KEY, settings);
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `npm test src/lib/rtxSettings.test.ts`
Expected: PASS

- [ ] **Step 5: Commit changes**

```bash
git add src/types/rtxSettings.ts src/lib/rtxSettings.ts src/lib/rtxSettings.test.ts
git commit -m "feat(settings): add RTX recording configuration schema and store helpers"
```

---

### Task 2: UI in Dashboard Settings

**Files:**
- Create: `src/components/video-editor/dashboard/RtxRecordingSettingsSection.tsx`
- Modify: `src/components/video-editor/dashboard/DashboardSettings.tsx`
- Modify: `src/i18n/locales/en/settings.json`
- Modify: `src/i18n/locales/ru/settings.json`

**Interfaces:**
- Consumes: `loadRtxSettings()`, `saveRtxSettings()`, `applyRtxPreset()`, `SettingsRow`
- Produces: Visual settings section rendered inside `<SettingsCategory category="recording">`

- [ ] **Step 1: Add i18n keys for RTX recording options in English and Russian**

English:
```json
"rtxRecording": {
  "title": "NVIDIA RTX Hardware Recording",
  "preset": "Performance Preset",
  "presetUltraPerformance": "Ultra Performance (Low Overhead)",
  "presetBalanced": "Balanced (Recommended)",
  "presetMaxQuality": "Max Quality (Studio)",
  "presetCustom": "Custom Configuration",
  "codec": "Video Codec",
  "fps": "Framerate",
  "rateControl": "Rate Control",
  "bitrate": "Bitrate (Mbps)",
  "cqp": "Quality Level (CQP)",
  "zeroCopy": "GPU Zero-Copy Pipeline",
  "zeroCopyDescription": "Streams frames directly in VRAM without CPU memory copies."
}
```

Russian:
```json
"rtxRecording": {
  "title": "Аппаратная запись NVIDIA RTX",
  "preset": "Пресет качества",
  "presetUltraPerformance": "Максимальная производительность (Низкая нагрузка)",
  "presetBalanced": "Сбалансированный (Рекомендуется)",
  "presetMaxQuality": "Студийное качество (Max Quality)",
  "presetCustom": "Пользовательские настройки",
  "codec": "Видеокодек",
  "fps": "Частота кадров (FPS)",
  "rateControl": "Режим битрейта",
  "bitrate": "Битрейт (Мбит/с)",
  "cqp": "Уровень качества (CQP)",
  "zeroCopy": "Zero-Copy захват через VRAM",
  "zeroCopyDescription": "Кодирует кадры напрямую в видеопамяти без копирования в CPU."
}
```

- [ ] **Step 2: Implement `RtxRecordingSettingsSection.tsx`**

Integrate select dropdowns and switches for presets, codec (H.264/HEVC), framerate (30/60/120 fps), rate control (CQP/VBR/CBR), bitrate slider, and zero-copy toggle using `SettingsRow`.

- [ ] **Step 3: Embed section into `DashboardSettings.tsx`**

Include `<RtxRecordingSettingsSection />` under `<SettingsCategory category="recording">` when platform is Windows.

- [ ] **Step 4: Verify UI compilation & typecheck**

Run: `npm run typecheck`
Expected: 0 errors

- [ ] **Step 5: Commit changes**

```bash
git add src/components/video-editor/dashboard/RtxRecordingSettingsSection.tsx src/components/video-editor/dashboard/DashboardSettings.tsx src/i18n/locales/en/settings.json src/i18n/locales/ru/settings.json
git commit -m "feat(ui): add RTX hardware recording controls and presets in dashboard settings"
```

---

### Task 3: IPC Bridge & Config Forwarding to Native Capture

**Files:**
- Modify: `electron/ipc/register/recording.ts`
- Modify: `electron/ipc/state.ts` (if needed for active RTX config)
- Test: `electron/ipc/recording.test.ts` (or existing recording test)

**Interfaces:**
- Consumes: `readAppSetting('rtxRecordingSettings')`
- Produces: Extended `config` passed to `wgc-capture.exe` spawn

- [ ] **Step 1: Update `electron/ipc/register/recording.ts` to read RTX settings**

In `startNativeWindowsCapture` (around line 468):
Read `readAppSetting('rtxRecordingSettings')` from `appSettingsStore`.
Merge into `config`:
```typescript
const rtxSettings = (readAppSetting('rtxRecordingSettings') as Record<string, unknown>) || {};
const fps = typeof rtxSettings.fps === 'number' ? rtxSettings.fps : 60;
const codec = typeof rtxSettings.codec === 'string' ? rtxSettings.codec : 'hevc';
const rateControl = typeof rtxSettings.rateControl === 'string' ? rtxSettings.rateControl : 'vbr';
const bitrate = typeof rtxSettings.bitrateMbps === 'number' ? rtxSettings.bitrateMbps * 1_000_000 : 35_000_000;
const qp = typeof rtxSettings.cqpLevel === 'number' ? rtxSettings.cqpLevel : 20;
const zeroCopy = rtxSettings.zeroCopy !== false;

const config: Record<string, unknown> = {
  outputPath: tempVideoPath,
  fps,
  codec,
  rateControl,
  bitrate,
  qp,
  zeroCopy,
};
```

- [ ] **Step 2: Run existing tests to ensure no regressions**

Run: `npm test electron/ipc`
Expected: PASS

- [ ] **Step 3: Commit changes**

```bash
git add electron/ipc/register/recording.ts
git commit -m "feat(ipc): forward RTX recording settings to native Windows capture helper"
```

---

### Task 4: C++ `wgc-capture` Zero-Copy D3D11 & NVENC Integration

**Files:**
- Modify: `electron/native/wgc-capture/src/main.cpp`
- Modify: `electron/native/wgc-capture/src/mf_encoder.h`
- Modify: `electron/native/wgc-capture/src/mf_encoder.cpp`
- Modify: `electron/native/wgc-capture/CMakeLists.txt`

**Interfaces:**
- Consumes: D3D11 captured texture from `WgcSession`
- Produces: Pure hardware encoded MP4 using `IMFDXGIDeviceManager` and `MFCreateDXGISurfaceBuffer`

- [ ] **Step 1: Parse extended fields in `electron/native/wgc-capture/src/main.cpp`**

In `CaptureConfig`, add:
- `std::string codec = "hevc"`
- `std::string rateControl = "vbr"`
- `int bitrate = 0`
- `int qp = 20`
- `bool zeroCopy = true`
Update `parseSimpleJson` to read these fields and pass them into `encoder.initialize(...)`.

- [ ] **Step 2: Update `MFEncoder` class in `mf_encoder.h`**

Add:
- `ComPtr<IMFDXGIDeviceManager> dxgiDeviceManager_`
- `bool zeroCopyEnabled_ = false`
- `std::string codec_ = "hevc"`
- `std::string rateControl_ = "vbr"`
- `int qp_ = 20`
- `int bitrate_ = 0`
- `ID3D11VideoDevice* videoDevice_ = nullptr`
- `ID3D11VideoProcessor* videoProcessor_ = nullptr`
- `ID3D11VideoProcessorEnumerator* videoProcessorEnum_ = nullptr`
- `ComPtr<ID3D11Texture2D> nv12GpuTexture_`

- [ ] **Step 3: Implement Zero-Copy D3D11 Device Manager in `mf_encoder.cpp`**

In `initialize(...)`:
1. Create `MFCreateDXGIDeviceManager(&resetToken, &dxgiDeviceManager_)`.
2. Reset device: `dxgiDeviceManager_->ResetDevice(device_, resetToken)`.
3. In SinkWriter attributes:
   `writerAttrs->SetUnknown(MF_SINK_WRITER_D3D_MANAGER, dxgiDeviceManager_.Get());`
   `writerAttrs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);`
4. Setup codec:
   If `codec == "hevc"`, `MFVideoFormat_HEVC`; else `MFVideoFormat_H264`.
5. Configure rate control via `ICodecAPI` on SinkWriter transform:
   - For CQP: `CODECAPI_AVEncCommonRateControlMode` = `eAVEncCommonRateControlMode_Quality` with QP / Quality index.
   - For VBR/CBR: set `MF_MT_AVG_BITRATE`.

- [ ] **Step 4: Implement GPU-side texture submission in `writeFrame(...)`**

If `zeroCopyEnabled_`:
1. Use D3D11 Video Processor or direct D3D11 surface to populate NV12 texture on GPU without CPU mapping.
2. Call `MFCreateDXGISurfaceBuffer(__uuidof(ID3D11Texture2D), pGpuTexture, 0, FALSE, &buffer)`.
3. Wrap buffer into `IMFSample`, set timestamp, and write to `sinkWriter_`.
4. If zero-copy fails, fall back automatically to the staging texture mapping path.

- [ ] **Step 5: Verify CMake configuration and build**

Run: `node scripts/build-windows-capture.mjs`
Expected: CMake build finishes with exit code 0 and updates `electron/native/bin/win32-x64/wgc-capture.exe`.

- [ ] **Step 6: Commit changes**

```bash
git add electron/native/wgc-capture/ scripts/build-windows-capture.mjs electron/native/bin/win32-x64/
git commit -m "feat(native): implement Zero-Copy D3D11 and NVENC pipeline in wgc-capture"
```

---

### Task 5: End-to-End Verification & Telemetry Integrity

**Files:**
- Verification only

- [ ] **Step 1: Run comprehensive tests**

Run: `npm test`
Expected: All tests pass.

- [ ] **Step 2: Test `wgc-capture.exe` execution**

Run test capture command using powershell with JSON config:
Verify that `wgc-capture.exe` initializes with Zero-Copy D3D11 manager and outputs a valid video file.

- [ ] **Step 3: Verify Telemetry generation**

Verify that cursor telemetry events and zoom suggestions remain completely functional with the clean video output.

- [ ] **Step 4: Commit and finalize**

```bash
git commit --allow-empty -m "chore: verified RTX zero-copy recording and telemetry pipeline"
```
