import sys
import argparse
import time
try:
    from PIL import Image, ImageOps
except ImportError:
    print("Error: Pillow is not installed. Please run: pip install Pillow")
    sys.exit(1)

try:
    import paho.mqtt.client as mqtt
except ImportError:
    print("Error: paho-mqtt is not installed. Please run: pip install paho-mqtt")
    sys.exit(1)

def image_to_rgb565(image_path):
    print(f"Reading and processing image: {image_path}")
    # 1. 打开图片并转换为 RGB 模式
    img = Image.open(image_path).convert('RGB')
    
    # 2. 居中裁剪并缩放到 240x240，防止图片被拉伸变形
    img = ImageOps.fit(img, (240, 240), Image.Resampling.LANCZOS)
    
    # 3. 转换为 RGB565 裸数据 (因为你的硬件屏幕是特殊的 RBG 排列：红5位，蓝6位，绿5位)
    pixels = img.load()
    rgb565_data = bytearray(240 * 240 * 2)
    idx = 0
    for y in range(240):
        for x in range(240):
            r, g, b = pixels[x, y]
            # 藍紅反轉 BGR565：藍色 5 bits, 綠色 6 bits, 紅色 5 bits
            color = ((b & 0xF8) << 8) | ((g & 0xFC) << 3) | (r >> 3)
            # 高位在前 (Big Endian)
            rgb565_data[idx] = (color >> 8) & 0xFF
            rgb565_data[idx+1] = color & 0xFF
            idx += 2
            
    return rgb565_data

def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print("✅ 成功连接到 MQTT Broker!")
    else:
        print(f"❌ 连接失败, 返回码: {rc}")

def main():
    parser = argparse.ArgumentParser(description="向 ESP32 智能屏幕推送 240x240 RGB565 图像的 MQTT 工具")
    parser.add_argument("image", help="要推送的图片路径 (支持 jpg, png, bmp 等)")
    parser.add_argument("-b", "--broker", default="127.0.0.1", help="MQTT Broker 地址 (默认: 127.0.0.1)")
    parser.add_argument("-p", "--port", type=int, default=1883, help="MQTT 端口 (默认: 1883)")
    parser.add_argument("-t", "--topic", required=True, help="目标 MQTT Topic (必须指定)")
    parser.add_argument("-u", "--user", default="", help="MQTT 用户名 (选填)")
    parser.add_argument("-w", "--pwd", default="", help="MQTT 密码 (选填)")
    args = parser.parse_args()

    # 处理图片
    rgb565_data = image_to_rgb565(args.image)
    print(f"✅ 生成 RGB565 数据成功，大小: {len(rgb565_data)} 字节")

    # 配置 MQTT
    client = mqtt.Client()
    if args.user:
        client.username_pw_set(args.user, args.pwd)
        
    client.on_connect = on_connect
    
    print(f"⏳ 正在连接到 {args.broker}:{args.port}...")
    try:
        client.connect(args.broker, args.port, 60)
    except Exception as e:
        print(f"❌ 无法连接到 MQTT 服务器: {e}")
        sys.exit(1)
        
    client.loop_start()
    
    # 稍微等一下确保连接成功
    time.sleep(1)
    
    print(f"🚀 正在向 Topic '{args.topic}' 推送图像数据...")
    info = client.publish(args.topic, rgb565_data, qos=0)
    info.wait_for_publish()
    
    print("🎉 图像推送完成！")
    client.loop_stop()
    client.disconnect()

if __name__ == "__main__":
    main()
