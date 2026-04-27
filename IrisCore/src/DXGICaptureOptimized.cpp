#define NOMINMAX
#include "../hdr/DXGICaptureOptimized.h"
#include <iostream>
#include <chrono>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")

// ===== GPUFrameBuffer Implementation =====
GPUFrameBuffer::GPUFrameBuffer(int capacity) : capacity(capacity) {}

GPUFrameBuffer::~GPUFrameBuffer() = default;

bool GPUFrameBuffer::Enqueue(const std::shared_ptr<GPUFrame>& frame) {
    std::lock_guard<std::mutex> lock(mutex);
    if (buffer.size() >= static_cast<size_t>(capacity)) {
        buffer.pop();
    }
    buffer.push(frame);
    return true;
}

std::shared_ptr<GPUFrame> GPUFrameBuffer::Dequeue() {
    std::lock_guard<std::mutex> lock(mutex);
    if (buffer.empty()) return nullptr;
    auto frame = buffer.front();
    buffer.pop();
    return frame;
}

int GPUFrameBuffer::GetSize() const {
    std::lock_guard<std::mutex> lock(mutex);
    return static_cast<int>(buffer.size());
}

bool GPUFrameBuffer::IsEmpty() const {
    std::lock_guard<std::mutex> lock(mutex);
    return buffer.empty();
}

// ===== DXGICaptureOptimized Implementation =====
DXGICaptureOptimized::DXGICaptureOptimized()
    : running(false), initialized(false),
      capture_width(0), capture_height(0), current_monitor(0),
      frame_buffer(nullptr) {}

DXGICaptureOptimized::~DXGICaptureOptimized() {
    Stop();
    Release();
}

bool DXGICaptureOptimized::Initialize(int monitor_index) {
    try {
        if (!frame_buffer) frame_buffer = std::make_unique<GPUFrameBuffer>(3);
    } catch (...) { frame_buffer = nullptr; }

    if (!InitializeDXGI(monitor_index)) { Release(); return false; }

    running = true;
    initialized = true;
    capture_thread = std::thread(&DXGICaptureOptimized::CaptureThreadMain, this);
    SetThreadPriority(capture_thread.native_handle(), THREAD_PRIORITY_TIME_CRITICAL);
    return true;
}

static const char* VendorName(UINT vid) {
    switch (vid) {
        case 0x10DE: return "NVIDIA";
        case 0x1002: return "AMD";
        case 0x8086: return "Intel";
        case 0x1414: return "Microsoft (WARP)";
        default:     return "Unknown";
    }
}

bool DXGICaptureOptimized::InitializeDXGI(int monitor_index) {
    HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)dxgi_factory.GetAddressOf());
    if (FAILED(hr)) { return false; }

    ComPtr<IDXGIAdapter1> picked_adapter;
    ComPtr<IDXGIOutput>   picked_output;

    for (UINT ai = 0; ; ++ai) {
        ComPtr<IDXGIAdapter1> a;
        if (dxgi_factory->EnumAdapters1(ai, a.GetAddressOf()) == DXGI_ERROR_NOT_FOUND) break;
        for (UINT oi = 0; ; ++oi) {
            ComPtr<IDXGIOutput> o;
            if (a->EnumOutputs(oi, o.GetAddressOf()) == DXGI_ERROR_NOT_FOUND) break;
            DXGI_OUTPUT_DESC od{};
            o->GetDesc(&od);
            bool is_primary = (od.DesktopCoordinates.left == 0 && od.DesktopCoordinates.top == 0);
            if (od.AttachedToDesktop && !picked_adapter) {
                if (monitor_index == 0 ? is_primary : (static_cast<int>(oi) == monitor_index)) {
                    picked_adapter = a;
                    picked_output  = o;
                }
            }
        }
    }

    if (!picked_adapter || !picked_output) { return false; }

    dxgi_adapter = picked_adapter;
    D3D_FEATURE_LEVEL feature_levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
    hr = D3D11CreateDevice(dxgi_adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0,
                           feature_levels, ARRAYSIZE(feature_levels), D3D11_SDK_VERSION,
                           d3d_device.GetAddressOf(), nullptr, d3d_context.GetAddressOf());
    if (FAILED(hr)) { return false; }

    ComPtr<IDXGIOutput1> output1;
    hr = picked_output.As(&output1);
    if (FAILED(hr)) { return false; }

    hr = output1->DuplicateOutput(d3d_device.Get(), output_duplication.GetAddressOf());
    if (FAILED(hr)) { return false; }

    // Use physical pixel dimensions from duplication descriptor.
    // DXGI_OUTPUT_DESC.DesktopCoordinates returns logical (DPI-scaled) coordinates,
    // which differ from physical pixels when Windows display scaling != 100%.
    // DXGI_OUTDUPL_DESC.ModeDesc always reflects the actual texture resolution.
    DXGI_OUTDUPL_DESC dupl_desc{};
    output_duplication->GetDesc(&dupl_desc);
    capture_width  = (int)dupl_desc.ModeDesc.Width;
    capture_height = (int)dupl_desc.ModeDesc.Height;

    roi_w = std::min(desired_roi_size, capture_width);
    roi_h = std::min(desired_roi_size, capture_height);
    roi_x = (capture_width  - roi_w) / 2;
    roi_y = (capture_height - roi_h) / 2;

    current_monitor = monitor_index;
    return true;
}

void DXGICaptureOptimized::CaptureThreadMain() {
    D3D11_TEXTURE2D_DESC sd{};
    sd.Width = roi_w; sd.Height = roi_h;
    sd.MipLevels = 1; sd.ArraySize = 1;
    sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.Usage = D3D11_USAGE_STAGING;
    sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    d3d_device->CreateTexture2D(&sd, nullptr, cached_staging.GetAddressOf());

    auto last_frame_time = std::chrono::steady_clock::now();
    static constexpr int STALE_THRESHOLD_SEC = 2;

    // Reinit leve: só recria IDXGIOutputDuplication (adapter/device ainda válidos)
    auto TryLightReinit = [&]() {
        output_duplication.Reset();
        ComPtr<IDXGIOutput> out;
        if (SUCCEEDED(dxgi_adapter->EnumOutputs(current_monitor, out.GetAddressOf()))) {
            ComPtr<IDXGIOutput1> out1;
            if (SUCCEEDED(out.As(&out1)))
                out1->DuplicateOutput(d3d_device.Get(), output_duplication.GetAddressOf());
        }
        last_frame_time = std::chrono::steady_clock::now();
    };

    // Reinit pesado: recria factory + adapter + device + duplication
    auto TryFullReinit = [&]() {
        output_duplication.Reset();
        cached_staging.Reset();
        d3d_context.Reset();
        d3d_device.Reset();
        dxgi_adapter.Reset();
        dxgi_factory.Reset();

        // Recria do zero
        if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)dxgi_factory.GetAddressOf())))
            return;

        ComPtr<IDXGIAdapter1> picked_adapter;
        ComPtr<IDXGIOutput>   picked_output;
        for (UINT ai = 0; ; ++ai) {
            ComPtr<IDXGIAdapter1> a;
            if (dxgi_factory->EnumAdapters1(ai, a.GetAddressOf()) == DXGI_ERROR_NOT_FOUND) break;
            for (UINT oi = 0; ; ++oi) {
                ComPtr<IDXGIOutput> o;
                if (a->EnumOutputs(oi, o.GetAddressOf()) == DXGI_ERROR_NOT_FOUND) break;
                DXGI_OUTPUT_DESC od{};
                o->GetDesc(&od);
                bool is_primary = (od.DesktopCoordinates.left == 0 && od.DesktopCoordinates.top == 0);
                if (od.AttachedToDesktop && !picked_adapter) {
                    if (current_monitor == 0 ? is_primary : (static_cast<int>(oi) == current_monitor)) {
                        picked_adapter = a; picked_output = o;
                    }
                }
            }
        }
        if (!picked_adapter || !picked_output) return;

        dxgi_adapter = picked_adapter;
        D3D_FEATURE_LEVEL fl[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
        if (FAILED(D3D11CreateDevice(dxgi_adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0,
                fl, ARRAYSIZE(fl), D3D11_SDK_VERSION,
                d3d_device.GetAddressOf(), nullptr, d3d_context.GetAddressOf())))
            return;

        ComPtr<IDXGIOutput1> out1;
        if (FAILED(picked_output.As(&out1))) return;
        out1->DuplicateOutput(d3d_device.Get(), output_duplication.GetAddressOf());

        // Recria staging texture
        D3D11_TEXTURE2D_DESC sd{};
        sd.Width = roi_w; sd.Height = roi_h;
        sd.MipLevels = 1; sd.ArraySize = 1;
        sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        sd.SampleDesc.Count = 1;
        sd.Usage = D3D11_USAGE_STAGING;
        sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        d3d_device->CreateTexture2D(&sd, nullptr, cached_staging.GetAddressOf());

        last_frame_time = std::chrono::steady_clock::now();
    };

    int consecutive_fails = 0;

    while (running) {
        if (!output_duplication) {
            if (consecutive_fails < 5)
                TryLightReinit();   // primeiras tentativas: reinit leve (mais rápido)
            else
                TryFullReinit();    // depois de 5 falhas: reinit completo

            if (!output_duplication) {
                consecutive_fails++;
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                continue;
            }
            consecutive_fails = 0;
        }

        ComPtr<ID3D11Texture2D> frame_texture;
        if (AcquireFrame(frame_texture) && cached_staging) {
            last_frame_time = std::chrono::steady_clock::now();
            consecutive_fails = 0;

            D3D11_BOX roi_box{ (UINT)roi_x, (UINT)roi_y, 0, (UINT)(roi_x + roi_w), (UINT)(roi_y + roi_h), 1 };
            d3d_context->CopySubresourceRegion(cached_staging.Get(), 0, 0, 0, 0, frame_texture.Get(), 0, &roi_box);

            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (SUCCEEDED(d3d_context->Map(cached_staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
                cv::Mat bgra(roi_h, roi_w, CV_8UC4, mapped.pData, mapped.RowPitch);
                cv::Mat bgr;
                cv::cvtColor(bgra, bgr, cv::COLOR_BGRA2BGR);
                d3d_context->Unmap(cached_staging.Get(), 0);

                uint64_t ts = std::chrono::high_resolution_clock::now().time_since_epoch().count();
                std::lock_guard<std::mutex> lk(mat_mutex);
                latest_bgr = bgr;
                latest_bgr_ts = ts;
            }
        } else {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - last_frame_time).count();
            if (elapsed >= STALE_THRESHOLD_SEC) {
                output_duplication.Reset(); // força reinit no próximo ciclo
            } else {
                std::this_thread::yield();
            }
        }
    }
}

bool DXGICaptureOptimized::GetLatestBGR(cv::Mat& out, uint64_t* out_timestamp) {
    std::lock_guard<std::mutex> lk(mat_mutex);
    if (latest_bgr.empty()) return false;
    out = latest_bgr;
    if (out_timestamp) *out_timestamp = latest_bgr_ts;
    return true;
}

bool DXGICaptureOptimized::AcquireFrame(ComPtr<ID3D11Texture2D>& output_texture) {
    if (!output_duplication) return false;

    ComPtr<IDXGIResource> desktop_resource;
    DXGI_OUTDUPL_FRAME_INFO frame_info{};
    HRESULT hr = output_duplication->AcquireNextFrame(0, &frame_info, desktop_resource.GetAddressOf());

    if (hr == DXGI_ERROR_WAIT_TIMEOUT) return false;  // watchdog in CaptureThreadMain handles recovery

    if (FAILED(hr)) {
        if (hr == DXGI_ERROR_ACCESS_LOST)
            output_duplication.Reset();  // watchdog will recreate on next cycle
        return false;
    }

    ComPtr<ID3D11Texture2D> acquired_texture;
    hr = desktop_resource.As(&acquired_texture);
    if (FAILED(hr)) { output_duplication->ReleaseFrame(); return false; }

    D3D11_TEXTURE2D_DESC desc;
    acquired_texture->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;
    desc.MiscFlags = 0;

    ComPtr<ID3D11Texture2D> owned_texture;
    hr = d3d_device->CreateTexture2D(&desc, nullptr, owned_texture.GetAddressOf());
    if (FAILED(hr)) { output_duplication->ReleaseFrame(); return false; }

    d3d_context->CopyResource(owned_texture.Get(), acquired_texture.Get());
    output_duplication->ReleaseFrame();
    output_texture = owned_texture;
    return true;
}

std::shared_ptr<GPUFrame> DXGICaptureOptimized::GetLatestFrame() {
    if (!initialized) return nullptr;
    std::shared_ptr<GPUFrame> latest = nullptr, frame;
    while ((frame = frame_buffer->Dequeue()) != nullptr) latest = frame;
    return latest;
}

void DXGICaptureOptimized::Stop() {
    running = false;
    if (capture_thread.joinable()) capture_thread.join();
}

bool DXGICaptureOptimized::Reinitialize(int roi_size) {
    // Stop thread without releasing D3D resources
    running = false;
    if (capture_thread.joinable()) capture_thread.join();
    cached_staging.Reset(); // will be recreated by new thread with new size

    // Update ROI
    desired_roi_size = roi_size;
    roi_w = std::min(desired_roi_size, capture_width);
    roi_h = std::min(desired_roi_size, capture_height);
    roi_x = (capture_width  - roi_w) / 2;
    roi_y = (capture_height - roi_h) / 2;

    // Restart capture thread
    {
        std::lock_guard<std::mutex> lk(mat_mutex);
        latest_bgr = cv::Mat{};
        latest_bgr_ts = 0;
    }
    running = true;
    capture_thread = std::thread(&DXGICaptureOptimized::CaptureThreadMain, this);
    SetThreadPriority(capture_thread.native_handle(), THREAD_PRIORITY_TIME_CRITICAL);
    return true;
}

void DXGICaptureOptimized::Release() {
    Stop();
    cached_staging.Reset();   // must reset before d3d_device so next Initialize() starts clean
    output_duplication.Reset();
    d3d_context.Reset();
    d3d_device.Reset();
    dxgi_adapter.Reset();
    dxgi_factory.Reset();
    initialized = false;
}
