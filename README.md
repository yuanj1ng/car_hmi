# 🚜 基于 RK3588S 的端侧 AI 视觉检测与底盘控制系统 (Laser Weeder HMI)

![C++](https://img.shields.io/badge/C++-17-blue.svg) ![Qt](https://img.shields.io/badge/Qt-5.15-green.svg) ![RKNN](https://img.shields.io/badge/RKNN-NPU-orange.svg) ![V4L2](https://img.shields.io/badge/V4L2-MemoryMapping-red.svg) ![ESP32](https://img.shields.io/badge/ESP32-PWM-yellow.svg)

本项目为激光除草机器人上位机控制系统，运行于 **RK3588S (Ubuntu)** 嵌入式平台。系统覆盖从 **Linux 内核驱动层**（摄像头 Sensor 寄存器调优）、**底层视频采集层**（V4L2 + mmap 零拷贝）、**NPU 异构计算层**（RKNN 量化推理）到 **TCP 下位机控制层**（ESP32 电机驱动）的全链路实现。

---

## 🔧 核心工作与技术实现

### 1. Linux 底层驱动与内核适配 (Kernel/Driver)
- **Sensor 驱动修改**：修改 `imx415.c` 驱动源码，查阅 Sensor 手册计算 I2C 寄存器序列，新增 **60FPS** 运行模式。
- **设备树与内核编译**：修改设备树叠加层 (DTBO) 绑定 MIPI CSI 数据通道；重新编译内核模块并配置 `extlinux` 引导参数，完成系统移植。

### 2. 视频流底层采集 (V4L2)
- **弃用 GStreamer**：直接调用 `V4L2 ioctl` API 进行底层取图控制。
- **零拷贝内存映射**：通过 `mmap` 将内核态 NV12 图像缓冲区映射至用户态，消除数据拷贝开销，降低取帧延迟。

### 3. NPU 端侧 AI 部署 (RKNN)
- **模型量化**：基于 `rknn-toolkit2` 将 YOLOv8 模型量化为 **int8** 格式，适配 NPU 计算单元。
- **异步推理流水线**：调用 RKNN C++ API 执行推理，结合多线程实现 **"抓图-推理-渲染"** 三线程异步流水线。端到端检测帧率从 20FPS 提升并稳定至 **60 FPS**。

### 4. 上位机 UI 与并发架构 (Qt/C++)
- 开发 Qt 主控界面，使用 `QThread` 将视觉推理模块与主线程物理隔离。
- 通过 **信号槽 (Signals & Slots)** 实现跨线程检测框数据与图像帧的安全传递。

### 5. 高频日志落盘 (SQLite)
- 开启 SQLite **WAL 模式 (Write-Ahead Logging)**。
- 开辟独立后台线程，采用 **批量事务写入 (`execBatch`)** 机制写入日志与检测记录，规避磁盘 I/O 阻塞 UI 主线程。

### 6. 网络通信与下位机控制 (TCP/ESP32)
- **自定义应用层协议**：基于 `QTcpSocket` 实现上位机指令下发与状态同步，内置心跳包与断线重连机制。
- **ESP32 固件**：编写 ESP32 C++ 解析 TCP 报文，输出 **PWM** 信号驱动 **TB6612** 电机驱动模块，控制底盘直流电机运动。

---

## 🛠️ 技术栈 (Tech Stack)

| 层级 | 技术组件 |
| :--- | :--- |
| **系统层** | Linux Kernel (Ubuntu), Device Tree (DTBO), imx415 Driver |
| **视频采集** | V4L2, mmap, NV12 |
| **AI 推理** | RKNN API, rknn-toolkit2, YOLOv8-int8, NPU |
| **上位机框架** | C++17, Qt 5.15 (Widgets, Network, SQL, Concurrent) |
| **图像处理** | OpenCV 4.x |
| **数据存储** | SQLite 3 (WAL 模式) |
| **下位机** | ESP32, ESP-IDF, PWM, TB6612 |

---

## ⚙️ 编译与运行 (Ubuntu / RK3588S)

### 环境依赖
- 系统：Ubuntu 22.04 (Rockchip SDK)
- 编译器：GCC 9.4+
- 第三方库：OpenCV 4.x, Qt 5.15, rknn-toolkit2, SQLite3

### 编译步骤
1. 克隆仓库：
   ```bash
   git clone https://github.com/yuanj1ng/car_hmi.git
   cd car_hmi
