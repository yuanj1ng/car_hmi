#include "vision.h"
#include <QDir>

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
    QString currentPath = QDir::currentPath ();
    std::string modelPath = currentPath.toStdString() + "/3dparty/yolov8s.onnx";
    QString classesPath = currentPath + "/3dparty/classes.txt"; // 哪怕是个空文件也要有，不然报错

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

    QList<QPoint> targetList;

    int detections = output.size();
    // qDebug() << "检测到目标数量:" << detections;

    // 2. 画框 (和 main.cpp 的逻辑一模一样)
    if(showAiState){
        for (int i = 0; i < detections; ++i)
        {
            Detection detection = output[i];
            cv::Rect box = detection.box;
            cv::Scalar color = detection.color;
            cv::Rect safeBox = box & cv::Rect(0, 0, frame.cols, frame.rows);

            if(showAiRect){
                // 画框
                cv::rectangle(frame, box, color, 2);

                // 画文字
                std::string classString = detection.className + ' ' + std::to_string(detection.confidence).substr(0, 4);
                cv::Size textSize = cv::getTextSize(classString, cv::FONT_HERSHEY_DUPLEX, 1, 2, 0);
                cv::Rect textBox(box.x, box.y - 40, textSize.width + 10, textSize.height + 20);

                cv::rectangle(frame, textBox, color, cv::FILLED);
                cv::putText(frame, classString, cv::Point(box.x + 5, box.y - 10), cv::FONT_HERSHEY_DUPLEX, 1, cv::Scalar(0, 0, 0), 2, 0);
            }
            //截图
            cv::Mat croppedImage = frame(safeBox);
            //hsv在这个croppedImage 找出像素中心点
            cv::Mat hsv;
            cv::cvtColor(croppedImage, hsv, cv::COLOR_BGR2HSV);

            cv::Mat mask;
            cv::inRange(hsv, cv::Scalar(35, 43, 46), cv::Scalar(77, 255, 255), mask);
            // 2. 找重心 (Moments)
            cv::Moments m = cv::moments(mask, true);
            if (m.m00 > 0) { // 确保真的有绿色像素
                int cx = int(m.m10 / m.m00);
                int cy = int(m.m01 / m.m00);

                // 注意：这个 cx, cy 是相对于 croppedImage (小图) 的坐标
                // 如果要换算回大图 frame 的坐标：
                int globalX = box.x + cx;
                int globalY = box.y + cy;

                // 画个圈标记一下
                cv::circle(frame, cv::Point(globalX, globalY), 5, cv::Scalar(0, 0, 255), -1);
                targetList.push_back({globalX, globalY});
                qDebug() << targetList.size();
            }
        }
    }
    emit sendTarget(targetList);
    // 3. 【关键改动】替代 cv::imshow
    // Qt 显示不了 BGR 格式，必须转成 RGB
    cv::cvtColor(frame, frame, cv::COLOR_BGR2RGB);

    // 转成 QImage
    QImage qImg((const uchar*)frame.data, frame.cols, frame.rows, frame.step, QImage::Format_RGB888);

    // 深拷贝一份发送出去 (因为 frame 函数结束后会销毁)
    emit sendResult(qImg.copy());
    qDebug() << "发送成功";
}

void Vision::change_showAiState(bool state){
    showAiState = state;
    qDebug() << "修改为" << state;
}

void Vision::change_showAiRect(bool state){
    showAiRect = state;
    qDebug() << "修改为" << state;
}
