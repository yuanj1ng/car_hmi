#ifndef VISION_H
#define VISION_H

#include <QObject>
#include <QImage>
#include <vector>
#include <opencv2/opencv.hpp>

// 引入多线程与队列控制所需的头文件
#include <thread>
#include <mutex>
#include <queue>
#include <condition_variable>

// ===== Linux 原生 V4L2 与系统调用头文件 =====
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include <cstring>  // 解决 memset 和 memcpy 找不到的问题

// 引入 NPU 推理引擎的头文件
#include "inference.h" 

class Vision : public QObject
{
    Q_OBJECT
public:
    explicit Vision(QObject *parent = nullptr);
    ~Vision();

public slots:
    void startLocalCamera();
    void loadYoloModel();
    void stop();

signals:
    // Qt 的跨线程信号传递非常安全，工作线程处理完后直接 emit 这两个信号即可
    void sendResult(QImage img);
    void sendDetections(std::vector<Detection> dets);

private:
    // 图像转换工具函数
    QImage cvMatToQImage(const cv::Mat& inMat);

    // 内部工作线程函数：3个打工人运行的死循环
    void workerFunction(int worker_id);
    
    // 内部读图线程函数：包工头专门负责读图，防止卡死 Qt 主界面
    void cameraLoop(); 

private:
    std::atomic<int> m_processedCount{0}; // 原子计数器
    float m_overallFps = 0.0f;            // 整体 FPS
    std::chrono::steady_clock::time_point m_fpsStartTime; 
    std::mutex m_fpsMutex;                // 专门用于时间结算的锁
    
    cv::VideoCapture m_cap;
    bool m_isRunning;
    
    // ==========================================
    // 💥 核心修改：多线程与任务调度模块
    // ==========================================
    
    // 1. 核心打工人 (绑定到 3 个 NPU 核心的实例)
    Inference* m_npuWorkers[3] = {nullptr, nullptr, nullptr}; 
    std::thread m_workerThreads[3]; // 3 个推理线程

    // 2. 读图包工头 (单独开一个线程读摄像头，绝不能在主线程 while 死循环)
    std::thread m_cameraThread; 

    // 3. 任务分发中心 (生产者-消费者队列)
    std::queue<cv::Mat> m_frameQueue;  // 待处理的图像队列
    std::mutex m_queueMutex;           // 线程锁（防止多个线程抢同一张图）
    std::condition_variable m_condition; // 唤醒机制（队列有图了就叫醒空闲的线程）
    
    bool m_stopThreads; // 控制所有线程安全退出的标志位
};

#endif // VISION_H