#ifndef INFERENCE_H
#define INFERENCE_H

#include <opencv2/opencv.hpp>
#include <vector>
#include <string>
#include <QString>
#include "rknn_api.h" // 替换为瑞芯微的 NPU API

struct Detection {
    int class_id;
    float confidence;
    cv::Rect box;
    std::string className;
    int targetX;
    int targetY;
};

class Inference
{
public:
    // 增加 rknn_core_mask 参数
    Inference(const std::string& modelPath, 
              const cv::Size& inputSize, 
              const QString& classesPath,
              rknn_core_mask core_mask = RKNN_NPU_CORE_AUTO);
    ~Inference();

    std::vector<Detection> runInference(const cv::Mat& frame);

private:
    void loadClasses(const QString& classesPath);

    cv::Size modelInputSize;
    std::vector<std::string> classes;

    // NPU 核心上下文
    rknn_context ctx = 0;
    rknn_input_output_num io_num;
    rknn_tensor_attr* input_attrs = nullptr;
    rknn_tensor_attr* output_attrs = nullptr;
};

#endif // INFERENCE_H