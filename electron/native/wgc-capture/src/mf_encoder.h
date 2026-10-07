#pragma once

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <icodecapi.h>
#include <codecapi.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <mutex>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

class MFEncoder {
public:
    MFEncoder();
    ~MFEncoder();

    bool initialize(const std::wstring& outputPath, int width, int height, int fps,
                    ID3D11Device* device, ID3D11DeviceContext* context,
                    const std::string& codec = "hevc",
                    const std::string& rateControl = "vbr",
                    int bitrate = 0,
                    int qp = 20,
                    bool zeroCopy = true);
    bool writeFrame(ID3D11Texture2D* texture, int64_t timestampHns);
    bool extendLastFrameTo(int64_t timestampHns);
    bool finalize();

    bool isZeroCopyActive() const { return zeroCopyActive_; }

private:
    void normalizeWriteTimestampHnsLocked(int64_t timestampHns, int64_t& normalizedTimestampHns);
    bool normalizeTimelineTimestampHnsLocked(int64_t timestampHns, int64_t& normalizedTimestampHns) const;
    bool extendLastFrameToLocked(int64_t timestampHns);
    bool writeNv12SampleLocked(const std::vector<uint8_t>& frameBuffer, int64_t timestampHns);
    bool writeZeroCopySampleLocked(int64_t timestampHns);

    bool initZeroCopyPipeline();
    bool processVideoFrameToGpuNv12(ID3D11Texture2D* sourceTexture);

    ComPtr<IMFSinkWriter> sinkWriter_;
    ComPtr<IMFDXGIDeviceManager> dxgiDeviceManager_;
    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;

    // Hardware Zero-Copy Video Processor & Surfaces
    ComPtr<ID3D11VideoDevice> videoDevice_;
    ComPtr<ID3D11VideoContext> videoContext_;
    ComPtr<ID3D11VideoProcessor> videoProcessor_;
    ComPtr<ID3D11VideoProcessorEnumerator> videoProcessorEnum_;
    ComPtr<ID3D11Texture2D> zeroCopyNv12Texture_;
    ComPtr<ID3D11VideoProcessorOutputView> vpOutputView_;

    // Staging fallback pipeline
    ComPtr<ID3D11Texture2D> stagingTexture_;
    ComPtr<ID3D11Texture2D> resizeCompositeTexture_;
    ComPtr<ID3D11RenderTargetView> resizeCompositeView_;
    std::vector<uint8_t> nv12Buffer_;
    std::vector<uint8_t> lastFrameBuffer_;

    DWORD streamIndex_ = 0;
    int width_ = 0;
    int height_ = 0;
    int fps_ = 60;
    int bitrate_ = 0;
    int qp_ = 20;
    std::string codec_ = "hevc";
    std::string rateControl_ = "vbr";
    bool zeroCopyRequested_ = true;
    bool zeroCopyActive_ = false;

    int64_t firstSampleTimeHns_ = -1;
    int64_t lastSampleTimeHns_ = -1;
    bool initialized_ = false;
    std::mutex mutex_;
};
