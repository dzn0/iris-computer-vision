#define NOMINMAX
#include "../hdr/YOLOInference.h"
#include <iostream>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <windows.h>

YOLOInference::YOLOInference()
    : ort_session(nullptr), ort_env(nullptr), memory_info(nullptr),
      input_width(480), input_height(480), input_channels(3) {
    class_names = {"enemy_head", "enemy_body"};  // class 0=head, class 1=body
}

YOLOInference::~YOLOInference() {
    Release();
}

static void SetupSessionOptions(Ort::SessionOptions& opts) {
    opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    opts.DisableMemPattern();
    opts.SetExecutionMode(ORT_SEQUENTIAL);

    // Use DirectML for GPU acceleration
    OrtStatus* dml_status = OrtSessionOptionsAppendExecutionProvider_DML(opts, 0);
    if (dml_status != nullptr)
        Ort::GetApi().ReleaseStatus(dml_status);
}

static void PopulateNames(Ort::Session* session, Ort::AllocatorWithDefaultOptions& alloc,
                           std::vector<std::string>& input_names, std::vector<std::string>& output_names,
                           std::vector<const char*>& input_c, std::vector<const char*>& output_c) {
    size_t n_in  = session->GetInputCount();
    size_t n_out = session->GetOutputCount();

    for (size_t i = 0; i < n_in; ++i) {
        Ort::AllocatedStringPtr p(session->GetInputNameAllocated(i, alloc));
        input_names.emplace_back(p.get());
    }
    for (size_t i = 0; i < n_out; ++i) {
        Ort::AllocatedStringPtr p(session->GetOutputNameAllocated(i, alloc));
        output_names.emplace_back(p.get());
    }

    input_c.clear();
    for (const auto& s : input_names) input_c.push_back(s.c_str());
    output_c.clear();
    for (const auto& s : output_names) output_c.push_back(s.c_str());
}

void YOLOInference::PreallocateBuffers() {
    const size_t n = static_cast<size_t>(input_channels) * input_height * input_width;
    input_buffer_.assign(n, 0.0f);
    input_shape_ = {1, static_cast<int64_t>(input_channels),
                       static_cast<int64_t>(input_height),
                       static_cast<int64_t>(input_width)};
    input_ort_value_ = Ort::Value::CreateTensor<float>(
        *memory_info, input_buffer_.data(), input_buffer_.size(),
        input_shape_.data(), input_shape_.size());
    letterbox_mat_ = cv::Mat(input_height, input_width, CV_8UC3, cv::Scalar(114, 114, 114));
}

bool YOLOInference::LoadModel(const std::string& model_path) {
    try {
        ort_env = new Ort::Env(ORT_LOGGING_LEVEL_FATAL, "iris_detector");
        Ort::SessionOptions opts;
        SetupSessionOptions(opts);
        std::wstring wpath(model_path.begin(), model_path.end());
        ort_session = new Ort::Session(*ort_env, wpath.c_str(), opts);
        memory_info = new Ort::MemoryInfo(Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault));
        Ort::AllocatorWithDefaultOptions alloc;
        PopulateNames(ort_session, alloc, input_names, output_names, input_names_c_, output_names_c_);
        PreallocateBuffers();
        return true;
    } catch (...) { Release(); return false; }
}

bool YOLOInference::LoadModelFromMemory(const void* data, size_t size) {
    try {
        ort_env = new Ort::Env(ORT_LOGGING_LEVEL_FATAL, "iris_detector");
        Ort::SessionOptions opts;
        SetupSessionOptions(opts);
        ort_session = new Ort::Session(*ort_env, data, size, opts);
        memory_info = new Ort::MemoryInfo(Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault));
        Ort::AllocatorWithDefaultOptions alloc;
        PopulateNames(ort_session, alloc, input_names, output_names, input_names_c_, output_names_c_);
        // Auto-detecta dimensões do modelo (suporta 480x480, 640x640, etc.)
        auto in_shape = ort_session->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
        if (in_shape.size() == 4) {
            input_height = static_cast<int>(in_shape[2]);
            input_width  = static_cast<int>(in_shape[3]);
        }
        PreallocateBuffers();
        return true;
    } catch (...) { Release(); return false; }
}

bool YOLOInference::RunInference(const cv::Mat& input_image, std::vector<Detection>& detections) {
    if (!ort_session) return false;
    try {
        Preprocess(input_image);
        auto output_tensors = ort_session->Run(
            Ort::RunOptions{nullptr},
            input_names_c_.data(), &input_ort_value_, input_names_c_.size(),
            output_names_c_.data(), output_names_c_.size());
        return ParseOutput(output_tensors, detections);
    } catch (...) { return false; }
}

void YOLOInference::Preprocess(const cv::Mat& src) {
    last_src_width  = src.cols;
    last_src_height = src.rows;
    const float scale = std::min(static_cast<float>(input_width)  / last_src_width,
                                 static_cast<float>(input_height) / last_src_height);
    const int new_w = static_cast<int>(last_src_width  * scale);
    const int new_h = static_cast<int>(last_src_height * scale);
    const int pad_x = (input_width  - new_w) / 2;
    const int pad_y = (input_height - new_h) / 2;
    last_lb_scale = scale;
    last_lb_pad_x = static_cast<float>(pad_x);
    last_lb_pad_y = static_cast<float>(pad_y);

    cv::resize(src, resize_mat_, cv::Size(new_w, new_h), 0, 0, cv::INTER_LINEAR);
    static int cached_pad_x = -1, cached_pad_y = -1, cached_new_w = -1, cached_new_h = -1;
    if (pad_x != cached_pad_x || pad_y != cached_pad_y ||
        new_w != cached_new_w || new_h != cached_new_h) {
        letterbox_mat_.setTo(cv::Scalar(114, 114, 114));
        cached_pad_x = pad_x; cached_pad_y = pad_y;
        cached_new_w = new_w; cached_new_h = new_h;
    }
    resize_mat_.copyTo(letterbox_mat_(cv::Rect(pad_x, pad_y, new_w, new_h)));

    const int H = input_height, W = input_width, plane = H * W;
    float* __restrict R = input_buffer_.data() + 0 * plane;
    float* __restrict G = input_buffer_.data() + 1 * plane;
    float* __restrict B = input_buffer_.data() + 2 * plane;
    const float inv255 = 1.0f / 255.0f;

    for (int y = 0; y < H; ++y) {
        const uchar* __restrict row = letterbox_mat_.ptr<uchar>(y);
        const int base = y * W;
        for (int x = 0; x < W; ++x) {
            R[base + x] = row[x * 3 + 2] * inv255;
            G[base + x] = row[x * 3 + 1] * inv255;
            B[base + x] = row[x * 3 + 0] * inv255;
        }
    }
}

bool YOLOInference::ParseOutput(const std::vector<Ort::Value>& outputs, std::vector<Detection>& detections) {
    if (outputs.empty()) return false;
    try {
        const Ort::Value& output = outputs[0];
        auto shape = output.GetTensorTypeAndShapeInfo().GetShape();

        const float* output_data = output.GetTensorData<float>();
        detections.clear();
        last_raw_stats_ = {};

        const float inv_src_w = (last_src_width  > 0) ? 1.0f / static_cast<float>(last_src_width)  : 1.0f;
        const float inv_src_h = (last_src_height > 0) ? 1.0f / static_cast<float>(last_src_height) : 1.0f;
        const float inv_scale = (last_lb_scale > 0.0f) ? 1.0f / last_lb_scale : 1.0f;

        // Detecta formato do output:
        //   Formato A (transposto): [1, num_fields, num_anchors] — shape[2] >> shape[1]
        //   Formato B (pós-NMS):   [1, num_dets,   6          ] — shape[2] == 6
        const bool post_nms = (shape.size() >= 3 && shape[2] == 6);

        if (post_nms) {
            // Formato B: [1, N, 6] onde cada linha = [x1, y1, x2, y2, conf, class_id]
            const int num_dets = static_cast<int>(shape[1]);
            last_raw_stats_.total_anchors = num_dets;

            for (int i = 0; i < num_dets; ++i) {
                const float* row = output_data + i * 6;
                float confidence = row[4];
                int   class_id   = static_cast<int>(row[5]);

                if (confidence > last_raw_stats_.max_conf) last_raw_stats_.max_conf = confidence;
                if (confidence >= 0.25f) last_raw_stats_.above_25++;
                if (confidence >= 0.50f) last_raw_stats_.above_50++;
                if (confidence >= 0.75f) last_raw_stats_.above_75++;

                if (confidence < conf_threshold_) continue;

                float x1 = (std::clamp(row[0], 0.0f, (float)input_width)  - last_lb_pad_x) * inv_scale;
                float y1 = (std::clamp(row[1], 0.0f, (float)input_height) - last_lb_pad_y) * inv_scale;
                float x2 = (std::clamp(row[2], 0.0f, (float)input_width)  - last_lb_pad_x) * inv_scale;
                float y2 = (std::clamp(row[3], 0.0f, (float)input_height) - last_lb_pad_y) * inv_scale;

                float px_w = x2 - x1, px_h = y2 - y1;
                if (px_w <= 0.0f || px_h <= 0.0f) continue;

                Detection det;
                det.cx = (x1 + px_w * 0.5f) * inv_src_w;
                det.cy = (y1 + px_h * 0.5f) * inv_src_h;
                det.w  = px_w * inv_src_w;
                det.h  = px_h * inv_src_h;
                det.confidence = confidence;
                det.class_id = (class_id >= 0 && class_id < (int)class_names.size()) ? class_id : 0;
                det.class_name = GetClassName(det.class_id);
                detections.push_back(det);
            }
        } else {
            // Formato A (transposto): [1, num_fields, num_anchors]
            // YOLOv8 padrão: campos [cx, cy, w, h, conf_cls0, conf_cls1, ...]
            const int num_anchors = static_cast<int>(shape[2]);
            last_raw_stats_.total_anchors = num_anchors;

            for (int i = 0; i < num_anchors; ++i) {
                float conf_class0 = output_data[4 * num_anchors + i];
                float conf_class1 = output_data[5 * num_anchors + i];

                float confidence;
                int class_id;
                if (conf_class0 > conf_class1) { confidence = conf_class0; class_id = 0; }
                else                           { confidence = conf_class1; class_id = 1; }

                if (confidence > last_raw_stats_.max_conf) last_raw_stats_.max_conf = confidence;
                if (confidence >= 0.25f) last_raw_stats_.above_25++;
                if (confidence >= 0.50f) last_raw_stats_.above_50++;
                if (confidence >= 0.75f) last_raw_stats_.above_75++;

                if (confidence < conf_threshold_) continue;

                // end2end=True: campos 0-3 já são x1,y1,x2,y2 decodificados
                float x1 = (std::clamp(output_data[0 * num_anchors + i], 0.0f, (float)input_width)  - last_lb_pad_x) * inv_scale;
                float y1 = (std::clamp(output_data[1 * num_anchors + i], 0.0f, (float)input_height) - last_lb_pad_y) * inv_scale;
                float x2 = (std::clamp(output_data[2 * num_anchors + i], 0.0f, (float)input_width)  - last_lb_pad_x) * inv_scale;
                float y2 = (std::clamp(output_data[3 * num_anchors + i], 0.0f, (float)input_height) - last_lb_pad_y) * inv_scale;

                float px_w = x2 - x1, px_h = y2 - y1;
                if (px_w <= 0.0f || px_h <= 0.0f) continue;

                Detection det;
                det.cx = (x1 + px_w * 0.5f) * inv_src_w;
                det.cy = (y1 + px_h * 0.5f) * inv_src_h;
                det.w  = px_w * inv_src_w;
                det.h  = px_h * inv_src_h;
                det.confidence = confidence;
                det.class_id = class_id < (int)class_names.size() ? class_id : 0;
                det.class_name = GetClassName(det.class_id);
                detections.push_back(det);
            }
        }
        return true;
    } catch (...) { return false; }
}

std::string YOLOInference::GetClassName(int class_id) const {
    switch (class_id) {
        case 0: return "enemy_head";
        case 1: return "enemy_body";
        default: return "unknown";
    }
}

void YOLOInference::DrawDetections(cv::Mat& frame, const std::vector<Detection>& detections) {
    if (frame.empty()) return;

    for (const auto& det : detections) {
        // Convert normalized coordinates to pixel coordinates
        int x1 = static_cast<int>((det.cx - det.w / 2.0f) * frame.cols);
        int y1 = static_cast<int>((det.cy - det.h / 2.0f) * frame.rows);
        int x2 = static_cast<int>((det.cx + det.w / 2.0f) * frame.cols);
        int y2 = static_cast<int>((det.cy + det.h / 2.0f) * frame.rows);

        // Clamp to frame boundaries
        x1 = std::max(0, x1);
        y1 = std::max(0, y1);
        x2 = std::min(frame.cols - 1, x2);
        y2 = std::min(frame.rows - 1, y2);

        // Color based on class
        cv::Scalar color = (det.class_id == 0) ? cv::Scalar(0, 255, 0) : cv::Scalar(0, 0, 255);

        // Draw bounding box only (no text)
        cv::rectangle(frame, {x1, y1}, {x2, y2}, color, 2);
    }
}

void YOLOInference::Release() {
    input_ort_value_ = Ort::Value(nullptr);
    delete ort_session; ort_session = nullptr;
    delete memory_info; memory_info = nullptr;
    delete ort_env;     ort_env = nullptr;
    input_names.clear(); output_names.clear();
    input_names_c_.clear(); output_names_c_.clear();
    input_buffer_.clear(); input_buffer_.shrink_to_fit();
}
