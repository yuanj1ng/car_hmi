#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include "vision.h"


MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    simTimer = new QTimer(this);
    connect(simTimer, &QTimer::timeout, this, &MainWindow::onFrameTimerTimeout);

    // 2. 加载文件夹 (这里为了演示，我直接写死路径，你也可以用 QFileDialog 选)
    // 假设你的图片都在 D:/test_images/ 里面
    folderPath = "C:/Users/25820/Documents/yolo/3rdparty/val2017";
    loadFolderImages();

    // 3. 开始播放 (33毫秒刷一次，大约 30 FPS)
    if (!imageFileList.isEmpty()) {
        simTimer->start(50);
    }

    qRegisterMetaType<cv::Mat>("cv::Mat");

    scene = new QGraphicsScene(this);           // 1. 创建场景
    imageItem = new QGraphicsPixmapItem();      // 2. 创建图片图元

    scene->addItem(imageItem);                  // 3. 把图片放入场景
    ui->display->setScene(scene);               // 4. 把场景放入 UI 里的 View

    vision = new Vision();
    workerThread = new QThread(this);
    vision->moveToThread(workerThread);

    connect(this, &MainWindow::signalRunInference, vision, &Vision::detectFrame);
    connect(vision, &Vision::sendResult, this, &MainWindow::showImage);

    workerThread->start();

    QMetaObject::invokeMethod(vision, "loadYoloModel");
}

MainWindow::~MainWindow()
{
    // 1. 请求线程停止
    workerThread->quit();

    // 2. 等待线程真正结束 (阻塞主线程直到子线程干完手里的活)
    workerThread->wait();

    // 3. 此时再删除 UI
    delete ui;
}

void MainWindow::showImage(QImage img)
{
    QPixmap pixmap = QPixmap::fromImage(img);

    // 2. 【核心修改】不要直接操作 ui->display，而是去更新那个 imageItem
    // 这样比每次 scene->clear() 再 add 效率高得多，不会闪烁
    imageItem->setPixmap(pixmap);

    // 3. 自动适应窗口大小 (相当于之前的 scaled)
    // 这一步会让图片自动铺满整个控件，保持比例
    ui->display->fitInView(imageItem, Qt::KeepAspectRatio);
}

void MainWindow::loadFolderImages()
{
    QDir dir(folderPath);

    // 只看 jpg, png, bmp 格式
    QStringList filters;
    filters << "*.jpg" << "*.png" << "*.bmp" << "*.jpeg";

    // 获取文件列表 (按名称排序，保证播放顺序是对的)
    imageFileList = dir.entryList(filters, QDir::Files, QDir::Name);

    currentFrameIndex = 0; // 重置索引

    qDebug() << "共加载了" << imageFileList.size() << "张图片";
}

void MainWindow::onFrameTimerTimeout()
{
    // 安全检查：如果列表为空，啥也别干
    if (imageFileList.isEmpty()) return;

    // 1. 拼凑完整的图片路径
    // imageFileList[i] 只有文件名，必须加上文件夹路径
    QString fullPath = folderPath + "/" + imageFileList[currentFrameIndex];

    // 2. 用 OpenCV 读取
    // 【注意】这里必须转成 std::string。
    // 如果路径里有中文，建议用 toLocal8Bit().constData()，但最好还是全英文路径
    cv::Mat frame = cv::imread(fullPath.toStdString());

    if (!frame.empty()) {
        // 3. 发送给 YOLO (就像摄像头采集到的一样)
        emit signalRunInference(frame);
    } else {
        qDebug() << "图片读取失败：" << fullPath;
    }

    // 4. 索引 +1，准备下一张
    currentFrameIndex++;

    // 5. 循环播放：如果播到最后一张，就回到第一张
    if (currentFrameIndex >= imageFileList.size()) {
        currentFrameIndex = 0;
    }
}
