#include "inference.h"
#include <fstream>
#include <QDebug> // 用于打印日志

Inference::Inference(const std::string& modelPath, const cv::Size& inputSize, const std::string& classesPath, bool runOnGPU)
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

void Inference::loadClasses(const std::string& classesPath) {
    // 简单起见，这里直接硬编码 COCO 80 类，防止文件读取错误
    // 如果你一定要读文件，保持原样即可
    classes = {
        "person", "bicycle", "car", "motorcycle", "airplane", "bus", "train", "truck", "boat", "traffic light",
        "fire hydrant", "stop sign", "parking meter", "bench", "bird", "cat", "dog", "horse", "sheep", "cow",
        "elephant", "bear", "zebra", "giraffe", "backpack", "umbrella", "handbag", "tie", "suitcase", "frisbee",
        "skis", "snowboard", "sports ball", "kite", "baseball bat", "baseball glove", "skateboard", "surfboard",
        "tennis racket", "bottle", "wine glass", "cup", "fork", "knife", "spoon", "bowl", "banana", "apple",
        "sandwich", "orange", "broccoli", "carrot", "hot dog", "pizza", "donut", "cake", "chair", "couch",
        "potted plant", "bed", "dining table", "toilet", "tv", "laptop", "mouse", "remote", "keyboard", "cell phone",
        "microwave", "oven", "toaster", "sink", "refrigerator", "book", "clock", "vase", "scissors", "teddy bear",
        "hair drier", "toothbrush"
    };
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
    if (!session) return outputDetections;

    // 1. 预处理 (Pre-process)
    cv::Mat blob;
    // YOLOv8 要求 RGB，且归一化 0~1
    cv::cvtColor(frame, blob, cv::COLOR_BGR2RGB);
    cv::resize(blob, blob, modelInputSize);
    blob.convertTo(blob, CV_32F, 1.0 / 255.0);

    // 2. 转换数据布局 (HWC -> CHW)
    // ONNX Runtime 需要 NCHW 格式的 float 数组
    cv::Mat chwBlob;
    cv::dnn::blobFromImage(blob, chwBlob); // 借用 OpenCV 的这个函数来转置，非常方便

    // 3. 创建输入 Tensor
    std::vector<int64_t> inputShape = {1, 3, modelInputSize.height, modelInputSize.width};
    size_t inputTensorSize = 1 * 3 * modelInputSize.height * modelInputSize.width;

    // 获取数据指针
    float* inputData = (float*)chwBlob.data;

    // 创建 MemoryInfo
    Ort::MemoryInfo memoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

    // 创建 Tensor
    Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
        memoryInfo, inputData, inputTensorSize, inputShape.data(), inputShape.size()
        );

    // 4. 推理 (Run)
    auto outputTensors = session->Run(
        Ort::RunOptions{nullptr},
        inputNodeNames.data(), &inputTensor, 1,
        outputNodeNames.data(), 1
        );

    // 5. 后处理 (Post-process)
    // YOLOv8 输出维度通常是 [1, 84, 8400] (84 = xywh + 80 classes)
    float* floatArr = outputTensors[0].GetTensorMutableData<float>();

    // 获取输出维度信息
    auto outputInfo = outputTensors[0].GetTensorTypeAndShapeInfo();
    std::vector<int64_t> outputShape = outputInfo.GetShape();
    // dim0=1, dim1=84, dim2=8400
    int rows = outputShape[1]; // 84
    int dimensions = outputShape[2]; // 8400

    // 需要转置？YOLOv8输出通常需要把 84x8400 转置为 8400x84 方便处理
    // 这里为了性能，我们直接手动解析，不进行矩阵转置操作

    // 缩放比例 (原图 / 模型图)
    float x_factor = (float)frame.cols / modelInputSize.width;
    float y_factor = (float)frame.rows / modelInputSize.height;

    std::vector<int> class_ids;
    std::vector<float> confidences;
    std::vector<cv::Rect> boxes;

    // 遍历所有锚框 (8400个)
    for (int i = 0; i < dimensions; ++i) {
        // 找出得分最高的类别
        float maxScore = 0.0f;
        int classId = -1;

        // 类别分数从第4个索引开始 (0,1,2,3 是 x,y,w,h)
        for (int c = 0; c < 80; ++c) {
            // 访问 floatArr[row * dimensions + col]
            // 但这里数据是 [84, 8400]，所以第 c 个类别的分数在 floatArr[(4+c) * dimensions + i]
            float score = floatArr[(4 + c) * dimensions + i];
            if (score > maxScore) {
                maxScore = score;
                classId = c;
            }
        }

        if (maxScore > 0.45) { // 置信度阈值
            // 解析坐标
            float x = floatArr[0 * dimensions + i];
            float y = floatArr[1 * dimensions + i];
            float w = floatArr[2 * dimensions + i];
            float h = floatArr[3 * dimensions + i];

            // 还原回原图坐标
            int left = int((x - 0.5 * w) * x_factor);
            int top = int((y - 0.5 * h) * y_factor);
            int width = int(w * x_factor);
            int height = int(h * y_factor);

            boxes.push_back(cv::Rect(left, top, width, height));
            confidences.push_back(maxScore);
            class_ids.push_back(classId);
        }
    }

    // NMS (非极大值抑制) - 去重
    std::vector<int> indices;
    cv::dnn::NMSBoxes(boxes, confidences, 0.45, 0.5, indices);

    for (int i : indices) {
        Detection det;
        det.class_id = class_ids[i];
        det.confidence = confidences[i];
        det.box = boxes[i];
        det.className = (det.class_id < classes.size()) ? classes[det.class_id] : "Unknown";
        det.color = getColor(det.class_id);
        outputDetections.push_back(det);
    }

    return outputDetections;
}
