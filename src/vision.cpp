#include "vision.h"
#include <QDebug>
#include <QThread>
#include <QCoreApplication>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <atomic>

// 全局原子变量，用于多线程安全地计算整体 FPS
static std::atomic<int> g_frameCount{0};
static std::atomic<int> g_currentFps{0};
static auto g_fpsStartTime = std::chrono::steady_clock::now();

Vision::Vision(QObject *parent) 
    : QObject(parent)
    , m_isRunning(false)
    , m_stopThreads(false)
{
    // 初始化时确保指针为空
    for (int i = 0; i < 3; ++i) {
        m_npuWorkers[i] = nullptr;
    }
}

Vision::~Vision()
{
    stop();
    // 释放 3 个 NPU 工作实例
    for (int i = 0; i < 3; ++i) {
        if (m_npuWorkers[i]) {
            delete m_npuWorkers[i];
            m_npuWorkers[i] = nullptr;
        }
    }
}

void Vision::stop()
{
    qDebug() << ">>> 准备安全停止所有视觉线程...";
    m_isRunning = false;
    m_stopThreads = true;
    
    // 唤醒所有正在沉睡等待任务的打工人，让他们下班
    m_condition.notify_all();

    // 回收读图包工头线程
    if (m_cameraThread.joinable()) {
        m_cameraThread.join();
    }

    // 回收 3 个打工人线程
    for (int i = 0; i < 3; ++i) {
        if (m_workerThreads[i].joinable()) {
            m_workerThreads[i].join();
        }
    }
    qDebug() << "🛑 视觉模块已彻底安全关闭。";
}

// 辅助函数：动态寻找 rkisp_mainpath 节点
std::string findIspMainpathNode() {
    std::string basePath = "/sys/class/video4linux/";
    for (const auto& entry : std::filesystem::directory_iterator(basePath)) {
        std::string nameFilePath = entry.path().string() + "/name";
        std::ifstream nameFile(nameFilePath);
        if (nameFile.is_open()) {
            std::string name;
            std::getline(nameFile, name);
            if (name.find("rkisp_mainpath") != std::string::npos) {
                return "/dev/" + entry.path().filename().string();
            }
        }
    }
    return "";
}

void Vision::loadYoloModel()
{
    m_fpsStartTime = std::chrono::steady_clock::now();
    // ==========================================
    // 1. 初始化 3 个独立的 AI 模型实例
    // ==========================================
    std::string modelPath = (QCoreApplication::applicationDirPath() + "/models/yolov8s_int8.rknn").toStdString();
    QString classesPath = QCoreApplication::applicationDirPath() + "/models/classes.txt";

    qDebug() << ">>> 开始加载 RKNN 模型并绑定物理核心...";
    

    // 分别绑定到 Core 0, Core 1, Core 2 (这里需要你的 inference.h 构造函数已支持传入 core_mask)
    m_npuWorkers[0] = new Inference(modelPath, cv::Size(640, 640), classesPath, RKNN_NPU_CORE_0);
    m_npuWorkers[1] = new Inference(modelPath, cv::Size(640, 640), classesPath, RKNN_NPU_CORE_1);
    m_npuWorkers[2] = new Inference(modelPath, cv::Size(640, 640), classesPath, RKNN_NPU_CORE_2);

    // ==========================================
    // 2. 启动 3 个打工人线程
    // ==========================================
    m_stopThreads = false;
    for (int i = 0; i < 3; ++i) {
        m_workerThreads[i] = std::thread(&Vision::workerFunction, this, i);
    }

    qDebug() << "✅ 3 个 NPU 推理线程已就绪，嗷嗷待哺！";
}

void Vision::startLocalCamera()
{
    // 不要在这里写 while 死循环！只负责启动独立线程
    if (m_isRunning) return;
    
    m_isRunning = true;
    m_cameraThread = std::thread(&Vision::cameraLoop, this);
}


// ==========================================
// 💥 包工头线程：只负责疯狂读图，塞进队列
// ==========================================
/*void Vision::cameraLoop()
{
    qDebug() << ">>> [包工头] 摄像头读取线程启动...";

    std::string isp_node = findIspMainpathNode();
    if (isp_node.empty()) isp_node = "/dev/video11"; 

    std::string pipeline = "v4l2src device=" + isp_node + " ! video/x-raw, width=1280, height=720, framerate=60/1 ! videoconvert ! appsink";
    m_cap.open(pipeline, cv::CAP_GSTREAMER);

    if (!m_cap.isOpened()) {
        qDebug() << "【致命错误】摄像头打开失败！";
        m_isRunning = false;
        return;
    }

    // 预热摄像头
    cv::Mat frame;
    for (int i = 0; i < 5; ++i) { m_cap >> frame; QThread::msleep(30); }

    while (m_isRunning) {
        m_cap >> frame;
        if (frame.empty()) {
            QThread::msleep(10);
            continue;
        }

        // --- 核心机制：塞入队列 ---
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            // ⚠️ 防爆栈策略：如果处理不过来，丢弃最老的帧，保证最新画面的实时性
            if (m_frameQueue.size() >= 3) {
                m_frameQueue.pop(); 
            }
            m_frameQueue.push(frame.clone()); 
        }
        // 队列有图了，喊醒一个空闲的打工人起来干活
        m_condition.notify_one(); 
    }

    m_cap.release();
    qDebug() << ">>> [包工头] 摄像头读取线程安全退出。";
}*/

void Vision::cameraLoop()
{
    qDebug() << ">>> 配置摄像头媒体链路(60fps 全像素模式)...";
    system("media-ctl -d /dev/media0 --set-v4l2 '\"m00_b_imx415 7-001a\":0[fmt:SGBRG10_1X10/3864x2192@10000/600000]'");

    std::string cameraNode = findIspMainpathNode();
    if (cameraNode.empty()) {
        qDebug() << "【致命错误】未在系统中找到 rkisp_mainpath 节点！";
        m_isRunning = false;
        return;
    }

    // 1. 打开设备
    int fd = open(cameraNode.c_str(), O_RDWR);
    if (fd < 0) {
        qDebug() << "【致命错误】打开摄像头设备失败！";
        m_isRunning = false;
        return;
    }

    // 2. 设置格式（multiplanar NV12，800x600，和之前保持一致）
    v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    fmt.fmt.pix_mp.width = 800;
    fmt.fmt.pix_mp.height = 600;
    fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12M;
    fmt.fmt.pix_mp.field = V4L2_FIELD_NONE;
    fmt.fmt.pix_mp.num_planes = 2;

    if (ioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
        qDebug() << "【致命错误】设置摄像头格式失败！";
        close(fd);
        m_isRunning = false;
        return;
    }

    // 3. 设置帧率60fps
    v4l2_streamparm parm;
    memset(&parm, 0, sizeof(parm));
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    parm.parm.capture.timeperframe.numerator = 1;
    parm.parm.capture.timeperframe.denominator = 60;
    ioctl(fd, VIDIOC_S_PARM, &parm);

    // 4. 申请3个mmap缓冲区
    const int NUM_BUFFERS = 3;
    v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = NUM_BUFFERS;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    req.memory = V4L2_MEMORY_MMAP;
    if (ioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
        qDebug() << "【致命错误】申请缓冲区失败！";
        close(fd);
        m_isRunning = false;
        return;
    }

    // 5. mmap每个buffer的每个plane
    struct PlaneBuf { void* start; size_t length; };
    std::vector<std::vector<PlaneBuf>> buffers(req.count);

    for (unsigned i = 0; i < req.count; i++) {
        v4l2_buffer buf;
        v4l2_plane planes[2];
        memset(&buf, 0, sizeof(buf));
        memset(planes, 0, sizeof(planes));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        buf.m.planes = planes;
        buf.length = 2;

        if (ioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) {
            qDebug() << "【致命错误】查询缓冲区失败！";
            close(fd);
            m_isRunning = false;
            return;
        }

        buffers[i].resize(2);
        for (int p = 0; p < 2; p++) {
            buffers[i][p].length = planes[p].length;
            buffers[i][p].start = mmap(NULL, planes[p].length, PROT_READ | PROT_WRITE,
                                        MAP_SHARED, fd, planes[p].m.mem_offset);
            if (buffers[i][p].start == MAP_FAILED) {
                qDebug() << "【致命错误】mmap失败！";
                close(fd);
                m_isRunning = false;
                return;
            }
        }
        ioctl(fd, VIDIOC_QBUF, &buf);
    }

    // 6. 开始streaming
    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (ioctl(fd, VIDIOC_STREAMON, &type) < 0) {
        qDebug() << "【致命错误】启动streaming失败！";
        close(fd);
        m_isRunning = false;
        return;
    }

    qDebug() << "✅ 原生V4L2 60fps 采集初始化成功，开始捕获视频流...";

    // 7. 主循环：DQBUF取帧 -> 转换 -> 塞队列 -> QBUF还回去
    while (m_isRunning) {
        v4l2_buffer buf;
        v4l2_plane planes[2];
        memset(&buf, 0, sizeof(buf));
        memset(planes, 0, sizeof(planes));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.m.planes = planes;
        buf.length = 2;

        if (ioctl(fd, VIDIOC_DQBUF, &buf) < 0) {
            QThread::msleep(2);
            continue;
        }

        // 把两个plane(Y和UV)包装成一个连续NV12格式给OpenCV处理
        // 800x600的Y平面 + 800x300的UV平面，用一块连续内存拼起来
        cv::Mat nv12_frame(600 + 600 / 2, 800, CV_8UC1);
        memcpy(nv12_frame.data, buffers[buf.index][0].start, planes[0].bytesused);
        memcpy(nv12_frame.data + planes[0].bytesused, buffers[buf.index][1].start, planes[1].bytesused);

        cv::Mat bgr_frame;
        cv::cvtColor(nv12_frame, bgr_frame, cv::COLOR_YUV2BGR_NV12);

        // 立刻把buffer还给驱动，让它继续采集下一帧
        ioctl(fd, VIDIOC_QBUF, &buf);

        // 塞入队列
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            if (m_frameQueue.size() >= 2) {
                m_frameQueue.pop();
            }
            m_frameQueue.push(bgr_frame);
        }
        m_condition.notify_one();
    }

    // 8. 清理
    ioctl(fd, VIDIOC_STREAMOFF, &type);
    for (auto& planeBufs : buffers) {
        for (auto& pb : planeBufs) {
            munmap(pb.start, pb.length);
        }
    }
    close(fd);
    qDebug() << ">>> [包工头] 摄像头读取线程安全退出。";
}

// ==========================================
// 💥 打工人线程：死循环等图 -> 推理 -> 渲染
// ==========================================
void Vision::workerFunction(int worker_id)
{
    qDebug() << ">>> [打工人" << worker_id << "] 线程已启动，绑定核心:" << worker_id;

    while (!m_stopThreads) {
        cv::Mat frame;
        
        // 1. 从队列中抢任务
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            // 如果没活干且没喊停，就挂起休眠，绝不空转浪费 CPU
            m_condition.wait(lock, [this]{ return !m_frameQueue.empty() || m_stopThreads; });
            
            if (m_stopThreads && m_frameQueue.empty()) break; // 彻底下班
            
            frame = m_frameQueue.front();
            m_frameQueue.pop();
        }

        if (frame.empty()) continue;

        // 2. 🧠 开始 NPU 专属物理核心推理
        auto t_inf_start = std::chrono::steady_clock::now();
        std::vector<Detection> dets = m_npuWorkers[worker_id]->runInference(frame);
        auto t_inf_end = std::chrono::steady_clock::now();
        double inferenceTime = std::chrono::duration<double, std::milli>(t_inf_end - t_inf_start).count();

        // 3. 🎨 在当前线程独立完成渲染 (互不干扰，性能最高)
        auto t_draw_start = std::chrono::steady_clock::now();
        for (const auto& det : dets) {
            cv::rectangle(frame, det.box, cv::Scalar(0, 255, 0), 2);
            cv::circle(frame, cv::Point(det.targetX, det.targetY), 5, cv::Scalar(0, 0, 255), -1);
            QString labelText = QString::fromStdString(det.className) + " " + QString::number(det.confidence, 'f', 2);
            cv::putText(frame, labelText.toStdString(), cv::Point(det.box.x, det.box.y - 10), 
                        cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0, 255, 255), 2);
        }

        // ==========================================
        // 💥 修复：线程安全的 FPS 结算中心
        // ==========================================
        m_processedCount++; // 原子操作：无锁化打卡，表示完成了一帧

        {
            // 上锁保护时间计算，确保只有1个线程在更新 FPS 
            std::lock_guard<std::mutex> lock(m_fpsMutex);
            auto currentTime = std::chrono::steady_clock::now();
            double elapsed = std::chrono::duration<double>(currentTime - m_fpsStartTime).count();
            
            if (elapsed >= 1.0) { 
                int count = m_processedCount.exchange(0); // 拿走总数并同时清零
                m_overallFps = count / elapsed;
                m_fpsStartTime = currentTime;
            }
        }

        // --- 绘制 HUD 面板 ---
        auto t_draw_end = std::chrono::steady_clock::now();
        double drawTime = std::chrono::duration<double, std::milli>(t_draw_end - t_draw_start).count();

        /*qDebug() << "[耗时拆解] worker" << worker_id 
          << "推理:" << inferenceTime << "ms"
          << "绘制+转换:" << drawTime << "ms";
        */
        
        cv::Rect hudRect(10, 10, 260, 100);
        // 安全裁剪，防止 frame 尺寸意外小于 HUD 区域导致越界
        hudRect = hudRect & cv::Rect(0, 0, frame.cols, frame.rows);

        cv::Mat roi = frame(hudRect);
        cv::Mat blackBox(roi.size(), roi.type(), cv::Scalar(0, 0, 0));
        cv::addWeighted(blackBox, 0.5, roi, 0.5, 0, roi); // 直接写回 roi，因为 roi 是 frame 的视图(不拷贝)

        // 读取最新的 m_overallFps 并打印
        std::string textFps = "NPU FPS   : " + std::to_string(static_cast<int>(m_overallFps));
        std::string textInf = "Worker " + std::to_string(worker_id) + "  : " + std::to_string(static_cast<int>(inferenceTime)) + " ms";
        std::string textDrw = "Draw Time : " + std::to_string(static_cast<int>(drawTime)) + " ms";

        cv::putText(frame, textFps, cv::Point(20, 35), cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 0), 2);
        cv::putText(frame, textInf, cv::Point(20, 65), cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0, 255, 0), 2);
        cv::putText(frame, textDrw, cv::Point(20, 95), cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0, 255, 0), 2);

        // 4. 将完成的图和数据抛给主线程 UI
        if (!dets.empty()) {
            emit sendDetections(dets);
        }
        
        QImage qImg = cvMatToQImage(frame);
        emit sendResult(qImg);
    }
    qDebug() << ">>> [打工人" << worker_id << "] 线程安全退出。";
}

// ==========================================
// 核心工具：OpenCV Mat 零拷贝转 QImage
// ==========================================
QImage Vision::cvMatToQImage(const cv::Mat& inMat)
{
    switch (inMat.type()) {
        case CV_8UC3: { 
            QImage image((const uchar*)inMat.data, inMat.cols, inMat.rows, inMat.step, QImage::Format_RGB888);
            return image.rgbSwapped(); 
        }
        case CV_8UC1: { 
            QImage image((const uchar*)inMat.data, inMat.cols, inMat.rows, inMat.step, QImage::Format_Grayscale8);
            return image.copy(); 
        }
        case CV_8UC4: { 
            QImage image((const uchar*)inMat.data, inMat.cols, inMat.rows, inMat.step, QImage::Format_ARGB32);
            return image.copy(); 
        }
        default:
            qDebug() << "未知的 cv::Mat 格式，转换 QImage 失败！";
            break;
    }
    return QImage();
}