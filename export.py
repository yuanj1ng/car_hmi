import torch
import types
from ultralytics import YOLO
from ultralytics.nn.modules.head import Detect

# ---------- 核心魔法：自定义的去头前向传播函数 ----------
def custom_detect_forward(self, x):
    """
    专门为 RK3588 NPU 定制的前向传播。
    跳过所有 DFL (Distribution Focal Loss) 和 Softmax 等复杂后处理，
    直接返回 3 个尺度的纯净特征图。
    """
    shape = x[0].shape  # BCHW
    for i in range(self.nl):
        # cv2 是预测框 (Box) 的卷积分支
        box_features = self.cv2[i](x[i])
        # cv3 是类别 (Class) 的卷积分支
        cls_features = self.cv3[i](x[i])
        
        # 将框和类别在通道(Channel)维度上直接拼接
        # 输出维度变成了 [1, 144, 80, 80], [1, 144, 40, 40], [1, 144, 20, 20]
        # (假设 80 个类，加上 64 维的框特征 = 144)
        x[i] = torch.cat((box_features, cls_features), dim=1)
        
    return tuple(x)
# --------------------------------------------------------

def main():
    model_path = 'yolov8s.pt'
    onnx_path = 'yolov8s.onnx'

    print(f">>> 正在加载 PyTorch 模型: {model_path}...")
    # 如果当前目录没有 yolov8s.pt，它会自动从网上下载
    model = YOLO(model_path)

    # 遍历模型的每一层，找到 Detect 头
    for m in model.model.modules():
        if isinstance(m, Detect):
            print(">>> 找到 Detect 头！正在注入去头魔法 (Bypass DFL)...")
            # 动态替换原来的 forward 函数
            m.forward = types.MethodType(custom_detect_forward, m)
            # 禁用动态形状，NPU 强依赖静态形状
            m.dynamic = False 
            m.export = True

    print(">>> 正在导出为 ONNX 格式 (锁定 640x640, Opset 12)...")
    # 强制静态尺寸并使用 Opset 12
    model.export(
        format='onnx', 
        imgsz=[640, 640], 
        opset=12,         
        simplify=True,    # 开启 onnx-simplifier，极其重要
        dynamic=False
    )
    print(f"🎉 导出大功告成！干净的 ONNX 已覆盖当前目录。")

if __name__ == '__main__':
    main()