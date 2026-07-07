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
    , connectionState(false) // 增加初始化状态
    , enableState(false)     // 增加初始化状态
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
    // 3. 网络通信模块升级：替换为 MQTT 架构
    // ==========================================
    m_mqttClient = new MqttClientManager(this);
    connect(m_mqttClient, &MqttClientManager::connectionStatusChanged, this, &MainWindow::onMqttConnectionChanged);
    connect(m_mqttClient, &MqttClientManager::dataReceived, this, &MainWindow::onMqttDataReceived);

    // ==========================================
    // 4. 视觉推理模块初始化 (多线程 + NPU 加速版)
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

    // 👉 极其清爽的自适应渲染逻辑
    connect(visionprocess, &Vision::sendResult, this, [this](QImage img){
        if (img.isNull()) return;
        
        // 1. 把新的画面贴上去
        m_pixmapItem->setPixmap(QPixmap::fromImage(img));
        
        // 2. 告诉场景（Scene），现在的有效物理边界就是这张图的大小
        m_scene->setSceneRect(m_pixmapItem->boundingRect());
        
        // 3. 💥 核心魔法：每一帧都强制缩放贴合当前的 graphicsView！
        // 无论你怎么拉伸窗口，下一帧（30毫秒后）马上就会按照新窗口的大小完美铺满。
        ui->graphicsView->fitInView(m_pixmapItem, Qt::KeepAspectRatio);
    });

    // 接收推理数据：存数据库并控制下位机
    connect(visionprocess, &Vision::sendDetections, this, [this](std::vector<Detection> dets){
        for(const auto& det : dets){
            // 存入数据库日志
            this->saveDetectionRecord(QString::fromStdString(det.className), det.confidence, det.targetX, det.targetY);

            // 如果成功计算出目标坐标(如草心)，下发给小车
            if (connectionState && det.targetX > 0 && det.targetY > 0) {
                // 如果需要 MQTT 自动下发坐标，可以在这里调用
                // m_mqttClient->sendTarget(det.targetX, det.targetY);
            }
        }
    });


    // 启动视觉线程
    m_visionThread->start();

    m_logRefreshTimer = new QTimer(this);
    connect(m_logRefreshTimer, &QTimer::timeout, this, [this](){
        if (m_logModel) {
            m_logModel->select(); // 刷新数据源
            
            //qDebug() << "[UI线程] 🔄 表格定时刷新，当前数据库总行数：" << m_logModel->rowCount();
            
            // 如果真的有数据，让它滚到底部
            if (m_logModel->rowCount() > 0) {
                ui->tableView_logs->scrollToBottom();
            }
        }
    });
    m_logRefreshTimer->start(1000);
}

MainWindow::~MainWindow()
{
    // 👉 [关键优化 2] 优雅退出流程，彻底告别关闭软件时的卡死
    
    // 1. 断开 MQTT 连接
    if (m_mqttClient) {
        m_mqttClient->disconnectFromBroker();
    }

    // 2. 打破视觉线程的死循环
    if (visionprocess) {
        visionprocess->stop(); 
    }

    // 3. 安全回收线程
    if (m_visionThread && m_visionThread->isRunning()) {
        m_visionThread->quit();
        m_visionThread->wait(); // 阻塞等待线程安全退出
    }
    
    if(visionprocess) delete visionprocess;
    if(m_visionThread) delete m_visionThread;
    
    // 👉 优化：加锁修改标志位，完美契合后台的 condition_variable
    {
        std::lock_guard<std::mutex> lock(m_dbMutex);
        m_isDbRunning = false;
    }
    m_dbCond.notify_all(); // 叫醒线程，让它把没存完的尾巴存完
    
    if (m_dbThread.joinable()) {
        m_dbThread.join();
    }

    delete ui;
}


// 当你用鼠标拖拽窗口大小的时候，Qt 会自动疯狂调用这个函数
void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event); // 先让父类处理默认操作

    // 如果画面 item 存在且里面有图，就强制缩放填充
    if (m_pixmapItem && !m_pixmapItem->pixmap().isNull()) {
        ui->graphicsView->fitInView(m_pixmapItem, Qt::KeepAspectRatio);
    }
}


// ==========================================
// 初始化与数据库 (保持原样，完美兼容你的 UI)
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
    m_isDbRunning = true;
    m_dbThread = std::thread(&MainWindow::dbWriterLoop, this);
}

void MainWindow::initDataBase()
{
    if(QSqlDatabase::contains("qt_sql_default_connection")){
        m_db = QSqlDatabase::database("qt_sql_default_connection");
    }else{
        m_db = QSqlDatabase::addDatabase("QSQLITE");
    }

    QString dbPath = QCoreApplication::applicationDirPath() + "/data.db";
    m_db.setDatabaseName(dbPath);
    if(!m_db.open()){
        qDebug() << "数据库打开失败：" << m_db.lastError().text();
        return;
    }

    m_db.exec("PRAGMA journal_mode=WAL;");

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
    QString currentTime = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    
    // 瞬间推入队列，不产生任何 I/O
    {
        std::lock_guard<std::mutex> lock(m_dbMutex);
        m_dbQueue.push({currentTime, className, confidence, x, y});

        qDebug() << "[主线程] 📥 推入队列:" << className << "| 队列长度:" << m_dbQueue.size();
    }
    
    m_dbCond.notify_one(); // 叫醒数据库线程干活
}


void MainWindow::dbWriterLoop()
{
    QSqlDatabase threadDb = QSqlDatabase::addDatabase("QSQLITE", "AsyncLogConnection");
    QString dbPath = QCoreApplication::applicationDirPath() + "/data.db";
    threadDb.setDatabaseName(dbPath);
    
    if (!threadDb.open()) {
        qDebug() << "[DB线程] ❌ 后台数据库打开失败！";
        return;
    }

    QVariantList times, types, confs, xs, ys;
    const int BATCH_SIZE = 50; 

    while (m_isDbRunning) {
        bool shouldFlush = false;
        {
            std::unique_lock<std::mutex> lock(m_dbMutex);
            m_dbCond.wait(lock, [this]{ return !m_dbQueue.empty() || !m_isDbRunning; });

            if (!m_isDbRunning && m_dbQueue.empty()) break;

            // 疯狂掏空当前队列里的所有数据
            while (!m_dbQueue.empty()) {
                DetectionLog log = m_dbQueue.front();
                m_dbQueue.pop();
                times << log.time;
                types << log.type;
                confs << QString::number(log.confidence, 'f', 2).toDouble();
                xs << log.x;
                ys << log.y;

                // 如果瞬间凑够了 50 条，先去存一波
                if (times.size() >= BATCH_SIZE) {
                    shouldFlush = true;
                    break; 
                }
            }
            
            // 👉 智能冲刷逻辑：虽然没凑够50条，但当前没人塞新数据了（队列空了），赶紧顺手存掉！
            if (m_dbQueue.empty() && times.size() > 0) {
                shouldFlush = true;
            }
        }

        // 触发落盘
        if (shouldFlush || (!m_isDbRunning && times.size() > 0)) {
            qDebug() << "[DB线程] 🚀 触发落盘！本次写入数据量：" << times.size();
            
            threadDb.transaction(); // 加锁硬盘
            QSqlQuery query(threadDb);
            
            // 👉 魔法 1：坚决不用问号 (?)，改用明确的命名占位符！
            query.prepare("INSERT INTO detection_logs (time, type, confidence, target_x, target_y) "
                          "VALUES (:time, :type, :confidence, :x, :y)");
            
            // 👉 魔法 2：改用 bindValue，指名道姓地绑定列表
            query.bindValue(":time", times);
            query.bindValue(":type", types);
            query.bindValue(":confidence", confs);
            query.bindValue(":x", xs);
            query.bindValue(":y", ys);

            if (!query.execBatch()) {
                qDebug() << "[DB线程] ⚠️ execBatch 失败:" << query.lastError().text() << "，启动极速降级逐条写入...";
                
                // 👉 魔法 3：无敌降级保险！只要在 transaction 内部，for 循环单条插入速度同样起飞
                for (int i = 0; i < times.size(); ++i) {
                    QSqlQuery singleQuery(threadDb);
                    singleQuery.prepare("INSERT INTO detection_logs (time, type, confidence, target_x, target_y) "
                                        "VALUES (?, ?, ?, ?, ?)");
                    singleQuery.addBindValue(times[i]);
                    singleQuery.addBindValue(types[i]);
                    singleQuery.addBindValue(confs[i]);
                    singleQuery.addBindValue(xs[i]);
                    singleQuery.addBindValue(ys[i]);
                    
                    if (!singleQuery.exec()) {
                        qDebug() << "[DB线程] ❌ 终极报错，单条写入也失败:" << singleQuery.lastError().text();
                    }
                }
                qDebug() << "[DB线程] ✅ 降级写入完成！";
            } else {
                qDebug() << "[DB线程] ✅ 批量写入成功！";
            }
            
            threadDb.commit(); // 统一提交，一次性真正写入硬盘

            // 清空缓存，准备迎接下一批
            times.clear(); types.clear(); confs.clear(); xs.clear(); ys.clear();
        }
    }
    
    threadDb.close();
    QSqlDatabase::removeDatabase("AsyncLogConnection");
    qDebug() << "[DB线程] 🛑 后台存库线程安全退出。";
}

// ==========================================
// 网络连接与数据处理 (已升级为 MQTT)
// ==========================================
void MainWindow::on_pushButton_connect_clicked()
{
    ui->pushButton_connect->setEnabled(false);
    if(!connectionState){
        QString Car_IP = ui->lineEdit_IP->text();
        int Car_Port = ui->lineEdit_port->text().toInt();
        m_mqttClient->connectToBroker(Car_IP, Car_Port);
    }else{
        m_mqttClient->disconnectFromBroker();
    }
}

void MainWindow::onMqttConnectionChanged(bool isConnected, const QString &message)
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

void MainWindow::onMqttDataReceived(uint8_t type, const QByteArray &payload)
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
    // 既然用物理摄像头，就不需要从 UI 读取 URL 了，直接触发底层就行
    ui->pushButton_detectImg->setEnabled(false); // 点了一次就置灰，防止重复开线程
    
    // 调用 Vision 里的本地摄像头启动函数 (我们下一步去写这个函数)
    QMetaObject::invokeMethod(visionprocess, "startLocalCamera", Qt::QueuedConnection);

    qDebug() << ">>> 正在通过 GStreamer 启动 IMX415 本地摄像头...";
}

void MainWindow::on_pushButton_enable_clicked()
{
    if (!connectionState) return;

    enableState = !enableState;

    if (enableState) {
        ui->pushButton_enable->setText("Power Off");
        ui->pushButton_enable->setStyleSheet("background-color: green; color: white;");
        m_mqttClient->sendControl(true, false, 1);
        qDebug() << ">>> 下发指令：系统已使能，激光开";
    } else {
        ui->pushButton_enable->setText("Power On");
        ui->pushButton_enable->setStyleSheet("background-color: red; color: white;");
        m_mqttClient->sendControl(false, false, 0);
        qDebug() << ">>> 下发指令：系统已关闭，激光关";
    }
}

// ==========================================
// 核心修复：精准匹配 UI 的运动控制 (MQTT 版)
// ==========================================
void MainWindow::on_verticalSlider_valueChanged(int value)
{
    qDebug() << "滑块当前值：" << value;
}

void MainWindow::on_pushButton_front_pressed()
{
    if (!connectionState) return;
    float current_speed = ui->verticalSlider->value() * 2.0f;
    m_mqttClient->sendMove(current_speed, 0, 0);
    qDebug() << ">>> 前进，速度 =" << current_speed;
}

void MainWindow::on_pushButton_back_pressed()
{
    if (!connectionState) return;
    float current_speed = ui->verticalSlider->value() * 2.0f;
    m_mqttClient->sendMove(-current_speed, 0, 0);
    qDebug() << ">>> 后退，速度 =" << -current_speed;
}

void MainWindow::on_pushButton_left_pressed()
{
    if (!connectionState) return;
    float current_speed = ui->verticalSlider->value() * 2.0f;
    m_mqttClient->sendMove(0, -current_speed, 0);
    qDebug() << ">>> 左移，速度 =" << -current_speed;
}

void MainWindow::on_pushButton_right_pressed()
{
    if (!connectionState) return;
    float current_speed = ui->verticalSlider->value() * 2.0f;
    m_mqttClient->sendMove(0, current_speed, 0);
    qDebug() << ">>> 右移，速度 =" << current_speed;
}

// 松开任意按钮，立即刹车 (下发全 0 速度)
void MainWindow::on_pushButton_front_released() { if(connectionState) m_mqttClient->sendMove(0, 0, 0); }
void MainWindow::on_pushButton_back_released()  { if(connectionState) m_mqttClient->sendMove(0, 0, 0); }
void MainWindow::on_pushButton_left_released()  { if(connectionState) m_mqttClient->sendMove(0, 0, 0); }
void MainWindow::on_pushButton_right_released() { if(connectionState) m_mqttClient->sendMove(0, 0, 0); }

// ==========================================
// 防链接器报错的“占位符”槽函数
// ==========================================
void MainWindow::handleVisionResults(const QString &className, float conf)
{
    // 这个函数是为了防止 Qt 的 MOC 编译器因为在头文件里声明了但没实现而报错
}
