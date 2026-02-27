cmake_minimum_required(VERSION 3.16)

# 1. 项目定义（必须放在最前面）
project(Car_HMI VERSION 1.0 LANGUAGES CXX)

# 2. 定义第三方库目录 (关键修改)
# ${CMAKE_CURRENT_SOURCE_DIR} 就是你的 car_hmi 文件夹路径
set(THIRDPARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/3dparty")

# 3. 配置 MSVC 编译选项
# 把它移到 add_executable 之后其实更标准，但如果想全局生效可以用 add_compile_options
if(MSVC)
    add_compile_options(/W4 /utf-8)
endif()

# 4. Qt 基础设置
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 5. 查找 Qt 库
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets SerialBus SerialPort Network Multimedia Sql)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets SerialBus SerialPort Network Multimedia Sql)

# 6. 配置 OpenCV (关键修改)
# 告诉 CMake 去哪里找 OpenCVConfig.cmake
# 假设你的结构是 car_hmi/3dparty/opencv/build
# 如果你的opencv文件夹名字不一样（比如叫 opencv4.8），请修改下面的 "opencv"
set(OpenCV_DIR "${THIRDPARTY_DIR}/opencv/build/x64/vc16/lib")
find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs dnn)

# 7. 配置 ONNX Runtime 路径 (关键修改)
# 使用相对路径指向 3dparty 下的 onnxruntime
# 请确认你的文件夹名是否完全匹配 "onnxruntime-win-x64-1.16.3"
set(ONNX_DIR "${THIRDPARTY_DIR}/onnxruntime-win-x64-1.16.3")


# 8. 源文件列表
set(PROJECT_SOURCES
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
    inference.cpp
    inference.h
    vision.h
    vision.cpp
    car_modbus_protocol.h
    TcpClientManager.h
    TcpClientManager.cpp
    protocol_def.h
)

# 9. 生成可执行文件
if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(Car_HMI
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
else()
    add_executable(Car_HMI
        ${PROJECT_SOURCES}
    )
endif()

# 10. 设置包含目录 (Include)
target_include_directories(Car_HMI PRIVATE
    "${ONNX_DIR}/include"
    ${OpenCV_INCLUDE_DIRS}
)

# 11. 链接库文件 (Link)
target_link_libraries(Car_HMI PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::SerialBus
    Qt${QT_VERSION_MAJOR}::SerialPort
    Qt${QT_VERSION_MAJOR}::Network
    Qt${QT_VERSION_MAJOR}::Multimedia
    Qt${QT_VERSION_MAJOR}::Sql
    ${OpenCV_LIBS}
    "${ONNX_DIR}/lib/onnxruntime.lib"
    # 如果不需要 providers_shared 可以注释掉下面这行，通常 onnxruntime.lib 就够了
    "${ONNX_DIR}/lib/onnxruntime_providers_shared.lib"
)

# 12. Qt 善后处理
if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(Car_HMI)
endif()
