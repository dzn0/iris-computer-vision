#pragma once
#define NOMINMAX
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <thread>
#include <atomic>
#include <memory>
#include <mutex>
#include <queue>
#include <opencv2/opencv.hpp>

using Microsoft::WRL::ComPtr;

static constexpr int ROI_SIZE = 640;

struct GPUFrame {
    ComPtr<ID3D11Texture2D> texture;
    int width;
    int height;
    uint64_t timestamp;
};

class GPUFrameBuffer {
public:
    GPUFrameBuffer(int capacity = 3);
    ~GPUFrameBuffer();

    bool Enqueue(const std::shared_ptr<GPUFrame>& frame);
    std::shared_ptr<GPUFrame> Dequeue();

    int GetCapacity() const { return capacity; }
    int GetSize() const;
    bool IsEmpty() const;

private:
    std::queue<std::shared_ptr<GPUFrame>> buffer;
    mutable std::mutex mutex;
    int capacity;
};

class DXGICaptureOptimized {
public:
    DXGICaptureOptimized();
    ~DXGICaptureOptimized();

    bool Initialize(int monitor_index = 0);
    std::shared_ptr<GPUFrame> GetLatestFrame();
    bool GetLatestBGR(cv::Mat& out, uint64_t* out_timestamp = nullptr);

    ID3D11Device* GetDevice() const { return d3d_device.Get(); }
    ID3D11DeviceContext* GetDeviceContext() const { return d3d_context.Get(); }

    int GetWidth() const { return capture_width; }
    int GetHeight() const { return capture_height; }
    int GetROIX()      const { return roi_x; }
    int GetROIY()      const { return roi_y; }
    int GetROIWidth()  const { return roi_w; }
    int GetROIHeight() const { return roi_h; }

    // Reinitialize with a new ROI size (reuses existing D3D device)
    bool Reinitialize(int roi_size);

    void Stop();
    void Release();

private:
    ComPtr<IDXGIFactory1> dxgi_factory;
    ComPtr<IDXGIAdapter1> dxgi_adapter;
    ComPtr<ID3D11Device> d3d_device;
    ComPtr<ID3D11DeviceContext> d3d_context;
    ComPtr<IDXGIOutputDuplication> output_duplication;

    std::unique_ptr<GPUFrameBuffer> frame_buffer;

    ComPtr<ID3D11Texture2D> cached_staging;
    std::mutex mat_mutex;
    cv::Mat latest_bgr;
    uint64_t latest_bgr_ts = 0;

    std::thread capture_thread;
    std::atomic<bool> running;
    std::atomic<bool> initialized;

    int capture_width;
    int capture_height;
    int current_monitor;

    int roi_x = 0, roi_y = 0, roi_w = 0, roi_h = 0;
    int desired_roi_size = ROI_SIZE;

    bool InitializeDXGI(int monitor_index);
    void CaptureThreadMain();
    bool AcquireFrame(ComPtr<ID3D11Texture2D>& output_texture);
};
