#ifndef VISION_H
#define VISION_H

#include <QObject>
#include <QImage>
#include <QDebug>
#include <opencv2/opencv.hpp>
#include "inference.h" // 引用你拷进来的那个推理头文件

class Vision : public QObject
{
    Q_OBJECT
public:
    explicit Vision(QObject *parent = nullptr);
    ~Vision();

public slots:
    // 对应 main 中的 Inference inf(...) 初始化
    void loadYoloModel();

    // 对应 main 中的 for 循环里处理单帧的逻辑
    void detectFrame(cv::Mat frame);

signals:
    // 替代 cv::imshow，把图发给界面显示
    void sendResult(QImage img);

private:
    // 把 Inference 做成成员变量，而不是局部变量
    // 这样模型只加载一次，不用每帧都加载
    Inference* m_inf = nullptr;
};

#endif // VISION_H
