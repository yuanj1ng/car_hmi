#include "inference.h"
#include <fstream>
#include <QDebug> // 用于打印日志
#include <QDir>
#include <QFile>

Inference::Inference(const std::string& modelPath, const cv::Size& inputSize, const QString& classesPath, bool runOnGPU)
{
    modelInputSize = inputSize;
    loadClasses(classesPath);

    try {
        // 1. 初始化 ORT 环境
        env = new Ort::Env(ORT_LOGGING_LEVEL_WARNING, "YoloV8_ORT");

        // 2. 配置 Session 选项
        Ort::SessionOptions sessionOptions;
        sessionOptions.SetIntraOpNumThreads(1);
        sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

        // 3. 加载模型 (注意：Windows下必须转为宽字符路径)
        std::wstring wModelPath(modelPath.begin(), modelPath.end());
        session = new Ort::Session(*env, wModelPath.c_str(), sessionOptions);

        // 4. 定义输入输出节点名 (YOLOv8 默认名字)
        // 注意：这些字符串必须在 session 生命周期内有效，所以暂时写死或者存为成员变量
        // 这里为了简单，针对 YOLOv8 标准模型写死
        static const char* inputName = "images";
        static const char* outputName = "output0";
        inputNodeNames.push_back(inputName);
        outputNodeNames.push_back(outputName);

        qDebug() << "【成功】ONNX Runtime 模型加载完成！";

    } catch (const std::exception& e) {
        qDebug() << "【致命错误】模型加载失败：" << e.what();
    }
}

Inference::~Inference() {
    if (session) delete session;
    if (env) delete env;
}

void Inference::loadClasses(const QString& classesPath) {
    QFile file(classesPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)){
        qDebug() << "打开classes.txt失败" << classes.size();
        return;
    }
    while (!file.atEnd()) {
        QByteArray line = file.readLine();
        classes.push_back(line.toStdString());
    }
}

cv::Scalar Inference::getColor(int classId) {
    // 简单的随机颜色生成
    int r = (classId * 50) % 255;
    int g = (classId * 80) % 255;
    int b = (classId * 120) % 255;
    return cv::Scalar(b, g, r);
}


std::vector<Detection> Inference::runInference(const cv::Mat& frame) {
    std::vector<Detection> outputDetections;

    // 0. 安全检查
    if (!session || frame.empty()) {
        std::cerr << "❌ 错误：模型未加载或输入图片为空" << std::endl;
        return outputDetections;
    }

    // 1. 预处理 (Pre-process) - 优化版
    // 使用 blobFromImage 一步到位：
    // - 缩放 (size)
    // - 归一化 (1/255.0)
    // - 交换通道 (swapRB=true, 也就是 BGR->RGB)
    // - 转置 (HWC -> CHW)
    cv::Mat chwBlob;
    cv::dnn::blobFromImage(frame, chwBlob, 1.0 / 255.0, modelInputSize, cv::Scalar(), true, false);

    // 2. 创建输入 Tensor
    // inputData 指针必须在 session->Run 期间保持有效
    float* inputData = reinterpret_cast<float*>(chwBlob.data);

    std::vector<int64_t> inputShape = {1, 3, modelInputSize.height, modelInputSize.width};
    size_t inputTensorSize = 1 * 3 * modelInputSize.height * modelInputSize.width;

    Ort::MemoryInfo memoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

    Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
        memoryInfo, inputData, inputTensorSize, inputShape.data(), inputShape.size()
        );

    // 3. 推理 (Run)
    auto outputTensors = session->Run(
        Ort::RunOptions{nullptr},
        inputNodeNames.data(), &inputTensor, 1,
        outputNodeNames.data(), 1
        );

    // 4. 后处理 (Post-process)
    float* floatArr = outputTensors[0].GetTensorMutableData<float>();

    // 获取输出维度
    auto outputInfo = outputTensors[0].GetTensorTypeAndShapeInfo();
    std::vector<int64_t> outputShape = outputInfo.GetShape();

    // YOLOv8 输出通常是 [1, channels, anchors]
    // 例如: [1, 5, 8400] (如果是 nc=1) 或 [1, 84, 8400] (如果是 nc=80)
    int rows = outputShape[1];       // 通道数 (4个坐标 + 类别数)
    int dimensions = outputShape[2]; // 锚框数量 (通常是 8400)

    // ✅ 关键修复：动态计算类别数量，不再写死 80！
    int num_classes = rows - 4;

    // 计算缩放比例 (原图 / 模型图)
    float x_factor = (float)frame.cols / modelInputSize.width;
    float y_factor = (float)frame.rows / modelInputSize.height;

    std::vector<int> class_ids;
    std::vector<float> confidences;
    std::vector<cv::Rect> boxes;

    // 遍历 8400 个锚框
    for (int i = 0; i < dimensions; ++i) {
        float maxScore = 0.0f;
        int classId = -1;

        // ✅ 关键修复：只遍历实际存在的类别
        for (int c = 0; c < num_classes; ++c) {
            // 数据布局是 [rows, dimensions]，所以跨行读取要 + dimensions
            // 第 (4+c) 行是类别分数
            float score = floatArr[(4 + c) * dimensions + i];
            if (score > maxScore) {
                maxScore = score;
                classId = c;
            }
        }

        // 阈值过滤 (建议 0.45 或 0.5)
        if (maxScore > 0.45f) {
            // 解析坐标 (前4行)
            float x = floatArr[0 * dimensions + i];
            float y = floatArr[1 * dimensions + i];
            float w = floatArr[2 * dimensions + i];
            float h = floatArr[3 * dimensions + i];

            // 还原坐标 (Center-XY -> TopLeft-XY) 并缩放回原图
            int left = int((x - 0.5 * w) * x_factor);
            int top = int((y - 0.5 * h) * y_factor);
            int width = int(w * x_factor);
            int height = int(h * y_factor);

            // 存入候选列表
            boxes.push_back(cv::Rect(left, top, width, height));
            confidences.push_back(maxScore);
            class_ids.push_back(classId);
        }
    }

    // 5. NMS (非极大值抑制)
    std::vector<int> indices;
    // NMS 阈值建议 0.45 或 0.5
    cv::dnn::NMSBoxes(boxes, confidences, 0.45f, 0.5f, indices);

    // 6. 封装结果
    for (int i : indices) {
        Detection det;
        det.class_id = class_ids[i];
        det.confidence = confidences[i];
        det.box = boxes[i];

        // 防止 class_id 越界导致崩溃
        if (det.class_id >= 0 && det.class_id < classes.size()) {
            det.className = classes[det.class_id];
        } else {
            det.className = "Unknown";
        }

        // 假设你有 getColor 函数
        // det.color = getColor(det.class_id);

        outputDetections.push_back(det);
    }

    return outputDetections;
}

