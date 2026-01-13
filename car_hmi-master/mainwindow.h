#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "car_modbus_protocol.h"
#include "modbusmanager.h"
#include "vision.h"
#include <QMainWindow>
#include <QModbusDataUnit>
#include <QVariant>
#include <QTimer>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QSqlDatabase>
#include <QSqlTableModel>

struct MonitorItem {
    QString name;       // 显示的名字，比如 "速度设定"
    QModbusDataUnit::RegisterType type; // 类型：Coils 或 HoldingRegisters
    int address;        // Modbus 地址，比如 0
    int tableRow;       // 这一项在表格里的第几行 (初始化时自动填)
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
    void on_pushButton_enable_clicked();

    // --- 方向 ---
    void on_pushButton_front_pressed();
    void on_pushButton_left_pressed();
    void on_pushButton_right_pressed();
    void on_pushButton_back_pressed();
    void on_pushButton_front_released();
    void on_pushButton_left_released();
    void on_pushButton_right_released();
    void on_pushButton_back_released();

    void on_verticalSlider_valueChanged(int value);
    void onModbusConnectionChanged(bool isConnected, const QString &message);
    void onReadTimerTimeout();
    void onModbusWriteFinished(QModbusDataUnit::RegisterType type, int address, bool success, const QString &statusMessage, QVariant value);
    void onModbusDataReady(const QModbusDataUnit &data);
    void onModbusReadError(const QString &errorMessage);

    void onSimulateTimerTimeout();

    void on_checkBox_show_ai_toggled(bool checked);

    void on_checkBox_auto_read_toggled(bool checked);

    void on_pushButton_nextImg_clicked();

    void on_pushButton_detectImg_clicked();

    void on_checkBox_showAiRect_clicked(bool checked);

private:
    void handleLaserSwitchResponse();
    void handleRadiatorSwitchResponse();
    void handleCameraSwitchResponse();
    void handleStrobeSwitchResponse();
    void handleLaserFireResponse();
    void handleEnableResponse();
    void initMonitorTable();
    void initModel();
    void initDataBase();
    void saveDetectionRecord(const QString &className, double confidence, int x = -1, int y = -1);
    // 更新表格某一行数据的函数
    void updateTableValue(QModbusDataUnit::RegisterType type, int address, quint16 value);

    // void handleConnectionResponse(bool isConnected, const QString &message);

signals:
    void startDetection(cv::Mat frame); // 给子线程发任务的信号

private:
    Ui::MainWindow *ui;
    ModbusManager *carModbus;
    bool connectionState = false;
    bool enableState = false;
    QTimer *m_readTimer;
    QTimer *m_detectTimer;
    QStringList m_imageFiles; // 存文件名的列表
    int m_currentImageIndex = 0; // 当前播到第几张
    QString m_imageFolderPath;   // 文件夹路径
    Vision *visionprocess;
    QThread* m_visionThread;
    QGraphicsScene* m_scene;          // 场景
    QGraphicsPixmapItem* m_pixmapItem;// 用来显示图片的“相框”
    bool autoReadState = true;
    // 存储所有监控项的列表
    QList<MonitorItem> m_monitorList;
    QSqlDatabase m_db;
    QSqlTableModel *m_logModel;
};
#endif // MAINWINDOW_H
