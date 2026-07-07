#ifndef VISION_H
#define VISION_H

#include <QObject>
#include <QImage>
#include <QDebug>
#include <QString>
#include <QTimer>
#include <opencv2/opencv.hpp>
#include "inference.h"

class Vision : public QObject
{
    Q_OBJECT
public:
    explicit Vision(QObject *parent = nullptr);
    ~Vision();

public slots:
    void loadYoloModel();
    void detectFrame(cv::Mat frame);

    // --- 新增：手机摄像头网络流入口 ---
    void startPhoneCamera(const QString &url);

    void change_showAiState(bool state);
    void change_showAiRect(bool state);

signals:
    void sendResult(QImage img);
    void sendDetections(std::vector<Detection> dets);

private:
    Inference* m_inf = nullptr;
    bool showAiState = true;
    bool showAiRect = true;

    // --- 新增：自主抓图相关组件 ---
    cv::VideoCapture m_cap;
    QTimer* m_captureTimer = nullptr;
};

#endif // VISION_H
