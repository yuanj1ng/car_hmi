#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "vision.h"
#include <QThread>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QTimer>
#include <QDir>
#include <QStringList>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

signals:
    void signalRunInference(cv::Mat frame);

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void showImage(QImage img);
    void loadFolderImages();
    void onFrameTimerTimeout();

private:
    Ui::MainWindow *ui;
    Vision *vision;
    QThread *workerThread;
    QGraphicsScene *scene;          // 场景
    QGraphicsPixmapItem *imageItem; // 用来显示那一帧图的“相框”
    QTimer *simTimer;          // 定时器
    QStringList imageFileList; // 存文件夹里所有图片的文件名
    QString folderPath;        // 文件夹路径
    int currentFrameIndex;     // 当前播到第几张了
};
#endif // MAINWINDOW_H
