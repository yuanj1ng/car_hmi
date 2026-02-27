#ifndef MODBUSMANAGER_H
#define MODBUSMANAGER_H

#include <QObject>
#include <QModbusDevice>
#include <QModbusClient>
#include <QModbusDataUnit>
#include <QVariant>
#include <QTimer>

class QModbusClient;
class QModbusReply;

class ModbusManager : public QObject
{
    Q_OBJECT

public:
    explicit ModbusManager(QObject *parent = nullptr);
    ~ModbusManager();

private:
    QModbusDataUnit readRequest() const;
    QModbusDataUnit writeRequest() const;

public slots:
    void connectToDevice(const QString &ip, int port);
    void disconnectFromDevice();
    void writeModbusValue(QModbusDataUnit::RegisterType type, int address, QVariant value, int serverAddress);
    void readModbusValue(QModbusDataUnit::RegisterType type, int address, int serverAddress, quint16 count = 1);
    void onReconnectTimeout();

signals:
    void connectionStatusChanged(bool isConnected, const QString &statusMessage);
    void writeRequestFinished(QModbusDataUnit::RegisterType type, int address, bool success,const QString &status, QVariant value);
    void readDataReady(const QModbusDataUnit &data);
    void readError(const QString &errorMessage);

private slots:
    void onReadReady();
    void onModbusStateChanged(int state);
    void onModbusErrorOccurred(QModbusDevice::Error error);

private:
    QModbusClient *m_modbusClient = nullptr;
    QModbusDevice::State m_lastBroadcastedState;
    QTimer *m_reconnectTimer;       // 重连定时器
    bool m_isUserDisconnect = false; // 标志位：是否为用户主动断开
};

#endif // MODBUSMANAGER_H
