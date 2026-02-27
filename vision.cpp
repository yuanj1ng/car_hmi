#include "vision.h"
#include <QDir>
#include <QCoreApplication>

Vision::Vision(QObject *parent) : QObject(parent)
{
    // 初始化抓图定时器
    m_captureTimer = new QTimer(this);
    connect(m_captureTimer, &QTimer::timeout, this, [this](){
        cv::Mat frame;
        if (m_cap.isOpened() && m_cap.read(frame)) {
            this->detectFrame(frame);
        }
    });
}

Vision::~Vision()
{
    if (m_captureTimer) m_captureTimer->stop();
    if (m_cap.isOpened()) m_cap.release();
    if (m_inf) {
        delete m_inf;
        m_inf = nullptr;
    }
}

void Vision::loadYoloModel()
{
    QString appPath = QCoreApplication::applicationDirPath();
    std::string modelPath = QDir(appPath).filePath("3dparty/yolov8s.onnx").toStdString();
    QString classesPath = QDir(appPath).filePath("3dparty/classes.txt");

    bool runOnGPU = false;
    cv::Size size(640, 640);

    try {
        m_inf = new Inference(modelPath, size, classesPath, runOnGPU);
        qDebug() << "YOLO 模型加载成功！";
    } catch (const std::exception& e) {
        qDebug() << "模型加载失败：" << e.what();
    }
}

// --- 新增：连接手机摄像头的具体实现 ---
void Vision::startPhoneCamera(const QString &url)
{
    if (m_cap.isOpened()) {
        m_captureTimer->stop();
        m_cap.release();
    }

    if (m_cap.open(url.toStdString())) {
        qDebug() << "成功连接到流:" << url;
        m_captureTimer->start(100); // 约 30 FPS
    } else {
        qDebug() << "❌ 连接失败，请检查 IP:" << url;
    }
}

void Vision::detectFrame(cv::Mat frame)
{
    qDebug() << "开始图片检测";
    if (!m_inf || frame.empty()) return;

    std::vector<Detection> output = m_inf->runInference(frame);
    cv::Mat rgbFrame = frame.clone();

    int detections = output.size();
    for (int i = 0; i < detections; ++i)
        {
            Detection& detection = output[i];
            cv::Rect box = detection.box;
            cv::Scalar color = detection.color;
            cv::Rect safeBox = box & cv::Rect(0, 0, frame.cols, frame.rows);

            // 1. 画框
            cv::rectangle(rgbFrame, box, color, 2);

            std::string classString = detection.className + ' ' + std::to_string(detection.confidence).substr(0, 4);
            cv::Size textSize = cv::getTextSize(classString, cv::FONT_HERSHEY_DUPLEX, 1, 2, 0);
            cv::Rect textBox(box.x, box.y - 40, textSize.width + 10, textSize.height + 20);

            cv::rectangle(rgbFrame, textBox, color, cv::FILLED);
            cv::putText(rgbFrame, classString, cv::Point(box.x + 5, box.y - 10), cv::FONT_HERSHEY_DUPLEX, 1, cv::Scalar(0, 0, 0), 2, 0);


            // 2. HSV 分析与目标点计算
            if (safeBox.width > 0 && safeBox.height > 0) {
                cv::Mat croppedImage = frame(safeBox);
                cv::Mat hsv, mask;
                cv::cvtColor(croppedImage, hsv, cv::COLOR_BGR2HSV);
                cv::inRange(hsv, cv::Scalar(35, 43, 46), cv::Scalar(77, 255, 255), mask);

                cv::Moments m = cv::moments(mask, true);
                if (m.m00 > 0) {
                    int cx = int(m.m10 / m.m00);
                    int cy = int(m.m01 / m.m00);

                    // 计算全局坐标并写回 output，方便下位机读取
                    detection.targetX = box.x + cx;
                    detection.targetY = box.y + cy;

                    cv::circle(rgbFrame, cv::Point(detection.targetX, detection.targetY), 5, cv::Scalar(0, 0, 255), -1);
                }
            }
    }

    // 核心数据发送给主线程存储或控制底盘
    emit sendDetections(output);

    cv::cvtColor(rgbFrame, rgbFrame, cv::COLOR_BGR2RGB);
    QImage qImg((const uchar*)rgbFrame.data, rgbFrame.cols, rgbFrame.rows, rgbFrame.step, QImage::Format_RGB888);
    emit sendResult(qImg.copy());
    qDebug() << "结束图片检测";
}

void Vision::change_showAiState(bool state){
    showAiState = state;
}

void Vision::change_showAiRect(bool state){
    showAiRect = state;
}
