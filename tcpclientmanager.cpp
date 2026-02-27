#include "tcpclientmanager.h"
#include <QTcpSocket>

TcpClientManager::TcpClientManager(QObject *parent)
    : QObject{parent}
{
    m_tcpSocket = new QTcpSocket(this);
    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setInterval(3000);

    m_heartbeatTimer = new QTimer(this);
    m_heartbeatTimer->setInterval(2000); // 每2秒发一次心跳

    connect(m_reconnectTimer, &QTimer::timeout, this,  &TcpClientManager::onReconnectTimeout);
    connect(m_tcpSocket, &QTcpSocket::stateChanged, this, &TcpClientManager::onTcpcClientStateChanged);

    // 绑定接收数据的信号
    connect(m_tcpSocket, &QTcpSocket::readyRead, this, &TcpClientManager::onReadyRead);
    // 绑定心跳定时器
    connect(m_heartbeatTimer, &QTimer::timeout, this, &TcpClientManager::sendHeartbeat);

    connect(m_tcpSocket, &QTcpSocket::errorOccurred, this, [=](QAbstractSocket::SocketError){
        qDebug() << "Socket Error:" << m_tcpSocket->errorString();
    });
}

void TcpClientManager::connectToDevice(const QString &ip, int port)
{
    if (m_tcpSocket->state() == QAbstractSocket::ConnectedState) {
        return;
    }
    m_carIp = ip;
    m_carPort = port;
    m_isUserDisconnect = false; // 标志为非主动断开

    qDebug() << "正在连接:" << m_carIp << ":" << m_carPort;
    m_tcpSocket->connectToHost(m_carIp, m_carPort);
}

void TcpClientManager::disconnectFromDevice()
{
    m_isUserDisconnect = true; // 标志为主动断开，防止触发自动重连
    m_tcpSocket->disconnectFromHost();
}

void TcpClientManager::sendData(const QByteArray &data)
{
    if(m_tcpSocket->state() == QAbstractSocket::ConnectedState){
        m_tcpSocket->write(data);
    }else{
        qDebug() << "未连接，无法发送";
    }
}

void TcpClientManager::onTcpcClientStateChanged(int state)
{
    QString message;
    bool connectionFlag;

    if (state == QAbstractSocket::ConnectedState){
        qDebug() << "Connect";
        message = "Connect";
        connectionFlag = true;

        m_reconnectTimer->stop(); // 连接成功，停止重连定时器
        m_heartbeatRetryCount = 0;
        m_heartbeatTimer->start(); // 连接成功，启动心跳
        m_buffer.clear();          // 清空旧数据
    }
    else if (state == QAbstractSocket::UnconnectedState) {
        m_heartbeatTimer->stop(); // 断开时停止发心跳

        if(m_isUserDisconnect){
            qDebug() << "DisConnect (User)";
            message = "DisConnect";
            connectionFlag = false;
        }else{
            qDebug() << "Connection Lost, Auto reconnecting...";
            message = "Reconnecting...";
            connectionFlag = false;

            if(!m_reconnectTimer->isActive()){
                m_reconnectTimer->start();
            }
        }
    } else {
        return;
    }
    emit connectionStatusChanged(connectionFlag, message);
}

void TcpClientManager::onReconnectTimeout()
{
    if(m_tcpSocket->state() == QAbstractSocket::ConnectedState){
        m_reconnectTimer->stop();
        return;
    }
    qDebug() << "尝试重连中..." << m_carIp << ":" << m_carPort;
    m_tcpSocket->connectToHost(m_carIp, m_carPort);
}

// ==========================================
// 核心新增：数据接收与解析 (解决粘包问题)
// ==========================================
void TcpClientManager::onReadyRead()
{
    m_buffer.append(m_tcpSocket->readAll());

    // 当缓冲区数据大于最小包长度 (包头大小 + 包尾1字节)
    while (m_buffer.size() >= (int)(sizeof(FrameHeader) + 1))
    {
        if ((uint8_t)m_buffer[0] != FRAME_HEAD) {
            m_buffer.remove(0, 1); // 找包头
            continue;
        }

        FrameHeader* header = reinterpret_cast<FrameHeader*>(m_buffer.data());
        int fullPackLen = sizeof(FrameHeader) + header->len + 1;

        if (m_buffer.size() < fullPackLen) {
            break; // 数据不够一整包，等下次数据到来
        }

        if ((uint8_t)m_buffer[fullPackLen - 1] == FRAME_TAIL) {
            // 提取载荷并处理
            QByteArray payload = m_buffer.mid(sizeof(FrameHeader), header->len);
            processIncomingPacket(header->type, payload);
            m_buffer.remove(0, fullPackLen);
        } else {
            m_buffer.remove(0, 1); // 尾部不对，丢弃头继续找
        }
    }
}

void TcpClientManager::processIncomingPacket(uint8_t type, const QByteArray &payload)
{
    switch(type) {
    case CMD_HEARTBEAT:
        m_heartbeatRetryCount = 0; // 收到心跳回应，计数清零
        break;
    default:
        // 其他数据抛出给 MainWindow 去处理（比如传感器数据）
        emit dataReceived(type, payload);
        break;
    }
}

// ==========================================
// 核心新增：心跳包发送
// ==========================================
void TcpClientManager::sendHeartbeat()
{
    if (m_tcpSocket->state() != QAbstractSocket::ConnectedState) return;

    m_heartbeatRetryCount++;
    if (m_heartbeatRetryCount > 3) { // 连续 3 次没收到回应 (6秒)
        qDebug() << "心跳超时，认为连接已断开！";
        m_tcpSocket->abort(); // 强制断开，触发重连机制
        return;
    }

    FrameHeader head;
    head.header = FRAME_HEAD;
    head.type   = CMD_HEARTBEAT;
    head.len    = 0; // 心跳包只有头尾，没有载荷

    QByteArray packet;
    packet.append((const char*)&head, sizeof(FrameHeader));
    packet.append((char)FRAME_TAIL);

    m_tcpSocket->write(packet);
}

// --- 以下为运动与控制指令发送 (保持不变) ---

void TcpClientManager::sendControl(bool led, bool buzzer, int mode)
{
    // 如果没连上，直接返回
    if (m_tcpSocket->state() != QAbstractSocket::ConnectedState) return;

    QByteArray packet;
    ControlPayload payload;
    payload.mode = mode;           // 模式 (比如 1代表使能/自动，0代表手动)
    payload.led_switch = led;      // 激光开关 (1开 0关)
    payload.buzzer_switch = 0;     // 蜂鸣器默认关
    payload.padding = 0;           // 内存对齐占位

    FrameHeader head;
    head.header = FRAME_HEAD;
    head.type   = CMD_CONTROL;
    head.len    = sizeof(ControlPayload);

    // 拼装数据包：包头 + 载荷 + 包尾
    packet.append((const char*)&head, sizeof(FrameHeader));
    packet.append((const char*)&payload, sizeof(ControlPayload));
    packet.append((char)FRAME_TAIL);

    m_tcpSocket->write(packet);
}

void TcpClientManager::sendTarget(int x, int y)
{
    if (m_tcpSocket->state() != QAbstractSocket::ConnectedState) return;

    QByteArray packet;
    TargetPayload payload;
    payload.x = (int16_t)x;
    payload.y = (int16_t)y;

    FrameHeader head;
    head.header = FRAME_HEAD;
    head.type   = CMD_TARGET;
    head.len    = sizeof(TargetPayload);

    packet.append((const char*)&head, sizeof(FrameHeader));
    packet.append((const char*)&payload, sizeof(TargetPayload));
    packet.append((char)FRAME_TAIL);

    m_tcpSocket->write(packet);
}

void TcpClientManager::sendMove(float vx, float vy, float vz)
{
    // 没连上就不发
    if (m_tcpSocket->state() != QAbstractSocket::ConnectedState) return;

    MovePayload payload;
    payload.vx = vx; // 前后速度 (正为前，负为后)
    payload.vy = vy; // 左右平移速度 (麦克纳姆轮专属)
    payload.vz = vz; // 旋转速度

    FrameHeader head;
    head.header = FRAME_HEAD;
    head.type   = CMD_MOVE;
    head.len    = sizeof(MovePayload);

    QByteArray packet;
    packet.append((const char*)&head, sizeof(FrameHeader));
    packet.append((const char*)&payload, sizeof(MovePayload));
    packet.append((char)FRAME_TAIL);

    m_tcpSocket->write(packet);
}
