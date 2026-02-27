# 🚜 Laser Weeder HMI (激光除草机器人上位机控制系统)

![C++](https://img.shields.io/badge/C++-11/14/17-blue.svg) ![Qt](https://img.shields.io/badge/Qt-5.15-green.svg) ![OpenCV](https://img.shields.io/badge/OpenCV-4.x-red.svg) ![ONNXRuntime](https://img.shields.io/badge/ONNXRuntime-Supported-orange.svg)

本项目是一款为“激光除草机器人”量身定制的跨平台上位机控制软件 (HMI)。
系统集成了**机器视觉目标检测**与**底层硬件 TCP 实时通信**，实现了从“眼睛（摄像头采集与杂草识别）”到“大脑（UI 交互与数据存储）”再到“四肢（下位机电机驱动）”的完整闭环控制。

---

## ✨ 核心特性 (Key Features)

* **👁️ 实时机器视觉流水线：** * 支持通过局域网读取手机 IP 摄像头的实时视频流。
  * 深度集成 `OpenCV` 与 `ONNX Runtime`，使用 `YOLOv8` 模型进行低延迟的杂草/目标检测。
  * 采用双缓冲与多线程架构（Vision Thread），保证 AI 推理时主 UI 界面“零卡顿”。
* **🎮 软硬件协同控制：** * 基于 `QTcpSocket` 与 ESP32 下位机建立高可靠性连接。
  * 自定义指令协议，支持全向运动控制（PWM 动态调速）与心跳包断线重连机制。
* **💾 数据持久化与日志：** * 内置 `SQLite` 数据库，实时记录检测目标的坐标、类别、置信度及系统运行日志，便于后续实验数据分析。
* **🖥️ 现代化交互界面：** * 基于 Qt 框架开发的工业级仪表盘，支持参数动态配置与实时画面渲染。

---

## 🛠️ 技术栈 (Tech Stack)

* **编程语言：** C++
* **GUI 框架：** Qt 5 (Widgets, Network, Sql, Concurrent)
* **视觉与 AI：** OpenCV 4, ONNX Runtime (C++ API), YOLOv8 
* **数据库：** SQLite 3
* **硬件端（配合使用）：** ESP32, L298N 电机驱动, 直流减速电机

---

## 🚀 快速体验 (Quick Start)

### 方式一：下载免安装发行版（推荐 Windows 用户）
1. 前往本仓库的 [Releases](https://github.com/yuanj1ng/car_hmi/releases) 页面。
2. 下载最新的 `win-x64Car_HMI.zip` 压缩包。
3. 解压至**全英文路径**下，直接双击运行 `Car_HMI.exe` 即可（已内置所有环境依赖）。

### 方式二：源码编译
1. 克隆本仓库：`git clone https://github.com/yuanj1ng/car_hmi.git`
2. 使用 Qt Creator 打开 `.pro` 或 `CMakeLists.txt` 文件。
3. 确保你的环境中已配置好 OpenCV 与 ONNX Runtime 的 C++ 库路径。
4. 将你的 YOLOv8 `.onnx` 模型文件与 `classes.txt` 放置于编译生成的同级 `3dparty/` 目录下。
5. 编译并运行。

---

## 🧠 系统架构简述 (Architecture)

为了保证工业级软件的稳定性，系统采用了严格的**主从多线程架构**：
1. **Main UI Thread (主线程)：** 负责接收用户输入、渲染 OpenCV 转换后的图像流、读写 SQLite 数据库。
2. **Vision Thread (视觉推理线程)：** 独立挂载定时器拉取网络视频流，调用 ONNX Runtime 进行前向推理，通过 Qt 的 `Signals and Slots` 机制安全地将检测框数据跨线程发给 UI。
3. **TCP Thread (网络通信线程)：** 独立处理大并发的底层指令发送与心跳包心跳监测，防止网络波动阻塞主程序。
