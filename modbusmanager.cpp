#include "modbusmanager.h"
#include "car_modbus_protocol.h"
#include <QModbusTcpClient>
#include <QModbusDataUnit>
#include <QDebug>

ModbusManager::ModbusManager(QObject *parent)
    : QObject{parent}
{
    m_modbusClient = new QModbusTcpClient(this);

    m_reconnectTimer = new QTimer;
    m_reconnectTimer->setInterval(3000);

    connect(m_reconnectTimer, &QTimer::timeout, this,  &ModbusManager::onReconnectTimeout);

    connect(m_modbusClient, &QModbusClient::stateChanged, this, &ModbusManager::onModbusStateChanged);
    connect(m_modbusClient, &QModbusClient::errorOccurred, this, &ModbusManager::onModbusErrorOccurred);
}

//析构函数
ModbusManager::~ModbusManager()
{
}

void ModbusManager::connectToDevice(const QString &ip, int port){
    m_modbusClient->setConnectionParameter(QModbusDevice::NetworkAddressParameter, ip);
    m_modbusClient->setConnectionParameter(QModbusDevice::NetworkPortParameter, port);
    m_modbusClient->connectDevice();
}

void ModbusManager::onReconnectTimeout(){
    if(m_modbusClient->state() == QModbusDevice::ConnectedState){
        m_reconnectTimer->stop();
        return;
    }

    qDebug() << "尝试重连中...";
    // 🔥 这里直接调用 client 的 connectDevice 即可
    // 因为之前的 setConnectionParameter 已经设置过 IP 和端口了，它记住了
    m_modbusClient->connectDevice();
}

void ModbusManager::disconnectFromDevice(){
    m_modbusClient->disconnectDevice();
}

void ModbusManager::writeModbusValue(QModbusDataUnit::RegisterType type, int address, QVariant value, int serverAddress)
{
    if (!m_modbusClient){
        // 修改点1：type 放第一个参数 (为了对应头文件)
        emit writeRequestFinished(type, address, false, tr("Write failed: Modbus client not initialized"), QVariant());
        return;
    }

    QModbusDataUnit writeUnit(type, address, 1);
    if(type == QModbusDataUnit::Coils){
        writeUnit.setValue(0, value.toBool());
    }else{
        writeUnit.setValue(0, static_cast<quint16>(value.toUInt()));
    }

    if (auto *reply = m_modbusClient->sendWriteRequest(writeUnit, serverAddress)) {

        if (!reply->isFinished()) {
            // 修改点2：【关键】捕获列表里加上 type，否则下面用不了！
            connect(reply, &QModbusReply::finished, this, [this, reply, address, value, type]() {
                QString statusMessage;
                if (reply->error() == QModbusDevice::ProtocolError) {
                    statusMessage = tr("Error writing to address %1: %2 (Exception code: 0x%3)")
                    .arg(address).arg(reply->errorString())
                        .arg(reply->rawResult().exceptionCode(), -1, 16);
                    // 修改点3：type 放第一个
                    emit writeRequestFinished(type, address, false, statusMessage, QVariant());
                } else if (reply->error() != QModbusDevice::NoError) {
                    statusMessage = tr("Error writing to address %1: %2 (code: 0x%3)")
                    .arg(address).arg(reply->errorString());
                    // 修改点3：type 放第一个
                    emit writeRequestFinished(type, address, false, statusMessage, QVariant());
                } else {
                    statusMessage = tr("Address %1 was successfully written.").arg(address);
                    // 修改点3：type 放第一个
                    emit writeRequestFinished(type, address, true, statusMessage, value);
                }
                reply->deleteLater();
            });
        } else {
            reply->deleteLater();
        }
    } else {
        // 修改点4：type 放第一个
        emit writeRequestFinished(type, address, false, tr("Address %1 write failed to send: %2")
                                                            .arg(address).arg(m_modbusClient->errorString()), QVariant());
    }
}

void ModbusManager::readModbusValue(QModbusDataUnit::RegisterType type, int address, int serverAddress, quint16 count){
    if (!m_modbusClient){
        emit readError(tr("Read failed: Modbus client not initialized."));
        return;
    }

    QModbusDataUnit readUnit(type, address, count);

    if (auto *reply = m_modbusClient->sendReadRequest(readUnit, serverAddress)) {
        if (!reply->isFinished())
            connect(reply, &QModbusReply::finished, this, &ModbusManager::onReadReady);
        else
            delete reply; // broadcast replies return immediately
    } else {
        emit readError(m_modbusClient->errorString());
    }
}

void ModbusManager::onReadReady(){
    qDebug() << "onReadReady";
    auto reply = qobject_cast<QModbusReply *>(sender());
    if (!reply)
        return;
    qDebug() << reply->error();
    if (reply->error() == QModbusDevice::NoError) {
        qDebug() << "onReadReady";
        const QModbusDataUnit unit = reply->result();
        emit readDataReady(unit);
    } else if (reply->error() == QModbusDevice::ProtocolError) {
        emit readError(tr("Read response error: %1 (Mobus exception: 0x%2)").
                                 arg(reply->errorString()).
                                 arg(reply->rawResult().exceptionCode()));
    } else {
        emit readError(tr("Read response error: %1 (code: 0x%2)").
                                 arg(reply->errorString()).
                                 arg(reply->error()));
    }

    reply->deleteLater();
}

void ModbusManager::onModbusStateChanged(int state)
{
    QString message;
    bool connectionFlag;

    if (state == QModbusDevice::ConnectedState){
        qDebug() << "Connect";
        message = "Connect";
        connectionFlag = true;
        m_reconnectTimer->stop();
        m_isUserDisconnect = false;
    }else if (state == QModbusDevice::UnconnectedState){
        if(m_isUserDisconnect){
            qDebug() << "DisConnect";
            message = "DisConnect";
            connectionFlag = false;
        }else{
            qDebug() << "Connection Lost, Auto reconnecting...";
            message = "Reconnecting...";
            connectionFlag = false;

            // 启动定时器 (如果没启动的话)
            if(!m_reconnectTimer->isActive()){
                m_reconnectTimer->start();
            }
        }
    }else{
        return;
    }
    emit connectionStatusChanged(connectionFlag, message);
}

void ModbusManager::onModbusErrorOccurred(QModbusDevice::Error error){
    QString errorMessage = tr("Device error: ") + m_modbusClient->errorString();
    emit connectionStatusChanged(false, errorMessage);
}






















