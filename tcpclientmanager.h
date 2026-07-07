#ifndef TCPCLIENTMANAGER_H
#define TCPCLIENTMANAGER_H

#include "protocol_def.h"
#include <QObject>
#include <QTcpSocket>
#include <QVariant>
#include <QTimer>
#include <QByteArray>

class TcpClientManager : public QObject
{
    Q_OBJECT
public:
    explicit TcpClientManager(QObject *parent = nullptr);

public slots:
    void connectToDevice(const QString &ip, int port);
    void disconnectFromDevice();
    void sendControl(bool led, bool buzzer, int mode);
    void sendMove(float vx, float vy, float vz);

private slots:
    void onTcpcClientStateChanged(int state);
    void sendData(const QByteArray &data);
    void onReconnectTimeout();
    void sendTarget(int x, int y);

    // --- 新增：接收与心跳机制 ---
    void onReadyRead();      // 处理接收到的数据
    void sendHeartbeat();    // 定时发送心跳包

signals:
    void connectionStatusChanged(bool isConnected, const QString &statusMessage);
    // --- 新增：收到下位机数据的信号 ---
    void dataReceived(uint8_t type, const QByteArray &payload);

private:
    void processIncomingPacket(uint8_t type, const QByteArray &payload);

    QTcpSocket *m_tcpSocket = nullptr;
    QTimer *m_reconnectTimer;

    // --- 新增：接收与心跳变量 ---
    QTimer *m_heartbeatTimer;
    int m_heartbeatRetryCount = 0;
    QByteArray m_buffer;     // 接收缓冲区，用于解决粘包

    bool m_isUserDisconnect = false;
    QString m_carIp;
    int m_carPort;
};

#endif // TCPCLIENTMANAGER_H
