# ESP32-S3 Smart Stock Terminal 📈

這是一個基於 ESP32-S3 與 ST7789 顯示器開發的智慧股票行情終端。它能夠自動獲取全球股市數據、監測環境溫濕度，並透過簡潔的 RESTful API 與 Web 控制台進行配置。

## 🚀 核心亮點
- **硬體效能**：基於 ESP32-S3，支援硬體加速繪圖。
- **無線配網**：支援 ESP-IDF 原生藍牙配網 (BLE Provisioning)，手機一鍵連線。
- **RESTful 架構**：前後端分離，透過 Web 介面遠端管理配置。
- **安全性**：內建 SHA-256 密碼加密與 Token 認證機制。
- **感測整合**：集成 BME280 高精度溫濕度氣壓計。

## 🛠️ 技術棧
- **開發環境**: ESP-IDF v6.0
- **核心語言**: C (Embedded)
- **通訊協議**: Wi-Fi (STA), BLE (Provisioning), HTTP/REST
- **加密技術**: PSA Crypto API (SHA-256)
- **硬體接口**: SPI (LCD), I2C (BME280), Capacitive Touch (IO6)

## 🏗️ 專案架構
```text
.
├── main/
│   ├── main.c           # 入口點與主循環
│   ├── ble_prov.c       # 藍牙配網邏輯
│   ├── web_server.c     # API 接口實作 (RESTful)
│   ├── stock_api.c      # 股票數據獲取
│   ├── graphics.c       # 螢幕繪圖與字庫
│   ├── bme280_drv.c     # 傳感器驅動
│   └── system_utils.c   # NVS 儲存與 Wi-Fi 管理
├── CMakeLists.txt       # 建置配置
└── index.html           # 遠端控制頁面