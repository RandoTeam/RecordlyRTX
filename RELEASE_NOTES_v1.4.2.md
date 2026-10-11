# RecordlyRTX v1.4.2 — Hotfix for Hardware Encoding Pipeline & Stop Finalization

This release resolves a critical deadlock that occurred during recording finalization on Windows.

---

### Bug Fixes & Improvements

- **Fixed "Timed out waiting for native Windows capture to stop" Error**:
  - Identified and resolved a root-cause deadlock in the Media Foundation hardware encoder pipeline where synchronous DXGI surface buffer submission blocked sample progression and parent IPC process teardown.
  - Enabled Direct3D 11 multithread protection (`SetMultithreadProtected(TRUE)`) on the device and immediate context to prevent driver-level lockups.
  - Set `MF_SINK_WRITER_DISABLE_THROTTLING` to allow uninhibited hardware frame feeding and instant stop finalization without artificial clock throttling.
  - Media Foundation hardware transforms (NVENC HEVC / H.264) now encode NV12 frames smoothly at full 60 FPS / 120 FPS with zero stalls, immediate frame-extension upon stopping, and clean file closure.

---

### Downloads & Verification

| Asset | Description | SHA-256 Checksum |
| :--- | :--- | :--- |
| **`RecordlyRTX-windows-x64.exe`** | Standalone Windows x64 Installer | `fc0a368e13c56c104484e877c78a9d8bba21d160e6277b5aa53efaa8251c8c27` |
| **`RecordlyRTX-windows-x64.exe.blockmap`** | Differential Update Blockmap | `7384ea242183f64bccfdaea86ed248bf979b2a55ce9017b4c55fc31ebec33425` |
| **`latest.yml`** | Auto-updater metadata manifest | `c72d805c068fc1cde2f94d7617bfa7efedf04630fea99bf4caf659b5e8765a72` |
| **`SHA256SUMS.txt`** | Checksums file | — |
