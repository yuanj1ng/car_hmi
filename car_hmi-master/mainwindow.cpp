#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include "modbusmanager.h"
#include "vision.h"
#include <QDebug>
#include <QTimer>
#include <QMessageBox>
#include <QModbusDataUnit>
#include <QThread>
#include <QDir>
#include <QStringList>
#include <opencv2/opencv.hpp>
#include <QImage>
#include <QPixmap>
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlDatabase>
#include <QDateTime>


MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    initDataBase();
    initModel();
    qRegisterMetaType<cv::Mat>("cv::Mat");
    qRegisterMetaType<QList<QPoint>>("QList<QPoint>");
    qRegisterMetaType<std::vector<Detection>>("std::vector<Detection>");
    qRegisterMetaType<Detection>("Detection");

    carModbus = new ModbusManager;
    connect(carModbus, &ModbusManager::connectionStatusChanged, this, &MainWindow::onModbusConnectionChanged);
    m_readTimer = new QTimer(this);
    m_readTimer->setInterval(500);
    connect(m_readTimer, &QTimer::timeout, this, &MainWindow::onReadTimerTimeout);
    connect(carModbus, &ModbusManager::readDataReady, this, &MainWindow::onModbusDataReady);
    connect(carModbus, &ModbusManager::writeRequestFinished, this, &MainWindow::onModbusWriteFinished);
    ui->pushButton_enable->setEnabled(false);

    /*QString appDir = QCoreApplication::applicationDirPath();
    QString modelPath = QDir(appDir).filePath("yolov8n.onnx");
    QString imagePath = QDir(appDir).filePath("image");*/

    // 3. 初始化
    initMonitorTable();

    visionprocess = new Vision;

    m_visionThread = new QThread;

    visionprocess->moveToThread(m_visionThread);


    m_scene = new QGraphicsScene(this);         // 场景
    ui->graphicsView->setScene(m_scene);
    m_pixmapItem = new QGraphicsPixmapItem();  // 用来显示图片的“相框”
    m_scene->addItem(m_pixmapItem);

    // 4. 【关键修改】不要直接调用 loadYoloModel！
    // 而是绑定线程的 started 信号。
    // 这样，当线程一启动(start)，它会在新线程里自动去加载模型。
    connect(m_visionThread, &QThread::started, visionprocess, &Vision::loadYoloModel);

    // 5. 处理线程销毁逻辑（防止内存泄漏）
    // 当窗口关闭时，线程结束 -> 自动删除 visionprocess 对象
    connect(m_visionThread, &QThread::finished, visionprocess, &QObject::deleteLater);
    connect(m_visionThread, &QThread::finished, m_visionThread, &QObject::deleteLater);

    // 6. 接收视觉结果（把子线程处理完的图片发回给主界面显示）
    // 注意：这里是跨线程通信，Qt 会自动处理锁的问题
    connect(visionprocess, &Vision::sendResult, this, [this](QImage img){
        qDebug() << "接收成功";
        if (img.isNull()) {
            qDebug() << "❌ 错误：接收到的图片是空的！";
            return;
        }
        QPixmap pixmap = QPixmap::fromImage(img);
        m_pixmapItem->setPixmap(pixmap);

        // 2. 【关键修正】更新场景的大小
        // 如果不设置 SceneRect，fitInView 可能会迷失方向
        m_scene->setSceneRect(m_pixmapItem->boundingRect());
        ui->graphicsView->fitInView(m_pixmapItem, Qt::KeepAspectRatio);
        m_scene->update();
        ui->graphicsView->viewport()->update();
    });

    /*connect(visionprocess, &Vision::sendTarget, this, [this](QList<QPoint> targetList){
        qDebug() << "打击点接收成功";
        qDebug() << "准备发送坐标，数量：" << targetList.size();
        if(targetList.size() == 0) return;
        for(int i = 0; i < targetList.size(); i++){

            this->saveDetectionRecord("LaserPoint", 1.0, targetList[i].x(), targetList[i].y());

            carModbus->writeModbusValue(QModbusDataUnit::HoldingRegisters, Reg_LaserTargetX, targetList[i].x(), CarServerID);
            carModbus->writeModbusValue(QModbusDataUnit::HoldingRegisters, Reg_LaserTargetY, targetList[i].y(), CarServerID);
        }
    });*/

    connect(visionprocess, &Vision::sendDetections, this, [this](std::vector<Detection> dets){
        // 遍历所有检测到的物体
        for(const auto& det : dets){
            // 这里的 det.className 就是例如 "person", "car" 的真实名字
            // 这里的 det.confidence 就是例如 0.85 的真实置信度

            // 为了美观，我们把置信度保留2位小数存进去
            // (你也可以直接存 det.confidence)
            this->saveDetectionRecord(QString::fromStdString(det.className), det.confidence, det.targetX, det.targetY);
            if (det.targetX > 0 && det.targetY > 0) {
                carModbus->writeModbusValue(QModbusDataUnit::HoldingRegisters, Reg_LaserTargetX, det.targetX, CarServerID);
                carModbus->writeModbusValue(QModbusDataUnit::HoldingRegisters, Reg_LaserTargetY, det.targetY, CarServerID);
            }
        }
    });

}



MainWindow::~MainWindow()
{
    if (m_visionThread && m_visionThread->isRunning()) {
        m_visionThread->quit(); // 或者 requestInterruption();
        m_visionThread->wait(); // 【关键】主线程在这里死等，直到子线程真正彻底结束后，才继续往下走
    }

    delete ui;
}


void MainWindow::initMonitorTable()
{
    // 1. 设置表头宽度
    ui->tableWidget_monitor->setColumnWidth(0, 150); // 名称宽一点
    ui->tableWidget_monitor->setColumnWidth(1, 80);
    ui->tableWidget_monitor->setColumnWidth(2, 60);
    ui->tableWidget_monitor->horizontalHeader()->setStretchLastSection(true); // 最后一列填满

    // 2. === 配置你的设备清单 (这里是你的核心配置) ===
    // 格式：{名称, 类型, 地址, 占位符}
    m_monitorList.append({"电源使能",       QModbusDataUnit::Coils,            Addr_PowerEnable, 0});
    m_monitorList.append({"前进指令",       QModbusDataUnit::Coils,            Addr_DirectionForward, 0});
    m_monitorList.append({"激光开关",       QModbusDataUnit::Coils,            Addr_LaserSwitch, 0});

    m_monitorList.append({"速度设定值",     QModbusDataUnit::HoldingRegisters, Reg_SpeedSet, 0});
    m_monitorList.append({"当前实际速度",   QModbusDataUnit::HoldingRegisters, Reg_CurrentSpeed, 0});
    m_monitorList.append({"激光 X 坐标",    QModbusDataUnit::HoldingRegisters, Reg_LaserTargetX, 0});
    m_monitorList.append({"激光 Y 坐标",    QModbusDataUnit::HoldingRegisters, Reg_LaserTargetY, 0});

    // 3. 把清单填进表格
    ui->tableWidget_monitor->setRowCount(m_monitorList.size()); // 设定总行数

    for(int i = 0; i < m_monitorList.size(); i++) {
        // 记录这一项在第几行，方便后面更新
        m_monitorList[i].tableRow = i;

        // 填名称
        ui->tableWidget_monitor->setItem(i, 0, new QTableWidgetItem(m_monitorList[i].name));

        // 填类型 (翻译一下给人看)
        QString typeStr = (m_monitorList[i].type == QModbusDataUnit::Coils) ? "线圈(Bool)" : "寄存器(UInt16)";
        ui->tableWidget_monitor->setItem(i, 1, new QTableWidgetItem(typeStr));

        // 填地址
        ui->tableWidget_monitor->setItem(i, 2, new QTableWidgetItem(QString::number(m_monitorList[i].address)));

        // 填初始值 (默认 "-")
        ui->tableWidget_monitor->setItem(i, 3, new QTableWidgetItem("-"));
    }
}

void MainWindow::initModel()
{
    // 1. 实例化 Model
    // 参数2: m_db 是数据库连接对象，告诉它去哪个数据库找
    m_logModel = new QSqlTableModel(this, m_db);

    // 2. 绑定数据库表
    // 告诉它：“你要显示 detection_logs 这张表的数据”
    m_logModel->setTable("detection_logs");

    // 3. 设置编辑策略
    // OnManualSubmit: 用户修改后需要点保存才生效（防止误触）
    // 这里的场景其实主要是看，设不设都行
    m_logModel->setEditStrategy(QSqlTableModel::OnManualSubmit);

    // 4. 【关键】查询数据
    // 这句话相当于执行了 "SELECT * FROM detection_logs"
    m_logModel->select();

    // 5. 设置表头别名 (给人看的中文)
    // 0: id, 1: time, 2: type, 3: confidence, 4: x, 5: y
    m_logModel->setHeaderData(0, Qt::Horizontal, "ID");
    m_logModel->setHeaderData(1, Qt::Horizontal, "时间");
    m_logModel->setHeaderData(2, Qt::Horizontal, "类别");
    m_logModel->setHeaderData(3, Qt::Horizontal, "置信度");
    m_logModel->setHeaderData(4, Qt::Horizontal, "X坐标");
    m_logModel->setHeaderData(5, Qt::Horizontal, "Y坐标");

    // 6. 把 Model 放到 View 里展示
    // 这一步之后，界面上立马就会出现数据！
    ui->tableView_logs->setModel(m_logModel);

    // 7. 一些美化 (可选)
    // 隐藏 ID 列（通常用户不关心 ID）
    // ui->tableView_logs->hideColumn(0);
    // 列宽自适应
    ui->tableView_logs->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
}



void MainWindow::initDataBase()
{
    if(QSqlDatabase::contains("qt_sql_default_connection")){
        m_db = QSqlDatabase::database("qt_sql_default_connection");
    }else{
        m_db = QSqlDatabase::addDatabase("QSQLITE");
    }

    QString dbPath = QCoreApplication::applicationDirPath() + "/car_data.db";
    m_db.setDatabaseName(dbPath);
    if(!m_db.open()){
        qDebug() << "数据库打开失败：" << m_db.lastError().text();
        return;
    }
    qDebug() << "数据库连接成功，路径：" << dbPath;

    QSqlQuery query;
    QString createSql = "CREATE TABLE IF NOT EXISTS detection_logs ("
                        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                        "time TEXT, "
                        "type TEXT, "
                        "confidence REAL,"
                        "target_x INTEGER, "
                        "target_y INTEGER)";

    if(!query.exec(createSql)){
        qDebug() << "建表失败：" << query.lastError().text();
    } else {
        qDebug() << "数据表检测/创建成功";
    }
}

void MainWindow::saveDetectionRecord(const QString &className, double confidence, int x, int y)
{
    if(!m_db.isOpen()) return;

    QSqlQuery query;
    // 使用 prepare + bindValue 是防止 SQL 注入、处理特殊字符的标准写法
    query.prepare("INSERT INTO detection_logs (time, type, confidence, target_x, target_y) "
                  "VALUES (:time, :type, :confidence, :x, :y)");

    // 获取当前时间字符串
    QString currentTime = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    double cleanVal = QString::number(confidence, 'f', 2).toDouble();
    query.bindValue(":time", currentTime);
    query.bindValue(":type", className);
    query.bindValue(":confidence", cleanVal);
    query.bindValue(":x", x); // 绑定 x
    query.bindValue(":y", y); // 绑定 y

    if(!query.exec()){
        qDebug() << "插入数据失败：" << query.lastError().text();
    } else {
        // qDebug() << "成功记录一条数据：" << className;
        if (m_logModel) {
            m_logModel->select();

            // 炫技：自动滚动到底部，让用户看到最新的一行
            ui->tableView_logs->scrollToBottom();
        }
    }
}


void MainWindow::on_pushButton_connect_clicked()
{
    ui->pushButton_connect->setEnabled(false);
    if(!connectionState){
        ui->label_connection->setText("Connecting...");
        carModbus->connectToDevice(CarModbus_IP, CarModbus_Port);
    }else{
        ui->label_connection->setText("Disconnecting...");
        carModbus->disconnectFromDevice();
    }
}

void MainWindow::on_pushButton_enable_clicked()
{
    if(connectionState){
        if(enableState){
            carModbus->writeModbusValue(QModbusDataUnit::Coils, Addr_PowerEnable, 0, CarServerID);
            m_detectTimer->stop();
            m_visionThread->quit();
        }else{
            carModbus->writeModbusValue(QModbusDataUnit::Coils, Addr_PowerEnable, 1, CarServerID);
            m_visionThread->start();
            QString appPath = QCoreApplication::applicationDirPath();

            // 拼接路径
            m_imageFolderPath = appPath + "/3dparty/images/val";

            QDir dir(m_imageFolderPath);
            QStringList filters;
            filters << "*.jpg" << "*.png" << "*.jpeg";
            m_imageFiles = dir.entryList(filters, QDir::Files | QDir::NoDotAndDotDot);
            qDebug() << "Loaded images count:" << m_imageFiles.size();
            m_detectTimer = new QTimer(this);
            m_detectTimer->setInterval(2000);
            connect(m_detectTimer, &QTimer::timeout, this, &MainWindow::onSimulateTimerTimeout);
            if (!m_imageFiles.isEmpty()) {
                m_detectTimer->start();
            }

            // 把 MainWindow 的发令枪(startDetection) 连接到 Vision 的执行函数(detectFrame)
            connect(this, &MainWindow::startDetection, visionprocess, &Vision::detectFrame);
            ui->pushButton_nextImg->setEnabled(false);
        }
    }else{
        qDebug() << "Not connected, please connect first";
    }
}

void MainWindow::on_pushButton_front_pressed()
{
    carModbus->writeModbusValue(QModbusDataUnit::Coils, Addr_DirectionForward, 1, CarServerID);
}

void MainWindow::on_pushButton_left_pressed()
{
    carModbus->writeModbusValue(QModbusDataUnit::Coils, Addr_DirectionLeft, 1, CarServerID);
}

void MainWindow::on_pushButton_right_pressed()
{
    carModbus->writeModbusValue(QModbusDataUnit::Coils, Addr_DirectionRight, 1, CarServerID);
}

void MainWindow::on_pushButton_back_pressed()
{
    carModbus->writeModbusValue(QModbusDataUnit::Coils, Addr_DirectionBackward, 1, CarServerID);
}

void MainWindow::on_verticalSlider_valueChanged(int value)
{
    qDebug() << value;
    carModbus->writeModbusValue(QModbusDataUnit::HoldingRegisters, Reg_SpeedSet, ui->verticalSlider->value(), CarServerID);
}

void MainWindow::onModbusConnectionChanged(bool isConnected, const QString &message){
    // qDebug() << isConnected;
    // qDebug() << message;
    if(isConnected){
        connectionState = true;
        ui->pushButton_enable->setEnabled(true);
        ui->label_connection->setText("Connected");
        ui->pushButton_connect->setText("Disconnect");
        ui->pushButton_connect->setEnabled(true);
        m_readTimer->start();
    }else{
        ui->label_connection->setText("Disconnected");
        ui->pushButton_connect->setText("Connect");
        connectionState = false;
        ui->pushButton_enable->setEnabled(false);
        ui->pushButton_connect->setEnabled(true);
        m_readTimer->stop();
    }
}

void MainWindow::on_pushButton_front_released()
{
    carModbus->writeModbusValue(QModbusDataUnit::Coils, Addr_DirectionForward, 0, CarServerID);
}


void MainWindow::on_pushButton_left_released()
{
    carModbus->writeModbusValue(QModbusDataUnit::Coils, Addr_DirectionLeft, 0, CarServerID);
}


void MainWindow::on_pushButton_right_released()
{
    carModbus->writeModbusValue(QModbusDataUnit::Coils, Addr_DirectionRight, 0, CarServerID);
}


void MainWindow::on_pushButton_back_released()
{
    carModbus->writeModbusValue(QModbusDataUnit::Coils, Addr_DirectionBackward, 0, CarServerID);
}

void MainWindow::onReadTimerTimeout()
{
    if (connectionState) {
        carModbus->readModbusValue(QModbusDataUnit::HoldingRegisters, 0, CarServerID, 20);
        carModbus->readModbusValue(QModbusDataUnit::Coils, 0, CarServerID, 5);
    }
}

//后续再补充
void MainWindow::onModbusWriteFinished(QModbusDataUnit::RegisterType type, int address, bool success, const QString &statusMessage, QVariant value){
    if(!success){
        QMessageBox::StandardButton button = QMessageBox::information(nullptr, "警告", statusMessage,
                                                      QMessageBox::Yes | QMessageBox::No, QMessageBox::NoButton);
    }
    qDebug() << "Write Success -> Type:" << type << " Addr:" << address << " Val:" << value;

    // === 核心修复：先判断是哪种类型的数据 ===

    // 1. 如果是线圈 (开关)
    if (type == QModbusDataUnit::Coils)
    {
        switch(address){
        case Addr_PowerEnable:
            qDebug() << "PowerEnable";
            handleEnableResponse();
            break;
        case Addr_CameraSwitch:
            handleCameraSwitchResponse();
            break;
        case Addr_LaserSwitch:
            handleLaserSwitchResponse();
            break;
        case Addr_RadiatorSwitch:{
            handleRadiatorSwitchResponse();
            break;
        }
        case Addr_StrobeSwitch:{
            handleStrobeSwitchResponse();
            break;
        }
        case Addr_LaserFire:{
            handleLaserFireResponse();
            break;
        }
        case Addr_DirectionForward:
        case Addr_DirectionLeft:
        case Addr_DirectionRight:
        case Addr_DirectionBackward:
            break;
        }
    }
}

void MainWindow::handleEnableResponse(){
    qDebug() << enableState;
    if(enableState){
        enableState = false;
        ui->pushButton_connect->setEnabled(true);
        ui->label_enable->setText("Power off");
        ui->pushButton_enable->setText("Power On");
    }else{
        enableState = true;
        ui->pushButton_connect->setEnabled(false);
        ui->label_enable->setText("Power on");
        ui->pushButton_enable->setText("Power Off");
    }
    qDebug() << enableState;
}

//后续补充，ui的内容
void MainWindow::handleCameraSwitchResponse(){

}

void MainWindow::handleLaserSwitchResponse(){

}

void MainWindow::onModbusDataReady(const QModbusDataUnit &data)
{
    // 获取这包数据的类型、起始地址、数量
    auto type = data.registerType();
    int startAddress = data.startAddress();
    int count = data.valueCount();

    // 遍历收到的每一个数据
    for (int i = 0; i < count; i++) {
        int currentAddr = startAddress + i; // 算出当前数据的绝对地址
        quint16 value = data.value(i);      // 取出值

        // === 更新表格 ===
        // 这是一个通用的查找更新函数
        updateTableValue(type, currentAddr, value);

        // === 这里保留你之前的业务逻辑 (比如更新滑块、Label) ===
        if (type == QModbusDataUnit::HoldingRegisters) {
            if (currentAddr == Reg_SpeedSet) {
                // 如果用户正在拖动滑块，最好别更新，否则会抢焦点（可选）
                // ui->verticalSlider->setValue(value);
                ui->label_speed->setText(QString::number(value));
            }
        }
    }
}

// 辅助函数：去 m_monitorList 里找，看看谁对应这个地址，然后更新那一格
void MainWindow::updateTableValue(QModbusDataUnit::RegisterType type, int address, quint16 value)
{
    // 遍历我们的监控列表
    for (const auto &item : m_monitorList) {
        // 如果类型匹配 且 地址匹配
        if (item.type == type && item.address == address) {

            // 找到了！item.tableRow 就是它在表格里的行号
            QTableWidgetItem *cell = ui->tableWidget_monitor->item(item.tableRow, 3);
            if (!cell) return;

            QString displayStr;

            // 如果是线圈，显示 "ON/OFF" 比显示 "1/0" 更专业
            if (type == QModbusDataUnit::Coils) {
                displayStr = (value != 0) ? "ON (开)" : "OFF (关)";

                // 炫技：如果是 ON，把字变绿；OFF 变灰
                if (value != 0) cell->setForeground(Qt::green);
                else cell->setForeground(Qt::gray);
            }
            else {
                // 如果是寄存器，直接显示数字
                displayStr = QString::number(value);
                cell->setForeground(Qt::black);
            }

            cell->setText(displayStr);
            return; // 更新完就溜，节省性能
        }
    }
}

void MainWindow::handleRadiatorSwitchResponse(){

}

void MainWindow::handleStrobeSwitchResponse(){

}

void MainWindow::handleLaserFireResponse(){

}

void MainWindow::onModbusReadError(const QString &errorMessage){
    qDebug() << errorMessage;
}

void MainWindow::onSimulateTimerTimeout()
{
    // 安全检查：如果没图，就别跑了
    if (m_imageFiles.isEmpty()) return;

    // 1. 获取当前文件名
    QString fileName = m_imageFiles.at(m_currentImageIndex);
    QString fullPath = m_imageFolderPath + "/" + fileName;

    // 2. 用 OpenCV 读取图片
    // 注意：OpenCV 读路径最好不要含中文，或者需要转码
    cv::Mat frame = cv::imread(fullPath.toStdString());

    if (frame.empty()) {
        qDebug() << "Error: Could not read image" << fullPath;
    } else {
        // 3. 【关键】通过信号把图片发给子线程的 Vision 对象
        // 这一步是线程安全的
        emit startDetection(frame);
        qDebug() << "开始处理";
    }

    // 4. 索引 +1，准备下一张
    m_currentImageIndex++;

    // 5. 实现“循环播放”：如果播到最后一张，就回到 0
    if (m_currentImageIndex >= m_imageFiles.size()) {
        m_currentImageIndex = 0;
    }
}


void MainWindow::on_checkBox_show_ai_toggled(bool checked)
{
    qDebug() << "即将修改为" << checked;
    visionprocess->change_showAiState(checked);
}


void MainWindow::on_checkBox_auto_read_toggled(bool checked)
{
    autoReadState = checked;
    ui->pushButton_nextImg->setEnabled(!checked);
    if(autoReadState){
        m_detectTimer->start();
        onSimulateTimerTimeout();
    }else{
        m_detectTimer->stop();
    }
}


void MainWindow::on_pushButton_nextImg_clicked()
{
    if (!autoReadState) {
        onSimulateTimerTimeout();
        qDebug() << "Manually switched to next img";
    }
}



// 辅助函数：把 QPixmap 转成 cv::Mat
cv::Mat QPixmapToCvMat(const QPixmap &pixmap) {
    if (pixmap.isNull()) {
        return cv::Mat();
    }

    // 1. QPixmap -> QImage
    // 这一步把数据从显存拉回内存
    QImage image = pixmap.toImage();

    // 2. 格式统一 (非常关键！)
    // OpenCV 默认是 BGR 顺序，Qt 默认可能是 RGB 或 ARGB
    // 我们强制转成 RGB888 (3通道)，这样跟 OpenCV 的 CV_8UC3 对应
    image = image.convertToFormat(QImage::Format_RGB888);

    // 3. QImage -> cv::Mat
    // 注意：这里用的是 image.bits() 和 image.bytesPerLine()，处理了内存对齐
    cv::Mat tmp(image.height(), image.width(), CV_8UC3, image.bits(), image.bytesPerLine());

    // 4. 深拷贝 + 颜色转换 (RGB -> BGR)
    // 因为 YOLO/OpenCV 训练的模型通常需要 BGR 格式
    cv::Mat result;
    cv::cvtColor(tmp, result, cv::COLOR_RGB2BGR);

    return result; // 返回深拷贝的数据，安全
}


void MainWindow::on_pushButton_detectImg_clicked()
{
    // 0. 判空，防止崩溃
    if (!m_pixmapItem || m_pixmapItem->pixmap().isNull()) {
        return;
    }

    // 1. 转换
    cv::Mat img = QPixmapToCvMat(m_pixmapItem->pixmap());

    // 2. 发送信号 (把 Mat 传给工作线程)
    // 确保你在信号定义里写的是 void startDetection(cv::Mat img);
    emit startDetection(img);
}





void MainWindow::on_checkBox_showAiRect_clicked(bool checked)
{
    qDebug() << "即将修改为" << checked;
    visionprocess->change_showAiRect(checked);
}

