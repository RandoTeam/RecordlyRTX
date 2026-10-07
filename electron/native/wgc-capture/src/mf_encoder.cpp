#include "mf_encoder.h"
#include <mfapi.h>
#include <mferror.h>
#include <codecapi.h>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <cstring>
#include "../../common/bt709_video.h"

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfuuid.lib")

static UINT32 calculateScreenRecordingBitrate(int width, int height, int fps) {
    constexpr uint64_t kFourKPixels = 3840ULL * 2160ULL;
    constexpr uint64_t kQhdPixels = 2560ULL * 1440ULL;
    constexpr UINT32 kBitrate4K = 45000000;
    constexpr UINT32 kBitrateQhd = 28000000;
    constexpr UINT32 kBitrateBase = 18000000;
    constexpr double kHighFrameRateBoost = 1.35;

    const uint64_t pixels =
        static_cast<uint64_t>((std::max)(width, 1)) *
        static_cast<uint64_t>((std::max)(height, 1));
    const UINT32 baseBitrate =
        pixels >= kFourKPixels ? kBitrate4K :
        pixels >= kQhdPixels ? kBitrateQhd :
        kBitrateBase;
    const double boost = fps >= 60 ? kHighFrameRateBoost : 1.0;
    return static_cast<UINT32>(static_cast<double>(baseBitrate) * boost + 0.5);
}

MFEncoder::MFEncoder() {}

MFEncoder::~MFEncoder() {
    finalize();
}

bool MFEncoder::initZeroCopyPipeline() {
    if (!device_ || !context_) return false;

    HRESULT hr = device_->QueryInterface(IID_PPV_ARGS(&videoDevice_));
    if (FAILED(hr) || !videoDevice_) {
        std::cerr << "VideoDevice query failed: 0x" << std::hex << hr << std::endl;
        return false;
    }

    hr = context_->QueryInterface(IID_PPV_ARGS(&videoContext_));
    if (FAILED(hr) || !videoContext_) {
        std::cerr << "VideoContext query failed: 0x" << std::hex << hr << std::endl;
        return false;
    }

    // Allocate GPU NV12 render target
    D3D11_TEXTURE2D_DESC nv12Desc = {};
    nv12Desc.Width = width_;
    nv12Desc.Height = height_;
    nv12Desc.MipLevels = 1;
    nv12Desc.ArraySize = 1;
    nv12Desc.Format = DXGI_FORMAT_NV12;
    nv12Desc.SampleDesc.Count = 1;
    nv12Desc.Usage = D3D11_USAGE_DEFAULT;
    nv12Desc.BindFlags = D3D11_BIND_RENDER_TARGET;

    hr = device_->CreateTexture2D(&nv12Desc, nullptr, &zeroCopyNv12Texture_);
    if (FAILED(hr)) {
        std::cerr << "Failed to allocate GPU NV12 texture: 0x" << std::hex << hr << std::endl;
        return false;
    }

    // Setup Video Processor Enumerator
    D3D11_VIDEO_PROCESSOR_CONTENT_DESC vpDesc = {};
    vpDesc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    vpDesc.InputWidth = width_;
    vpDesc.InputHeight = height_;
    vpDesc.OutputWidth = width_;
    vpDesc.OutputHeight = height_;
    vpDesc.InputFrameRate.Numerator = fps_;
    vpDesc.InputFrameRate.Denominator = 1;
    vpDesc.OutputFrameRate.Numerator = fps_;
    vpDesc.OutputFrameRate.Denominator = 1;
    vpDesc.Usage = D3D11_VIDEO_USAGE_OPTIMAL_QUALITY;

    hr = videoDevice_->CreateVideoProcessorEnumerator(&vpDesc, &videoProcessorEnum_);
    if (FAILED(hr) || !videoProcessorEnum_) {
        std::cerr << "CreateVideoProcessorEnumerator failed: 0x" << std::hex << hr << std::endl;
        return false;
    }

    UINT flags = 0;
    hr = videoProcessorEnum_->CheckVideoProcessorFormat(DXGI_FORMAT_B8G8R8A8_UNORM, &flags);
    if (FAILED(hr) || !(flags & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT)) {
        std::cerr << "BGRA input not supported by video processor" << std::endl;
        return false;
    }

    hr = videoDevice_->CreateVideoProcessor(videoProcessorEnum_.Get(), 0, &videoProcessor_);
    if (FAILED(hr) || !videoProcessor_) {
        std::cerr << "CreateVideoProcessor failed: 0x" << std::hex << hr << std::endl;
        return false;
    }

    // Output view for NV12
    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC outViewDesc = {};
    outViewDesc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
    outViewDesc.Texture2D.MipSlice = 0;

    hr = videoDevice_->CreateVideoProcessorOutputView(
        zeroCopyNv12Texture_.Get(),
        videoProcessorEnum_.Get(),
        &outViewDesc,
        &vpOutputView_);
    if (FAILED(hr)) {
        std::cerr << "CreateVideoProcessorOutputView failed: 0x" << std::hex << hr << std::endl;
        return false;
    }

    return true;
}

bool MFEncoder::initialize(const std::wstring& outputPath, int width, int height, int fps,
                           ID3D11Device* device, ID3D11DeviceContext* context,
                           const std::string& codec,
                           const std::string& rateControl,
                           int bitrate,
                           int qp,
                           bool zeroCopy) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (initialized_) return false;

    if (fps <= 0) {
        std::cerr << "ERROR: Encoder fps must be positive, got " << fps << std::endl;
        return false;
    }

    if (width % 2 != 0 || height % 2 != 0) {
        std::cerr << "ERROR: Encoder dimensions must be even, got " << width << "x" << height << std::endl;
        return false;
    }

    width_ = width;
    height_ = height;
    fps_ = fps;
    codec_ = codec.empty() ? "hevc" : codec;
    rateControl_ = rateControl.empty() ? "vbr" : rateControl;
    bitrate_ = bitrate;
    qp_ = qp > 0 ? qp : 20;
    zeroCopyRequested_ = zeroCopy;
    device_ = device;
    context_ = context;

    HRESULT hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) {
        std::cerr << "ERROR: MFStartup failed: 0x" << std::hex << hr << std::endl;
        return false;
    }

    // Output media type (H.264 or HEVC)
    ComPtr<IMFMediaType> outputType;
    hr = MFCreateMediaType(&outputType);
    if (FAILED(hr)) return false;

    outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    if (codec_ == "h264") {
        outputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    } else {
        outputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_HEVC);
    }

    const UINT32 videoBitrate = bitrate_ > 0 ? static_cast<UINT32>(bitrate_) : calculateScreenRecordingBitrate(width_, height_, fps_);
    outputType->SetUINT32(MF_MT_AVG_BITRATE, videoBitrate);
    MFSetAttributeSize(outputType.Get(), MF_MT_FRAME_SIZE, width_, height_);
    MFSetAttributeRatio(outputType.Get(), MF_MT_FRAME_RATE, fps_, 1);
    MFSetAttributeRatio(outputType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    outputType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    hr = setBt709LimitedVideoAttributes(outputType.Get());
    if (FAILED(hr)) return false;

    std::cerr << "Encoder configured: " << (codec_ == "h264" ? "H.264" : "HEVC")
              << " (" << rateControl_ << ") " << videoBitrate << " bps @ "
              << width_ << "x" << height_ << " " << fps_ << "fps (CQP QP=" << qp_ << ")" << std::endl;

    // Input media type (NV12)
    ComPtr<IMFMediaType> inputType;
    hr = MFCreateMediaType(&inputType);
    if (FAILED(hr)) return false;

    inputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    inputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
    MFSetAttributeSize(inputType.Get(), MF_MT_FRAME_SIZE, width_, height_);
    MFSetAttributeRatio(inputType.Get(), MF_MT_FRAME_RATE, fps_, 1);
    MFSetAttributeRatio(inputType.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    inputType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    hr = setBt709LimitedVideoAttributes(inputType.Get());
    if (FAILED(hr)) return false;

    // Create SinkWriter with hardware transform attributes
    ComPtr<IMFAttributes> writerAttrs;
    hr = MFCreateAttributes(&writerAttrs, 3);
    if (FAILED(hr)) return false;

    writerAttrs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
    writerAttrs->SetUINT32(MF_LOW_LATENCY, TRUE);

    if (zeroCopyRequested_) {
        UINT resetToken = 0;
        hr = MFCreateDXGIDeviceManager(&resetToken, &dxgiDeviceManager_);
        if (SUCCEEDED(hr) && dxgiDeviceManager_) {
            hr = dxgiDeviceManager_->ResetDevice(device_, resetToken);
            if (SUCCEEDED(hr)) {
                writerAttrs->SetUnknown(MF_SINK_WRITER_D3D_MANAGER, dxgiDeviceManager_.Get());
            }
        }
    }

    hr = MFCreateSinkWriterFromURL(outputPath.c_str(), nullptr, writerAttrs.Get(), &sinkWriter_);
    if (FAILED(hr)) {
        std::cerr << "ERROR: MFCreateSinkWriterFromURL failed: 0x" << std::hex << hr << std::endl;
        return false;
    }

    hr = sinkWriter_->AddStream(outputType.Get(), &streamIndex_);
    if (FAILED(hr)) {
        std::cerr << "ERROR: AddStream failed: 0x" << std::hex << hr << std::endl;
        return false;
    }

    hr = sinkWriter_->SetInputMediaType(streamIndex_, inputType.Get(), nullptr);
    if (FAILED(hr)) {
        std::cerr << "ERROR: SetInputMediaType failed: 0x" << std::hex << hr << std::endl;
        return false;
    }

    // Apply ICodecAPI properties if accessible
    ComPtr<ICodecAPI> codecApi;
    HRESULT hrCodec = sinkWriter_->GetServiceForStream(
        streamIndex_,
        GUID_NULL,
        __uuidof(ICodecAPI),
        reinterpret_cast<void**>(codecApi.GetAddressOf()));
    if (SUCCEEDED(hrCodec) && codecApi) {
        if (rateControl_ == "cqp") {
            VARIANT var;
            VariantInit(&var);
            var.vt = VT_UI4;
            var.ulVal = eAVEncCommonRateControlMode_Quality;
            codecApi->SetValue(&CODECAPI_AVEncCommonRateControlMode, &var);

            var.ulVal = static_cast<ULONG>((std::clamp)(qp_, 1, 51));
            codecApi->SetValue(&CODECAPI_AVEncVideoEncodeQP, &var);
        } else if (rateControl_ == "cbr") {
            VARIANT var;
            VariantInit(&var);
            var.vt = VT_UI4;
            var.ulVal = eAVEncCommonRateControlMode_CBR;
            codecApi->SetValue(&CODECAPI_AVEncCommonRateControlMode, &var);
        }
    }

    hr = sinkWriter_->BeginWriting();
    if (FAILED(hr)) {
        std::cerr << "ERROR: BeginWriting failed: 0x" << std::hex << hr << std::endl;
        return false;
    }

    // Initialize Zero-Copy GPU pipeline
    if (zeroCopyRequested_ && dxgiDeviceManager_) {
        zeroCopyActive_ = initZeroCopyPipeline();
        if (zeroCopyActive_) {
            std::cerr << "RTX Zero-Copy VRAM pipeline initialized successfully." << std::endl;
        } else {
            std::cerr << "Notice: Zero-Copy pipeline unavailable, using optimized staging fallback." << std::endl;
        }
    }

    // Allocate fallback staging texture
    D3D11_TEXTURE2D_DESC stagingDesc = {};
    stagingDesc.Width = width_;
    stagingDesc.Height = height_;
    stagingDesc.MipLevels = 1;
    stagingDesc.ArraySize = 1;
    stagingDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    stagingDesc.SampleDesc.Count = 1;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    hr = device_->CreateTexture2D(&stagingDesc, nullptr, &stagingTexture_);
    if (FAILED(hr)) {
        std::cerr << "ERROR: Failed to create staging texture: 0x" << std::hex << hr << std::endl;
        return false;
    }

    D3D11_TEXTURE2D_DESC compositeDesc = {};
    compositeDesc.Width = width_;
    compositeDesc.Height = height_;
    compositeDesc.MipLevels = 1;
    compositeDesc.ArraySize = 1;
    compositeDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    compositeDesc.SampleDesc.Count = 1;
    compositeDesc.Usage = D3D11_USAGE_DEFAULT;
    compositeDesc.BindFlags = D3D11_BIND_RENDER_TARGET;

    hr = device_->CreateTexture2D(&compositeDesc, nullptr, &resizeCompositeTexture_);
    if (FAILED(hr)) {
        std::cerr << "ERROR: Failed to create resize composite texture: 0x" << std::hex << hr << std::endl;
        return false;
    }

    hr = device_->CreateRenderTargetView(resizeCompositeTexture_.Get(), nullptr, &resizeCompositeView_);
    if (FAILED(hr)) {
        std::cerr << "ERROR: Failed to create resize composite view: 0x" << std::hex << hr << std::endl;
        return false;
    }

    const int ySize = width_ * height_;
    const int uvSize = (width_ / 2) * (height_ / 2) * 2;
    nv12Buffer_.resize(ySize + uvSize);
    lastFrameBuffer_.clear();
    firstSampleTimeHns_ = -1;
    lastSampleTimeHns_ = -1;

    initialized_ = true;
    return true;
}

bool MFEncoder::processVideoFrameToGpuNv12(ID3D11Texture2D* sourceTexture) {
    if (!videoDevice_ || !videoContext_ || !videoProcessor_ || !vpOutputView_) return false;

    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inViewDesc = {};
    inViewDesc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
    inViewDesc.Texture2D.MipSlice = 0;

    ComPtr<ID3D11VideoProcessorInputView> inputView;
    HRESULT hr = videoDevice_->CreateVideoProcessorInputView(
        sourceTexture,
        videoProcessorEnum_.Get(),
        &inViewDesc,
        &inputView);
    if (FAILED(hr) || !inputView) return false;

    D3D11_VIDEO_PROCESSOR_STREAM streamData = {};
    streamData.Enable = TRUE;
    streamData.pInputSurface = inputView.Get();

    hr = videoContext_->VideoProcessorBlt(
        videoProcessor_.Get(),
        vpOutputView_.Get(),
        0,
        1,
        &streamData);

    return SUCCEEDED(hr);
}

bool MFEncoder::writeZeroCopySampleLocked(int64_t timestampHns) {
    if (!sinkWriter_ || !zeroCopyNv12Texture_) return false;

    ComPtr<IMFMediaBuffer> buffer;
    HRESULT hr = MFCreateDXGISurfaceBuffer(
        __uuidof(ID3D11Texture2D),
        zeroCopyNv12Texture_.Get(),
        0,
        FALSE,
        &buffer);
    if (FAILED(hr) || !buffer) return false;

    const DWORD totalBytes = static_cast<DWORD>(width_ * height_ * 3 / 2);
    buffer->SetCurrentLength(totalBytes);

    ComPtr<IMFSample> sample;
    hr = MFCreateSample(&sample);
    if (FAILED(hr) || !sample) return false;

    sample->AddBuffer(buffer.Get());
    sample->SetSampleTime(timestampHns);
    const int64_t frameDurationHns = 10000000LL / fps_;
    sample->SetSampleDuration(frameDurationHns);

    hr = sinkWriter_->WriteSample(streamIndex_, sample.Get());
    return SUCCEEDED(hr);
}

bool MFEncoder::writeFrame(ID3D11Texture2D* texture, int64_t timestampHns) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_ || !sinkWriter_) return false;

    int64_t normalizedTimestampHns = 0;
    normalizeWriteTimestampHnsLocked(timestampHns, normalizedTimestampHns);

    // Fast-path: Zero-Copy GPU execution
    if (zeroCopyActive_) {
        if (processVideoFrameToGpuNv12(texture)) {
            bool wrote = writeZeroCopySampleLocked(normalizedTimestampHns);
            if (wrote) {
                lastSampleTimeHns_ = normalizedTimestampHns;
                return true;
            }
        }
        // If GPU blt or surface buffer write fails, fall through to staging path
    }

    // Staging fallback path
    D3D11_TEXTURE2D_DESC sourceDesc = {};
    texture->GetDesc(&sourceDesc);

    if (sourceDesc.Width == static_cast<UINT>(width_) &&
        sourceDesc.Height == static_cast<UINT>(height_)) {
        context_->CopyResource(stagingTexture_.Get(), texture);
    } else {
        if (!resizeCompositeTexture_ || !resizeCompositeView_) return false;

        const FLOAT clearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        context_->ClearRenderTargetView(resizeCompositeView_.Get(), clearColor);

        D3D11_BOX sourceBox = {};
        sourceBox.left = 0;
        sourceBox.top = 0;
        sourceBox.front = 0;
        sourceBox.right = (std::min)(sourceDesc.Width, static_cast<UINT>(width_));
        sourceBox.bottom = (std::min)(sourceDesc.Height, static_cast<UINT>(height_));
        sourceBox.back = 1;

        if (sourceBox.right == 0 || sourceBox.bottom == 0) return false;

        context_->CopySubresourceRegion(
            resizeCompositeTexture_.Get(),
            0,
            0,
            0,
            0,
            texture,
            0,
            &sourceBox);
        context_->CopyResource(stagingTexture_.Get(), resizeCompositeTexture_.Get());
    }

    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = context_->Map(stagingTexture_.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) return false;

    const uint8_t* bgra = static_cast<const uint8_t*>(mapped.pData);
    const int bgraPitch = static_cast<int>(mapped.RowPitch);
    convertBgraToBt709LimitedNv12(bgra, bgraPitch, width_, height_, nv12Buffer_);

    context_->Unmap(stagingTexture_.Get(), 0);

    if (!lastFrameBuffer_.empty() && !extendLastFrameToLocked(normalizedTimestampHns)) {
        return false;
    }

    bool wroteSample = writeNv12SampleLocked(nv12Buffer_, normalizedTimestampHns);
    if (wroteSample) {
        lastFrameBuffer_ = nv12Buffer_;
        lastSampleTimeHns_ = normalizedTimestampHns;
    }
    return wroteSample;
}

void MFEncoder::normalizeWriteTimestampHnsLocked(int64_t timestampHns, int64_t& normalizedTimestampHns) {
    if (firstSampleTimeHns_ < 0) {
        firstSampleTimeHns_ = timestampHns;
        normalizedTimestampHns = 0;
        return;
    }

    normalizedTimestampHns = timestampHns - firstSampleTimeHns_;
    if (normalizedTimestampHns <= lastSampleTimeHns_) {
        normalizedTimestampHns = lastSampleTimeHns_ + 1;
    }
}

bool MFEncoder::normalizeTimelineTimestampHnsLocked(int64_t timestampHns, int64_t& normalizedTimestampHns) const {
    if (firstSampleTimeHns_ < 0) {
        return false;
    }

    normalizedTimestampHns = timestampHns - firstSampleTimeHns_;
    if (normalizedTimestampHns < 0) {
        normalizedTimestampHns = 0;
    }
    return true;
}

bool MFEncoder::extendLastFrameTo(int64_t timestampHns) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_ || !sinkWriter_) {
        return false;
    }

    int64_t normalizedTimestampHns = 0;
    if (!normalizeTimelineTimestampHnsLocked(timestampHns, normalizedTimestampHns)) {
        return false;
    }

    return extendLastFrameToLocked(normalizedTimestampHns);
}

bool MFEncoder::extendLastFrameToLocked(int64_t timestampHns) {
    if (lastSampleTimeHns_ < 0) {
        return true;
    }

    if (fps_ <= 0) return false;
    const int64_t frameDurationHns = 10000000LL / fps_;
    const int64_t nextTargetTimeHns = lastSampleTimeHns_ + frameDurationHns;
    if (timestampHns < nextTargetTimeHns) {
        return true;
    }

    int64_t syntheticTimeHns = nextTargetTimeHns;
    while (syntheticTimeHns <= timestampHns) {
        bool wrote = false;
        if (zeroCopyActive_) {
            wrote = writeZeroCopySampleLocked(syntheticTimeHns);
        } else if (!lastFrameBuffer_.empty()) {
            wrote = writeNv12SampleLocked(lastFrameBuffer_, syntheticTimeHns);
        }
        if (!wrote) return false;
        lastSampleTimeHns_ = syntheticTimeHns;
        syntheticTimeHns += frameDurationHns;
    }

    return true;
}

bool MFEncoder::writeNv12SampleLocked(const std::vector<uint8_t>& frameBuffer, int64_t timestampHns) {
    if (frameBuffer.empty() || !sinkWriter_) return false;

    DWORD bufferSize = static_cast<DWORD>(frameBuffer.size());
    ComPtr<IMFMediaBuffer> buffer;
    HRESULT hr = MFCreateMemoryBuffer(bufferSize, &buffer);
    if (FAILED(hr)) return false;

    BYTE* bufferData = nullptr;
    hr = buffer->Lock(&bufferData, nullptr, nullptr);
    if (FAILED(hr)) return false;

    std::memcpy(bufferData, frameBuffer.data(), bufferSize);
    buffer->Unlock();
    buffer->SetCurrentLength(bufferSize);

    ComPtr<IMFSample> sample;
    hr = MFCreateSample(&sample);
    if (FAILED(hr)) return false;

    sample->AddBuffer(buffer.Get());
    sample->SetSampleTime(timestampHns);
    if (fps_ <= 0) return false;
    const int64_t frameDurationHns = 10000000LL / fps_;
    sample->SetSampleDuration(frameDurationHns);

    hr = sinkWriter_->WriteSample(streamIndex_, sample.Get());
    return SUCCEEDED(hr);
}

bool MFEncoder::finalize() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_) return false;
    initialized_ = false;
    zeroCopyActive_ = false;

    vpOutputView_.Reset();
    zeroCopyNv12Texture_.Reset();
    videoProcessor_.Reset();
    videoProcessorEnum_.Reset();
    videoContext_.Reset();
    videoDevice_.Reset();

    stagingTexture_.Reset();
    resizeCompositeView_.Reset();
    resizeCompositeTexture_.Reset();
    nv12Buffer_.clear();
    nv12Buffer_.shrink_to_fit();
    lastFrameBuffer_.clear();
    lastFrameBuffer_.shrink_to_fit();

    if (!sinkWriter_) return false;
    HRESULT hr = sinkWriter_->Finalize();
    sinkWriter_.Reset();
    dxgiDeviceManager_.Reset();
    MFShutdown();
    return SUCCEEDED(hr);
}
