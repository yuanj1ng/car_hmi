#ifndef MQTTCLIENTMANAGER_H
#define MQTTCLIENTMANAGER_H

#include <QObject>
#include <mqtt/async_client.h> // vcpkg 安装的 Paho 核心库
#include "protocol_def.h"

// 继承 mqtt::callback 以处理连接丢失和消息到达事件
class MqttClientManager : public QObject, public virtual mqtt::callback 
{
    Q_OBJECT
public:
    explicit MqttClientManager(QObject *parent = nullptr);
    virtual ~MqttClientManager();

    // 基础连接接口
    void connectToBroker(const QString &ip, int port);
    void disconnectFromBroker();

    // 业务发送接口：内部封装 publish 逻辑
    void sendMove(float vx, float vy, float vz);
    void sendControl(bool led, bool buzzer, int mode);

signals:
    // 连接状态改变信号，用于更新 MainWindow 的连接按钮颜色
    void connectionStatusChanged(bool connected, const QString &message);
    // 收到小车回传数据信号
    void dataReceived(uint8_t type, const QByteArray &payload);

protected:
    // Paho MQTT 库的回调重写
    void connected(const mqtt::string& cause) override;
    void connection_lost(const mqtt::string& cause) override;
    void message_arrived(mqtt::const_message_ptr msg) override;

private:
    mqtt::async_client* m_client = nullptr;
    mqtt::connect_options m_connOpts;
    
    // 主题定义
    const std::string TOPIC_CMD    = "car/cmd";    // 发送
    const std::string TOPIC_STATUS = "car/status"; // 接收
};
#endif