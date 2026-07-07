cmake_minimum_required(VERSION 3.10)

project(car_hmi LANGUAGES CXX)

# 1. 开启 Qt 自动处理 (非常关键，否则无法识别 .ui 文件)
set(CMAKE_AUTOMOC ON) # 自动生成元对象代码
set(CMAKE_AUTOUIC ON) # 自动将 .ui 转换为 .h
set(CMAKE_AUTORCC ON) # 自动处理资源文件
set(CMAKE_AUTOUIC_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/ui")

# 2. 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 3. 寻找依赖库
# 寻找 Qt5
find_package(Qt5 COMPONENTS Widgets Sql REQUIRED)

# 寻找 Paho MQTT 库 (Ubuntu 原生方式，取代 vcpkg)
find_library(PAHO_CPP_LIB paho-mqttpp3 REQUIRED)
find_library(PAHO_C_LIB paho-mqtt3a REQUIRED)

# 寻找 OpenCV
find_package(OpenCV REQUIRED)

# 👉 [新增] 寻找并配置瑞芯微 RKNN NPU 库
# 香橙派系统里，rknn_api.h 通常在 /usr/include 或 /usr/include/rknn
# librknnrt.so 通常在 /usr/lib 或 /usr/lib/aarch64-linux-gnu
find_path(RKNN_INCLUDE_DIR rknn_api.h PATHS /usr/include /usr/include/rknn /usr/local/include)
find_library(RKNN_LIBRARY rknnrt PATHS /usr/lib /usr/lib/aarch64-linux-gnu /usr/local/lib)

if(NOT RKNN_LIBRARY)
    message(WARNING "未自动找到 librknnrt.so！如果你在编译时报错，请手动指定 RKNN 库路径。")
endif()

# 👉 [新增] 寻找并配置 RGA 硬件加速库
set(RGA_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/third_party/rga")
find_library(RGA_LIBRARY NAMES rga rga.so.2
    PATHS /usr/lib/aarch64-linux-gnu /usr/lib /usr/local/lib)

if(NOT RGA_LIBRARY)
    # find_library 有时认不出只有 .so.2 没有 .so 软链接的库，手动兜底
    set(RGA_LIBRARY /usr/lib/aarch64-linux-gnu/librga.so.2)
    message(WARNING "find_library 未找到 librga，回退使用硬编码路径: ${RGA_LIBRARY}")
endif()

if(NOT EXISTS ${RGA_LIBRARY})
    message(FATAL_ERROR "librga 库文件不存在，请检查路径: ${RGA_LIBRARY}")
endif()

# 4. 指定源文件
set(PROJECT_SOURCES
    src/main.cpp
    src/mainwindow.cpp
    src/mainwindow.h
    src/mqttclientmanager.cpp
    src/mqttclientmanager.h
    src/vision.cpp      
    src/vision.h
    src/inference.cpp  
    src/inference.h
    ui/mainwindow.ui
)

# 5. 生成可执行程序
add_executable(${PROJECT_NAME} ${PROJECT_SOURCES})


target_include_directories(${PROJECT_NAME} PRIVATE ${RKNN_INCLUDE_DIR} ${RGA_INCLUDE_DIR})

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt5::Widgets
    Qt5::Sql
    ${PAHO_CPP_LIB}     # 链接 MQTT C++ 库
    ${PAHO_C_LIB}       # 链接 MQTT C 异步底层库
    ${OpenCV_LIBS}
    ${RKNN_LIBRARY}     # 链接 NPU 硬件加速库
    ${RGA_LIBRARY}      # 👉 [新增] 链接 RGA 硬件加速库
)

# 7. (可选) 设置输出目录
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)


# 自动把 models 目录拷贝到可执行文件旁边
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/models
    $<TARGET_FILE_DIR:${PROJECT_NAME}>/models
)