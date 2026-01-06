#ifndef INFERENCE_H
#define INFERENCE_H

#include <string>
#include <vector>
#include <opencv2/opencv.hpp>
#include <onnxruntime_cxx_api.h> // 必须引用这个

struct Detection {
    int class_id;
    float confidence;
    cv::Rect box;
    std::string className;
    cv::Scalar color;
};

class Inference {
public:
    // 构造函数
    Inference(const std::string& modelPath, const cv::Size& inputSize, const std::string& classesPath, bool runOnGPU);
    ~Inference();

    // 核心推理函数
    std::vector<Detection> runInference(const cv::Mat& frame);

private:
    // ORT 核心资源
    Ort::Env* env = nullptr;
    Ort::Session* session = nullptr;

    // 输入输出节点的名称（YOLOv8通常是 images 和 output0）
    std::vector<const char*> inputNodeNames;
    std::vector<const char*> outputNodeNames;

    cv::Size modelInputSize;
    std::vector<std::string> classes;

    // 辅助函数：加载类别文件
    void loadClasses(const std::string& classesPath);
    // 辅助函数：生成颜色
    cv::Scalar getColor(int classId);
};

#endif // INFERENCE_H
