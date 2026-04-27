#pragma once
#define NOMINMAX
#include <onnxruntime_cxx_api.h>
#include <dml_provider_factory.h>
#include <opencv2/opencv.hpp>
#include <vector>
#include <string>
#include <windows.h>

class YOLOInference {
public:
    struct Detection {
        float cx, cy;
        float w, h;
        float confidence;
        int class_id;
        std::string class_name;
    };

    YOLOInference();
    ~YOLOInference();

    bool LoadModel(const std::string& model_path);
    bool LoadModelFromMemory(const void* data, size_t size);
    bool RunInference(const cv::Mat& input_image, std::vector<Detection>& detections);

    int GetInputWidth() const { return input_width; }
    int GetInputHeight() const { return input_height; }
    std::string GetClassName(int class_id) const;

    // Draw detections on frame (temporary, in-memory rendering)
    static void DrawDetections(cv::Mat& frame, const std::vector<Detection>& detections);

    struct RawStats {
        int total_anchors = 0;
        int above_25 = 0;
        int above_50 = 0;
        int above_75 = 0;
        float max_conf = 0.0f;
    };
    RawStats GetLastRawStats() const { return last_raw_stats_; }

    void SetConfidenceThreshold(float thresh) { conf_threshold_ = thresh; }
    float GetConfidenceThreshold() const { return conf_threshold_; }

    void Release();

private:
    Ort::Session* ort_session;
    Ort::Env* ort_env;
    Ort::MemoryInfo* memory_info;

    int input_width;
    int input_height;
    int input_channels;

    int last_src_width = 0;
    int last_src_height = 0;
    float last_lb_scale = 1.0f;
    float last_lb_pad_x = 0.0f;
    float last_lb_pad_y = 0.0f;

    std::vector<std::string> input_names;
    std::vector<std::string> output_names;
    std::vector<std::string> class_names;

    RawStats last_raw_stats_;
    float conf_threshold_ = 0.75f;

    std::vector<float> input_buffer_;
    Ort::Value         input_ort_value_{nullptr};
    std::vector<int64_t> input_shape_;
    cv::Mat            letterbox_mat_;
    cv::Mat            resize_mat_;
    std::vector<const char*> input_names_c_;
    std::vector<const char*> output_names_c_;

    void Preprocess(const cv::Mat& src);
    bool ParseOutput(const std::vector<Ort::Value>& outputs, std::vector<Detection>& detections);
    void PreallocateBuffers();
};
