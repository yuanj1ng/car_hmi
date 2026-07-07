#include "inference.h"
#include <fstream>
#include <QDebug>
#include <QFile>
#include <cmath>
#include <algorithm>
#include <chrono> // 引入高精度计时器
#include "im2d.hpp" 

Inference::Inference(const std::string& modelPath, const cv::Size& inputSize, const QString& classesPath, rknn_core_mask core_mask)
{
    modelInputSize = inputSize;
    loadClasses(classesPath);

    // 1. 读取 .rknn 模型文件到内存
    FILE *fp = fopen(modelPath.c_str(), "rb");
    if (!fp) {
        qDebug() << "【致命错误】找不到 RKNN 模型文件:" << modelPath.c_str();
        return;
    }
    fseek(fp, 0, SEEK_END);
    int model_len = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    void *model_data = malloc(model_len);
    
    // ✅ 修复警告：接收 fread 的返回值并简单校验
    size_t read_bytes = fread(model_data, 1, model_len, fp);
    if (read_bytes != (size_t)model_len) {
        qDebug() << "⚠️ 警告: 模型文件读取不完整，期望" << model_len << "字节，实际读取" << read_bytes << "字节";
    }
    fclose(fp);

    // 2. 初始化 Context！(第一次声明变量 ret)
    int ret = rknn_init(&ctx, model_data, model_len, 0, NULL);
    if (ret < 0) {
        qDebug() << "【致命错误】rknn_init 失败! 错误码:" << ret;
        free(model_data); 
        return;
    }

    ret = rknn_set_core_mask(ctx, core_mask);
    
    if (ret == 0) {
        qDebug() << "set_core_mask: 成功 (Mask:" << core_mask << ")";
    } else {
        qDebug() << "【警告】rknn_set_core_mask 失败！错误码:" << ret;
    }

    // 4. 获取模型的输入输出节点信息
    rknn_query(ctx, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num));
    
    input_attrs = new rknn_tensor_attr[io_num.n_input];
    for (int i = 0; i < io_num.n_input; i++) {
        input_attrs[i].index = i;
        rknn_query(ctx, RKNN_QUERY_INPUT_ATTR, &(input_attrs[i]), sizeof(rknn_tensor_attr));
    }

    // ✅ 动态读取输出节点数量
    output_attrs = new rknn_tensor_attr[io_num.n_output];
    for (int i = 0; i < io_num.n_output; i++) {
        output_attrs[i].index = i;
        rknn_query(ctx, RKNN_QUERY_OUTPUT_ATTR, &(output_attrs[i]), sizeof(rknn_tensor_attr));
    }

    // 构造函数里 rknn_init 之后加这几行
    rknn_sdk_version sdk_ver;
    rknn_query(ctx, RKNN_QUERY_SDK_VERSION, &sdk_ver, sizeof(sdk_ver));
    qDebug() << "RKNN SDK:" << sdk_ver.api_version << "Driver:" << sdk_ver.drv_version;

    // 打印每个输出的数据类型，确认是 INT8
    for (int i = 0; i < io_num.n_output; i++) {
        qDebug() << "Output" << i 
                << "type:" << output_attrs[i].type  // 应该是 RKNN_TENSOR_INT8 = 5
                << "scale:" << output_attrs[i].scale
                << "zp:" << output_attrs[i].zp;
    }

    rknn_sdk_version version;
    ret = rknn_query(ctx, RKNN_QUERY_SDK_VERSION, &version, sizeof(rknn_sdk_version));
    if (ret == 0) {
        qDebug() << "RKNN C API (Runtime) Version:" << version.api_version;
        qDebug() << "RKNN Driver Version:" << version.drv_version;
    }

    qDebug() << "【成功】RK3588 NPU 模型加载完成！输出张量数:" << io_num.n_output;
    printf("========== 模型输出信息 ==========\n");
    printf("n_output = %d\n", io_num.n_output);
    for (int i = 0; i < io_num.n_output; i++) {
        printf("output[%d]: name=%s dims=(%d,%d,%d,%d) type=%d scale=%.4f zp=%d\n",
            i, output_attrs[i].name,
            output_attrs[i].dims[0], output_attrs[i].dims[1],
            output_attrs[i].dims[2], output_attrs[i].dims[3],
            output_attrs[i].type, output_attrs[i].scale, output_attrs[i].zp);
    }
    printf("===================================\n");
}


Inference::~Inference() {
    if (ctx > 0) rknn_destroy(ctx);
    if (input_attrs) delete[] input_attrs;
    if (output_attrs) delete[] output_attrs;
}

void Inference::loadClasses(const QString& classesPath) {
    QFile file(classesPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)){
        qDebug() << "打开 classes.txt 失败";
        return;
    }
    while (!file.atEnd()) {
        QByteArray line = file.readLine();
        classes.push_back(line.trimmed().toStdString());
    }
}

std::vector<Detection> Inference::runInference(const cv::Mat& frame) {
    std::vector<Detection> outputDetections;
    if (ctx == 0 || frame.empty()) return outputDetections;

    auto t0 = std::chrono::steady_clock::now();

    // ========== 1. 预处理（保持不变）==========
    float scale = std::min((float)modelInputSize.width / frame.cols,
                        (float)modelInputSize.height / frame.rows);
    int new_w = std::round(frame.cols * scale);
    int new_h = std::round(frame.rows * scale);
    int pad_left = (modelInputSize.width - new_w) / 2;
    int pad_top = (modelInputSize.height - new_h) / 2;

    cv::Mat letterbox_img(modelInputSize.height, modelInputSize.width, CV_8UC3, cv::Scalar(114, 114, 114));
    bool rga_ok = false;
    {
        rga_buffer_t src = wrapbuffer_virtualaddr((void*)frame.data, frame.cols, frame.rows, RK_FORMAT_BGR_888);
        rga_buffer_t dst = wrapbuffer_virtualaddr((void*)letterbox_img.data, letterbox_img.cols, letterbox_img.rows, RK_FORMAT_RGB_888);
        im_rect src_rect = {0, 0, frame.cols, frame.rows};
        im_rect dst_rect = {pad_left, pad_top, new_w, new_h};
        im_rect pat_rect = {0, 0, 0, 0};
        rga_buffer_t pat = {};
        IM_STATUS check_ret = imcheck(src, dst, src_rect, dst_rect);
        if (check_ret == IM_STATUS_NOERROR) {
            IM_STATUS run_ret = improcess(src, dst, pat, src_rect, dst_rect, pat_rect, IM_SYNC);
            rga_ok = (run_ret == IM_STATUS_SUCCESS);
        }
    }
    if (!rga_ok) {
        cv::Mat resized_img;
        cv::resize(frame, resized_img, cv::Size(new_w, new_h), 0, 0, cv::INTER_LINEAR);
        resized_img.copyTo(letterbox_img(cv::Rect(pad_left, pad_top, new_w, new_h)));
        cv::cvtColor(letterbox_img, letterbox_img, cv::COLOR_BGR2RGB);
    }

    // ========== 2. NPU输入配置（保持不变）==========
    rknn_input inputs[1];
    memset(inputs, 0, sizeof(inputs));
    inputs[0].index = 0;
    inputs[0].type = RKNN_TENSOR_UINT8;
    inputs[0].size = letterbox_img.cols * letterbox_img.rows * 3;
    inputs[0].fmt = RKNN_TENSOR_NHWC;
    inputs[0].pass_through = 0;
    inputs[0].buf = letterbox_img.data;
    rknn_inputs_set(ctx, io_num.n_input, inputs);

    auto t1 = std::chrono::steady_clock::now();

    // ========== 3. NPU推理 ==========
    rknn_run(ctx, NULL);

    // ========== 4. 获取9个输出（原生int8）==========
    rknn_output outputs[9];
    memset(outputs, 0, sizeof(outputs));
    for (int i = 0; i < 9; i++) {
        outputs[i].want_float = 0;
    }
    rknn_outputs_get(ctx, 9, outputs, NULL);

    auto t2 = std::chrono::steady_clock::now();

    // ========== 5. INT8解码（含DFL softmax）==========
    std::vector<int> class_ids;
    std::vector<float> confidences;
    std::vector<cv::Rect> boxes;
    int num_classes = classes.size();
    int strides[] = {8, 16, 32};
    const int REG_MAX = 16;  // DFL的bin数量
    float conf_threshold = 0.45f;

    for (int i = 0; i < 3; ++i) {
        // 每组3个输出：box(64ch) -> cls(80ch) -> clssum(1ch)
        int box_idx    = i * 3 + 0;
        int cls_idx    = i * 3 + 1;
        int clssum_idx = i * 3 + 2;

        int8_t* box_ptr    = (int8_t*)outputs[box_idx].buf;
        int8_t* cls_ptr    = (int8_t*)outputs[cls_idx].buf;
        int8_t* clssum_ptr = (int8_t*)outputs[clssum_idx].buf;

        float box_scale    = output_attrs[box_idx].scale;
        int   box_zp       = output_attrs[box_idx].zp;
        float cls_scale    = output_attrs[cls_idx].scale;
        int   cls_zp       = output_attrs[cls_idx].zp;
        float clssum_scale = output_attrs[clssum_idx].scale;
        int   clssum_zp    = output_attrs[clssum_idx].zp;

        int stride = strides[i];
        int grid_w = modelInputSize.width / stride;
        int grid_h = modelInputSize.height / stride;
        int grid_area = grid_w * grid_h;

        int8_t clssum_thres_i8 = std::max((int8_t)-128,
            (int8_t)std::min(127, (int)std::round(conf_threshold / clssum_scale + clssum_zp)));

        for (int y = 0; y < grid_h; ++y) {
            for (int x = 0; x < grid_w; ++x) {
                int g_idx = y * grid_w + x;

                if (clssum_ptr[g_idx] <= clssum_thres_i8) continue;

                int8_t maxScore_i8 = -128;
                int classId = -1;
                for (int c = 0; c < num_classes; ++c) {
                    int8_t score_i8 = cls_ptr[c * grid_area + g_idx];
                    if (score_i8 > maxScore_i8) {
                        maxScore_i8 = score_i8;
                        classId = c;
                    }
                }
                float maxScore = (maxScore_i8 - cls_zp) * cls_scale;
                if (maxScore <= conf_threshold) continue;

                float dist[4];
                for (int side = 0; side < 4; ++side) {
                    float logits[16];
                    float maxLogit = -1e9f;
                    for (int b = 0; b < 16; ++b) {
                        int ch = side * 16 + b;
                        float v = (box_ptr[ch * grid_area + g_idx] - box_zp) * box_scale;
                        logits[b] = v;
                        if (v > maxLogit) maxLogit = v;
                    }
                    float sumExp = 0.f;
                    float probs[16];
                    for (int b = 0; b < 16; ++b) {
                        probs[b] = std::exp(logits[b] - maxLogit);
                        sumExp += probs[b];
                    }
                    float weighted = 0.f;
                    for (int b = 0; b < 16; ++b) {
                        weighted += (probs[b] / sumExp) * b;
                    }
                    dist[side] = weighted;
                }

                float d_left = dist[0], d_top = dist[1], d_right = dist[2], d_bottom = dist[3];
                float cx = (x + 0.5f) * stride;
                float cy = (y + 0.5f) * stride;
                float x1 = cx - d_left * stride;
                float y1 = cy - d_top * stride;
                float x2 = cx + d_right * stride;
                float y2 = cy + d_bottom * stride;

                int final_x = std::max(0, (int)std::round((x1 - pad_left) / scale));
                int final_y = std::max(0, (int)std::round((y1 - pad_top) / scale));
                int final_w = std::min((int)std::round((x2 - x1) / scale), frame.cols - final_x);
                int final_h = std::min((int)std::round((y2 - y1) / scale), frame.rows - final_y);
                if (final_w <= 0 || final_h <= 0) continue;

                boxes.push_back(cv::Rect(final_x, final_y, final_w, final_h));
                confidences.push_back(maxScore);
                class_ids.push_back(classId);
            }
        }
    }
    // ========== 6. NMS（保持不变）==========
    std::vector<int> indices;
    cv::dnn::NMSBoxes(boxes, confidences, conf_threshold, 0.5f, indices);
    for (int i : indices) {
        Detection det;
        det.class_id = class_ids[i];
        det.confidence = confidences[i];
        det.box = boxes[i];
        det.targetX = boxes[i].x + boxes[i].width / 2;
        det.targetY = boxes[i].y + boxes[i].height / 2;
        det.className = (det.class_id >= 0 && det.class_id < classes.size()) ? classes[det.class_id] : "Unknown";
        outputDetections.push_back(det);
    }

    // ========== 7. 释放9个输出 ==========
    rknn_outputs_release(ctx, 9, outputs);

    auto t3 = std::chrono::steady_clock::now();
    double pre_time = std::chrono::duration<double, std::milli>(t1 - t0).count();
    double npu_time = std::chrono::duration<double, std::milli>(t2 - t1).count();
    double post_time = std::chrono::duration<double, std::milli>(t3 - t2).count();
    
    /*qDebug() << "[AI耗时拆解] 预处理:" << QString::number(pre_time, 'f', 1) << "ms"
             << "| NPU:" << QString::number(npu_time, 'f', 1) << "ms"
             << "| 后处理:" << QString::number(post_time, 'f', 1) << "ms";
    */

    return outputDetections;
}