#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QVariant>
#include <QTimer>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QSqlDatabase>
#include <QSqlTableModel>
#include <QThread>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <thread>

// 👉 [修改 1] 把 tcp 的头文件换成 mqtt 的头文件
#include "mqttclientmanager.h"
#include "vision.h"
#include "protocol_def.h"

// 简化后的表格监控项
struct MonitorItem {
    QString name;
    QString typeStr;
    int tableRow;
};

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void on_pushButton_connect_clicked();

    // 运动控制
    void on_pushButton_front_pressed();
    void on_pushButton_left_pressed();
    void on_pushButton_right_pressed();
    void on_pushButton_back_pressed();
    void on_pushButton_front_released();
    void on_pushButton_left_released();
    void on_pushButton_right_released();
    void on_pushButton_back_released();
    void on_verticalSlider_valueChanged(int value);

    // 👉 [修改 2] 网络与数据处理：名字改成 MQTT
    void onMqttConnectionChanged(bool isConnected, const QString &message);
    void onMqttDataReceived(uint8_t type, const QByteArray &payload); 
    void on_pushButton_enable_clicked();

    // 视觉控制
    void on_pushButton_detectImg_clicked(); 

    // 👉 [修改 3] 补全我们在 cpp 里加的防报错函数声明
    void handleVisionResults(const QString &className, float conf);

private:
    void initMonitorTable();
    void initModel();
    void initDataBase();
    void saveDetectionRecord(const QString &className, double confidence, int x = -1, int y = -1);

private:
    Ui::MainWindow *ui;

    // 👉 [修改 4] 核心模块：从 TcpClientManager 改为 MqttClientManager
    MqttClientManager *m_mqttClient;
    Vision *visionprocess;
    QThread* m_visionThread;

    // UI 与状态
    bool connectionState = false;
    bool enableState = false;
    int speed = 0;

    QGraphicsScene* m_scene;
    QGraphicsPixmapItem* m_pixmapItem;

    // 数据持久化
    QList<MonitorItem> m_monitorList;
    QSqlDatabase m_db;
    QSqlTableModel *m_logModel;
    // mainwindow.h 里加
    QTimer* m_logRefreshTimer;

    // 日志数据包
    struct DetectionLog {
        QString time;
        QString type;
        double confidence;
        int x;
        int y;
    };

    // 在 MainWindow 类中添加成员
    private:
        std::mutex m_dbMutex;
        std::queue<DetectionLog> m_dbQueue;
        std::condition_variable m_dbCond;
        std::thread m_dbThread;
        bool m_isDbRunning = false;

        // 声明后台存库函数
        void dbWriterLoop();
};
#endif // MAINWINDOW_H