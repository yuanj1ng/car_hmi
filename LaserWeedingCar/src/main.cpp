#include <Arduino.h>
#include <WiFi.h>
#include "protocol_def.h" // 确保你的 include 文件夹里有这个头文件

// ==========================================
// 1. 引脚与硬件定义
// ==========================================
// 电机A (左前)
#define MOTOR_A_PWM 17
#define MOTOR_A_IN1 18
#define MOTOR_A_IN2 19
// 电机B (右前)
#define MOTOR_B_PWM 21
#define MOTOR_B_IN1 22
#define MOTOR_B_IN2 23
// 电机C (右后)
#define MOTOR_C_PWM 25
#define MOTOR_C_IN1 26
#define MOTOR_C_IN2 27 
// 电机D (左后)
#define MOTOR_D_PWM 32
#define MOTOR_D_IN1 33
#define MOTOR_D_IN2 15 

// ==========================================
// 2. WiFi & TCP 服务器配置
// ==========================================
const char* ssid = "CU_55e9";          // 替换为你的 WiFi 名称
const char* password = "k6bfcgtx";  // 替换为你的 WiFi 密码
const int tcp_port = 8080;                    

WiFiServer server(tcp_port);
WiFiClient client;

#define RX_BUFFER_SIZE 128
uint8_t rx_buffer[RX_BUFFER_SIZE];
int rx_index = 0;

// === 函数声明 ===
void handleClientData();
void processIncomingPacket(uint8_t type, uint8_t* payload);
void setMotorSpeed(int in1, int in2, int pwm_channel, int speed);

void setup() {
    Serial.begin(115200);
    Serial.println("小车启动中，开始执行setup()...");

    // --- 1. 初始化电机引脚 ---
    pinMode(MOTOR_A_IN1, OUTPUT); pinMode(MOTOR_A_IN2, OUTPUT);
    pinMode(MOTOR_B_IN1, OUTPUT); pinMode(MOTOR_B_IN2, OUTPUT);
    pinMode(MOTOR_C_IN1, OUTPUT); pinMode(MOTOR_C_IN2, OUTPUT);
    pinMode(MOTOR_D_IN1, OUTPUT); pinMode(MOTOR_D_IN2, OUTPUT);

    // --- 2. 配置PWM功能 ---
    ledcSetup(0, 5000, 8); // 频率 5000Hz, 分辨率 8位 (0-255)
    ledcSetup(1, 5000, 8); 
    ledcSetup(2, 5000, 8); 
    ledcSetup(3, 5000, 8); 

    ledcAttachPin(MOTOR_A_PWM, 0);
    ledcAttachPin(MOTOR_B_PWM, 1);
    ledcAttachPin(MOTOR_C_PWM, 2);
    ledcAttachPin(MOTOR_D_PWM, 3);

    // 初始状态全部刹车
    setMotorSpeed(MOTOR_A_IN1, MOTOR_A_IN2, 0, 0);
    setMotorSpeed(MOTOR_B_IN1, MOTOR_B_IN2, 1, 0);
    setMotorSpeed(MOTOR_C_IN1, MOTOR_C_IN2, 2, 0);
    setMotorSpeed(MOTOR_D_IN1, MOTOR_D_IN2, 3, 0);

    // --- 3. 连接 WiFi ---
    Serial.printf("Connecting to %s ", ssid);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi Connected!");
    Serial.print("============================\n");
    Serial.print("ESP32 IP Address: ");
    Serial.println(WiFi.localIP()); 
    Serial.print("============================\n");

    // --- 4. 启动 TCP 服务 ---
    server.begin();
}

void loop() {
    if (!client.connected()) {
        client = server.available();
        if (client) {
            Serial.println("上位机已连接！准备接收指令...");
            rx_index = 0; 
            
            // 发送一个初始心跳或状态给上位机
            FrameHeader head = {FRAME_HEAD, CMD_HEARTBEAT, 0};
            uint8_t tail = FRAME_TAIL;
            client.write((const uint8_t*)&head, sizeof(head));
            client.write(&tail, 1);
        }
    } else {
        // 全速处理网络数据，坚决不能加 delay()
        handleClientData();
    }
}

// ==========================================
// 核心：单电机底层驱动封装
// ==========================================
// speed 取值范围：-255 到 +255
void setMotorSpeed(int in1, int in2, int pwm_channel, int speed) {
    if (speed > 0) {
        digitalWrite(in1, HIGH);
        digitalWrite(in2, LOW);
        ledcWrite(pwm_channel, speed > 255 ? 255 : speed);
    } else if (speed < 0) {
        digitalWrite(in1, LOW);
        digitalWrite(in2, HIGH);
        ledcWrite(pwm_channel, abs(speed) > 255 ? 255 : abs(speed));
    } else {
        // 速度为 0 时执行刹车
        digitalWrite(in1, HIGH);
        digitalWrite(in2, HIGH);
        ledcWrite(pwm_channel, 0);
    }
}

// ==========================================
// TCP 数据防粘包解析
// ==========================================
void handleClientData() {
    while (client.available() > 0) {
        if (rx_index >= RX_BUFFER_SIZE) rx_index = 0; 
        rx_buffer[rx_index++] = client.read();

        while (rx_index >= sizeof(FrameHeader) + 1) {
            if (rx_buffer[0] != FRAME_HEAD) {
                memmove(rx_buffer, rx_buffer + 1, rx_index - 1);
                rx_index--;
                continue;
            }

            FrameHeader* head = (FrameHeader*)rx_buffer;
            int fullPackLen = sizeof(FrameHeader) + head->len + 1;

            if (rx_index < fullPackLen) break; 

            if (rx_buffer[fullPackLen - 1] == FRAME_TAIL) {
                uint8_t* payload = rx_buffer + sizeof(FrameHeader);
                processIncomingPacket(head->type, payload);

                int remaining = rx_index - fullPackLen;
                if (remaining > 0) memmove(rx_buffer, rx_buffer + fullPackLen, remaining);
                rx_index = remaining;
            } else {
                memmove(rx_buffer, rx_buffer + 1, rx_index - 1);
                rx_index--;
            }
        }
    }
}

// ==========================================
// 业务逻辑：解析指令并控制硬件
// ==========================================
void processIncomingPacket(uint8_t type, uint8_t* payload) {
    switch(type) {
        case CMD_HEARTBEAT:
            {
                // 心跳包原样返回
                FrameHeader head = {FRAME_HEAD, CMD_HEARTBEAT, 0};
                uint8_t tail = FRAME_TAIL;
                client.write((const uint8_t*)&head, sizeof(head));
                client.write(&tail, 1);
            }
            break;

        case CMD_MOVE:
            {
                MovePayload* moveData = (MovePayload*)payload;
                int vx = moveData->vx; // 前后
                int vy = moveData->vy; // 左右平移
                int vz = moveData->vz; // 旋转
                
                Serial.printf("收到运动指令: Vx=%d, Vy=%d, Vz=%d\n", vx, vy, vz);
                // 运动学逆解计算 (适用于全向轮/麦克纳姆轮)
                // [Image of Mecanum wheel kinematics diagram]
                int speedA = vx + vy + vz; // 左前
                int speedB = vx - vy - vz; // 右前
                int speedC = vx + vy - vz; // 右后
                int speedD = vx - vy + vz; // 左后

                setMotorSpeed(MOTOR_A_IN1, MOTOR_A_IN2, 0, speedA);
                setMotorSpeed(MOTOR_B_IN1, MOTOR_B_IN2, 1, speedB);
                setMotorSpeed(MOTOR_C_IN1, MOTOR_C_IN2, 2, speedC);
                setMotorSpeed(MOTOR_D_IN1, MOTOR_D_IN2, 3, speedD);
            }
            break;

        case CMD_TARGET:
            {
                TargetPayload* targetData = (TargetPayload*)payload;
                Serial.printf("收到杂草坐标: X=%d, Y=%d\n", targetData->x, targetData->y);
                // 这里可以留作后续控制云台舵机的扩展点
            }
            break;

        case CMD_CONTROL:
            {
                ControlPayload* ctrlData = (ControlPayload*)payload;
                // 收到开启指令时，如果你的激光挂在某个引脚上，可以在这里 digitalWrite
                Serial.printf("系统模式=%d, 激光器状态=%d\n", ctrlData->mode, ctrlData->led_switch);
            }
            break;
    }
}