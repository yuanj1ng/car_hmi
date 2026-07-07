#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include <QDebug>
#include <QMessageBox>
#include <QDir>
#include <QDateTime>
#include <QSqlQuery>
#include <QSqlError>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , speed(0)
{
    ui->setupUi(this);

    // 1. 初始化数据库和表格
    initDataBase();
    initModel();
    initMonitorTable();

    // 2. 注册自定义类型 (用于跨线程信号槽)
    qRegisterMetaType<cv::Mat>("cv::Mat");
    qRegisterMetaType<std::vector<Detection>>("std::vector<Detection>");
    qRegisterMetaType<uint8_t>("uint8_t");

    // UI 初始状态
    ui->pushButton_connect->setStyleSheet("background-color: red; color: white;");
    ui->pushButton_enable->setEnabled(false);

    // ==========================================
    // 3. 网络通信模块初始化 (TCP)
    // ==========================================
    m_tcpClient = new TcpClientManager();
    connect(m_tcpClient, &TcpClientManager::connectionStatusChanged, this, &MainWindow::onTcpConnectionChanged);
    connect(m_tcpClient, &TcpClientManager::dataReceived, this, &MainWindow::onCarDataReceived);

    // ==========================================
    // 4. 视觉推理模块初始化 (多线程)
    // ==========================================
    m_scene = new QGraphicsScene(this);
    ui->graphicsView->setScene(m_scene);
    m_pixmapItem = new QGraphicsPixmapItem();
    m_scene->addItem(m_pixmapItem);

    visionprocess = new Vision;
    m_visionThread = new QThread;
    visionprocess->moveToThread(m_visionThread);

    // 启动线程时自动加载模型
    connect(m_visionThread, &QThread::started, visionprocess, &Vision::loadYoloModel);

    // 接收处理好的画面并渲染
    connect(visionprocess, &Vision::sendResult, this, [this](QImage img){
        if (img.isNull()) return;
        m_pixmapItem->setPixmap(QPixmap::fromImage(img));
        m_scene->setSceneRect(m_pixmapItem->boundingRect());
        ui->graphicsView->fitInView(m_pixmapItem, Qt::KeepAspectRatio);
    });

    // 接收推理数据：存数据库并控制下位机
    connect(visionprocess, &Vision::sendDetections, this, [this](std::vector<Detection> dets){
        for(const auto& det : dets){
            // 存入数据库日志
            this->saveDetectionRecord(QString::fromStdString(det.className), det.confidence, det.targetX, det.targetY);

            // 如果成功计算出目标坐标(如草心)，下发给小车
            if (det.targetX > 0 && det.targetY > 0) {
                // m_tcpClient->sendTarget(det.targetX, det.targetY);
            }
        }
    });

    // 启动视觉线程
    m_visionThread->start();
}

MainWindow::~MainWindow()
{
    if (m_visionThread && m_visionThread->isRunning()) {
        m_visionThread->quit();
        m_visionThread->wait(); // 阻塞等待线程安全退出
    }
    if(visionprocess) delete visionprocess;
    if(m_visionThread) delete m_visionThread;
    delete ui;
}

// ==========================================
// 初始化与数据库
// ==========================================
void MainWindow::initMonitorTable()
{
    ui->tableWidget_monitor->setColumnWidth(0, 150);
    ui->tableWidget_monitor->setColumnWidth(1, 100);
    ui->tableWidget_monitor->setColumnWidth(2, 60);
    ui->tableWidget_monitor->horizontalHeader()->setStretchLastSection(true);

    m_monitorList.append({"系统模式", "状态", 0});
    m_monitorList.append({"激光器状态", "开关", 1});

    ui->tableWidget_monitor->setRowCount(m_monitorList.size());

    for(int i = 0; i < m_monitorList.size(); i++) {
        m_monitorList[i].tableRow = i;
        ui->tableWidget_monitor->setItem(i, 0, new QTableWidgetItem(m_monitorList[i].name));
        ui->tableWidget_monitor->setItem(i, 1, new QTableWidgetItem(m_monitorList[i].typeStr));
        ui->tableWidget_monitor->setItem(i, 2, new QTableWidgetItem("-"));
        ui->tableWidget_monitor->setItem(i, 3, new QTableWidgetItem("-"));
    }
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

    QSqlQuery query;
    QString createSql = "CREATE TABLE IF NOT EXISTS detection_logs ("
                        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                        "time TEXT, "
                        "type TEXT, "
                        "confidence REAL,"
                        "target_x INTEGER, "
                        "target_y INTEGER)";
    query.exec(createSql);
}

void MainWindow::initModel()
{
    m_logModel = new QSqlTableModel(this, m_db);
    m_logModel->setTable("detection_logs");
    m_logModel->setEditStrategy(QSqlTableModel::OnManualSubmit);
    m_logModel->select();

    m_logModel->setHeaderData(0, Qt::Horizontal, "ID");
    m_logModel->setHeaderData(1, Qt::Horizontal, "时间");
    m_logModel->setHeaderData(2, Qt::Horizontal, "类别");
    m_logModel->setHeaderData(3, Qt::Horizontal, "置信度");
    m_logModel->setHeaderData(4, Qt::Horizontal, "X坐标");
    m_logModel->setHeaderData(5, Qt::Horizontal, "Y坐标");

    ui->tableView_logs->setModel(m_logModel);
    ui->tableView_logs->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
}

void MainWindow::saveDetectionRecord(const QString &className, double confidence, int x, int y)
{
    if(!m_db.isOpen()) return;

    QSqlQuery query;
    query.prepare("INSERT INTO detection_logs (time, type, confidence, target_x, target_y) "
                  "VALUES (:time, :type, :confidence, :x, :y)");

    QString currentTime = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    query.bindValue(":time", currentTime);
    query.bindValue(":type", className);
    query.bindValue(":confidence", QString::number(confidence, 'f', 2).toDouble());
    query.bindValue(":x", x);
    query.bindValue(":y", y);

    if(query.exec() && m_logModel){
        m_logModel->select();
        ui->tableView_logs->scrollToBottom();
    }
}

// ==========================================
// 网络连接与数据处理
// ==========================================
void MainWindow::on_pushButton_connect_clicked()
{
    ui->pushButton_connect->setEnabled(false);
    if(!connectionState){
        QString CarTcp_IP = ui->lineEdit_IP->text();
        int CarTcp_Port = ui->lineEdit_port->text().toInt();
        m_tcpClient->connectToDevice(CarTcp_IP, CarTcp_Port);
    }else{
        m_tcpClient->disconnectFromDevice();
    }
}

void MainWindow::onTcpConnectionChanged(bool isConnected, const QString &message)
{
    connectionState = isConnected;
    ui->pushButton_enable->setEnabled(isConnected);

    if(isConnected){
        ui->pushButton_connect->setStyleSheet("background-color: green; color: white;");
        ui->pushButton_connect->setText("Disconnect");
    }else{
        ui->pushButton_connect->setStyleSheet("background-color: red; color: white;");
        ui->pushButton_connect->setText("Connect");
    }
    ui->pushButton_connect->setEnabled(true);
}

void MainWindow::onCarDataReceived(uint8_t type, const QByteArray &payload)
{
    if (type == CMD_CONTROL && payload.size() == sizeof(ControlPayload)) {
        ControlPayload* status = (ControlPayload*)payload.data();

        QTableWidgetItem *modeCell = ui->tableWidget_monitor->item(0, 3);
        if(modeCell) modeCell->setText(status->mode == 1 ? "自动模式" : "手动模式");

        QTableWidgetItem *laserCell = ui->tableWidget_monitor->item(1, 3);
        if(laserCell) {
            laserCell->setText(status->led_switch ? "ON" : "OFF");
            laserCell->setForeground(status->led_switch ? Qt::green : Qt::gray);
        }
    }
}

// ==========================================
// 视觉流控制与使能
// ==========================================
void MainWindow::on_pushButton_detectImg_clicked()
{
    // 1. 从刚才新建的 UI 输入框里读取摄像头地址
    QString cameraUrl = ui->lineEdit_cameraUrl->text();

    // 2. 加个防呆保护：如果用户什么都没填，直接弹窗提示，不往下执行
    if (cameraUrl.isEmpty()) {
        QMessageBox::warning(this, "警告", "请先输入手机摄像头的视频流地址！");
        return;
    }

    // 3. 安全地跨线程调用 Vision 的方法，把地址传过去
    QMetaObject::invokeMethod(visionprocess, "startPhoneCamera",
                              Qt::QueuedConnection,
                              Q_ARG(QString, cameraUrl));

    // 4. (可选) 给个调试提示
    qDebug() << ">>> 正在尝试连接摄像头视频流：" << cameraUrl;
}

void MainWindow::on_pushButton_enable_clicked()
{
    if (!connectionState) return;

    enableState = !enableState;

    if (enableState) {
        ui->pushButton_enable->setText("Power Off");
        ui->pushButton_enable->setStyleSheet("background-color: green; color: white;");
        m_tcpClient->sendControl(true, false, 1);
        qDebug() << ">>> 下发指令：系统已使能，激光开";
    } else {
        ui->pushButton_enable->setText("Power On");
        ui->pushButton_enable->setStyleSheet("background-color: red; color: white;");
        m_tcpClient->sendControl(false, false, 0);
        qDebug() << ">>> 下发指令：系统已关闭，激光关";
    }
}

// ==========================================
// 核心修复：精准匹配 UI 的运动控制
// ==========================================
void MainWindow::on_verticalSlider_valueChanged(int value)
{
    // 滑块变化时打印一下，方便调试
    qDebug() << "滑块当前值：" << value;
}

// 1. 前进
void MainWindow::on_pushButton_front_pressed()
{
    if (!connectionState) return;
    // 直接放大倍数转成整数，假设滑块是 50，发过去就是 100 的 PWM 速度
    int16_t speed_pwm = ui->verticalSlider->value() * 2;
    m_tcpClient->sendMove(speed_pwm, 0, 0);
    qDebug() << ">>> 前进，PWM速度 =" << speed_pwm;
}

// 2. 后退
void MainWindow::on_pushButton_back_pressed()
{
    if (!connectionState) return;
    int16_t speed_pwm = ui->verticalSlider->value() * 2;
    m_tcpClient->sendMove(-speed_pwm, 0, 0);
    qDebug() << ">>> 后退，PWM速度 =" << -speed_pwm;
}

// 3. 左移
void MainWindow::on_pushButton_left_pressed()
{
    if (!connectionState) return;
    int16_t speed_pwm = ui->verticalSlider->value() * 2;
    m_tcpClient->sendMove(0, -speed_pwm, 0);
    qDebug() << ">>> 左移，PWM速度 =" << -speed_pwm;
}

// 4. 右移
void MainWindow::on_pushButton_right_pressed()
{
    if (!connectionState) return;
    int16_t speed_pwm = ui->verticalSlider->value() * 2;
    m_tcpClient->sendMove(0, speed_pwm, 0);
    qDebug() << ">>> 右移，PWM速度 =" << speed_pwm;
}

// 5. 松开任意按钮，立即刹车
void MainWindow::on_pushButton_front_released() { if(connectionState) m_tcpClient->sendMove(0, 0, 0); }
void MainWindow::on_pushButton_back_released()  { if(connectionState) m_tcpClient->sendMove(0, 0, 0); }
void MainWindow::on_pushButton_left_released()  { if(connectionState) m_tcpClient->sendMove(0, 0, 0); }
void MainWindow::on_pushButton_right_released() { if(connectionState) m_tcpClient->sendMove(0, 0, 0); }
