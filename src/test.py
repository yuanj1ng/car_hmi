import cv2
cap = cv2.VideoCapture('/dev/video0', cv2.CAP_V4L2)
cap.set(cv2.CAP_PROP_FRAME_WIDTH, 3840)
cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 2160)
print("width:", cap.get(cv2.CAP_PROP_FRAME_WIDTH))
print("height:", cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
ret, frame = cap.read()
if ret:
    print("shape:", frame.shape)
    cv2.imwrite('/home/jing/test_cv2.jpg', frame)
cap.release()