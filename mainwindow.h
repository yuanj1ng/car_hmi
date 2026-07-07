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

#include "tcpclientmanager.h"
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

    // 网络与数据处理
    void onTcpConnectionChanged(bool isConnected, const QString &message);
    void onCarDataReceived(uint8_t type, const QByteArray &payload); // 接收小车数据
    void on_pushButton_enable_clicked();

    // 视觉控制
    void on_pushButton_detectImg_clicked(); // 复用此按钮作为“启动摄像头”

private:
    void initMonitorTable();
    void initModel();
    void initDataBase();
    void saveDetectionRecord(const QString &className, double confidence, int x = -1, int y = -1);

private:
    Ui::MainWindow *ui;

    // 核心模块
    TcpClientManager *m_tcpClient;
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
};
#endif // MAINWINDOW_H
