#include "mqttclientmanager.h"
#include <QDebug>

MqttClientManager::MqttClientManager(QObject *parent) : QObject{parent} {
    // 初始化时给一个占位符地址，防止新版 Paho MQTT 库禁用空参数构造
    m_client = new mqtt::async_client("tcp://127.0.0.1:1883", "InitialClient");
    m_client->set_callback(*this); // 提前绑定好回调接口
}

MqttClientManager::~MqttClientManager() {
    // 析构函数，先安全断开连接再删除客户端实例
    if (m_client) {
        // 增加 is_connected() 校验，防止对未连接的客户端强行断开引发异常
        if (m_client->is_connected()) {
            try {
                m_client->disconnect()->wait();
            } catch (...) {} // 析构函数中忽略异常，防止程序崩溃退出
        }
        delete m_client;
    }
}

void MqttClientManager::connectToBroker(const QString &ip, int port){
    // 构建 MQTT Broker URL = tcp://[IP地址]:[端口号]
    std::string brokerUrl = QString("tcp://%1:%2").arg(ip).arg(port).toStdString();
    
    // 如果已有客户端实例，先安全断开连接并删除
    if(m_client) {
        if (m_client->is_connected()) {
            try {
                m_client->disconnect()->wait();
            } catch (...) {}
        }
        delete m_client;
    }

    // 创建新的 MQTT 客户端实例
    m_client = new mqtt::async_client(brokerUrl, "OrangePi5_HMI_Main");
    m_client->set_callback(*this); // 绑定回调接口

    // 配置连接选项，启用自动重连和心跳机制
    m_connOpts = mqtt::connect_options_builder()
        .clean_session(true)
        .keep_alive_interval(std::chrono::seconds(5))
        .automatic_reconnect(true)
        .finalize();
    
    try{
        qDebug() << ">>> 正在尝试连接 MQTT Broker:" << QString::fromStdString(brokerUrl);
        m_client->connect(m_connOpts);
    } catch (const mqtt::exception& exc) {
        // 连接失败，发出连接状态改变信号，携带错误信息
        emit connectionStatusChanged(false, QString::fromStdString(exc.what()));
    }
}

void MqttClientManager::disconnectFromBroker(){
    if (m_client && m_client->is_connected()) {
        try {
            m_client->disconnect()->wait();
        } catch (const mqtt::exception& exc) {
            qDebug() << ">>> 断开 MQTT 连接时异常:" << exc.what();
        }
    }
}

void MqttClientManager::sendMove(float vx, float vy, float vz){
    if (!m_client || !m_client->is_connected()) return;
    
    MovePayload payload = {vx, vy, vz}; 
    FrameHeader head = {0x5A, CMD_MOVE, sizeof(MovePayload)};

    QByteArray packet;
    packet.append((const char*)&head, sizeof(FrameHeader));
    packet.append((const char*)&payload, sizeof(MovePayload));

    // 推荐加上 QoS (0) 和 retained (false)
    try {
        m_client->publish(TOPIC_CMD, packet.data(), packet.size(), 0, false);
    } catch (...) {
        qDebug() << ">>> 发送移动指令失败";
    }
}

void MqttClientManager::sendControl(bool led, bool buzzer, int mode){
    if (!m_client || !m_client->is_connected()) return;
    
    ControlPayload payload;
    payload.mode = (uint8_t)mode;
    payload.led_switch = led ? 1 : 0;
    payload.buzzer_switch = buzzer ? 1 : 0;
    payload.padding = 0;

    FrameHeader head = {0x5A, CMD_CONTROL, sizeof(ControlPayload)};

    QByteArray packet;
    packet.append((const char*)&head, sizeof(FrameHeader));
    packet.append((const char*)&payload, sizeof(ControlPayload));

    try {
        m_client->publish(TOPIC_CMD, packet.data(), packet.size(), 0, false);
    } catch (...) {
        qDebug() << ">>> 发送控制指令失败";
    }
}

// ===================== MQTT 回调处理 =====================

void MqttClientManager::connected(const mqtt::string& cause){
    QString statusMsg;

    if (cause.empty()) {
        statusMsg = "初始连接成功";
        qDebug() << ">>> MQTT 初始连接已建立";
    } else {
        statusMsg = QString("自动重连成功: %1").arg(QString::fromStdString(cause));
        qDebug() << ">>> MQTT 自动重连成功，原因:" << statusMsg;
    }

    // 连接成功后，务必订阅主题
    try {
        m_client->subscribe(TOPIC_STATUS, 1);
    } catch (const mqtt::exception& exc) {
        qDebug() << ">>> 订阅主题失败:" << exc.what();
    }

    // 通知 MainWindow 更新 UI
    emit connectionStatusChanged(true, statusMsg);
}

void MqttClientManager::connection_lost(const mqtt::string& cause){
    qDebug() << ">>> MQTT 连接丢失！原因:" << QString::fromStdString(cause);
    emit connectionStatusChanged(false, QString::fromStdString(cause));
}

void MqttClientManager::message_arrived(mqtt::const_message_ptr msg){
    // 获取原始内存指针和长度，防止 \x00 截断
    const std::string& raw_payload = msg->get_payload();
    QByteArray data(raw_payload.data(), raw_payload.length());
    
    // 如果需要调试，可以打开下面这行查看二进制 Hex 数据
    // qDebug() << ">>> 收到 MQTT 原始二进制消息 (Hex):" << data.toHex();
    
    if (data.size() < (int)sizeof(FrameHeader)) return;

    FrameHeader* header = reinterpret_cast<FrameHeader*>(data.data());
    
    // 校验包头 0x5A
    if (header->header != 0x5A) return; 

    // 防止越界读取崩溃
    if (data.size() < (int)(sizeof(FrameHeader) + header->len)) return;

    // 提取载荷并发出信号给 MainWindow 处理
    QByteArray payload_data = data.mid(sizeof(FrameHeader), header->len);
    emit dataReceived(header->type, payload_data);
}