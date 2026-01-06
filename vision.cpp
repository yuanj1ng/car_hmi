#include "vision.h"


Vision::Vision(QObject *parent) : QObject(parent)
{
}

Vision::~Vision()
{
    if (m_inf) delete m_inf;
}

void Vision::loadYoloModel()
{
    // --- 对应 main 中的路径设置 ---
    // 改成你的真实路径，注意用正斜杠 /
    std::string modelPath = "C:/Users/25820/Documents/yolo/3rdparty/yolov8s.onnx";
    std::string classesPath = "C:/Users/25820/Documents/yolo/3rdparty/classes.txt"; // 哪怕是个空文件也要有，不然报错

    // 你的电脑目前用 CPU 比较稳，先设为 false
    bool runOnGPU = false;

    // 模型输入尺寸 (YOLOv8 默认 640x640)
    cv::Size size(640, 640);

    // --- 对应 main 中的 Inference inf(...) ---
    try {
        m_inf = new Inference(modelPath, size, classesPath, runOnGPU);
        qDebug() << "YOLO 模型加载成功！";
    } catch (const std::exception& e) {
        qDebug() << "模型加载失败：" << e.what();
    }
}

void Vision::detectFrame(cv::Mat frame)
{
    if (!m_inf || frame.empty()) return;

    // === 下面是直接从 main.cpp 移植过来的核心逻辑 ===

    // 1. 推理 (Inference starts here...)
    std::vector<Detection> output = m_inf->runInference(frame);

    int detections = output.size();
    // qDebug() << "检测到目标数量:" << detections;

    // 2. 画框 (和 main.cpp 的逻辑一模一样)
    for (int i = 0; i < detections; ++i)
    {
        Detection detection = output[i];
        cv::Rect box = detection.box;
        cv::Scalar color = detection.color;

        // 画框
        cv::rectangle(frame, box, color, 2);

        // 画文字
        std::string classString = detection.className + ' ' + std::to_string(detection.confidence).substr(0, 4);
        cv::Size textSize = cv::getTextSize(classString, cv::FONT_HERSHEY_DUPLEX, 1, 2, 0);
        cv::Rect textBox(box.x, box.y - 40, textSize.width + 10, textSize.height + 20);

        cv::rectangle(frame, textBox, color, cv::FILLED);
        cv::putText(frame, classString, cv::Point(box.x + 5, box.y - 10), cv::FONT_HERSHEY_DUPLEX, 1, cv::Scalar(0, 0, 0), 2, 0);
    }
    // === 移植结束 ===

    // 3. 【关键改动】替代 cv::imshow
    // Qt 显示不了 BGR 格式，必须转成 RGB
    cv::cvtColor(frame, frame, cv::COLOR_BGR2RGB);

    // 转成 QImage
    QImage qImg((const uchar*)frame.data, frame.cols, frame.rows, frame.step, QImage::Format_RGB888);

    // 深拷贝一份发送出去 (因为 frame 函数结束后会销毁)
    emit sendResult(qImg.copy());
}
