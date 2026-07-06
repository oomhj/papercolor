# 硬件初始化序列

PaperColor 上电后的硬件初始化顺序。

---

## 初始化步骤

每次上电或复位后，按以下顺序初始化硬件：

```
步骤 1: I2C 总线恢复（仅在非冷启动时）
         → 发送 9 个时钟脉冲释放被锁住的设备
         → 发出 STOP 条件
         → 恢复 GPIO 模式

步骤 2: M5.begin()
         → 初始化 I2C 主机总线 (SCL=G2, SDA=G3)
         → 初始化按键 GPIO (内部上拉)
         → 初始化 M5GFX 显示驱动
         → 初始化音频系统
         → 初始化 LED 控制

步骤 3: 离屏 Canvas
         → 在 PSRAM 中创建 M5Canvas
         → 分辨率适配当前旋转方向
         → 8-bit 色深

步骤 4: M5PM1 电源管理
         → I2C 初始化 (0x6e)
         → EPD 电源使能: PYG0 → HIGH (PY_EPD_EN)
         → 音频电源使能: G45 → HIGH
         → 扬声器功放关闭: G46 → LOW (默认静音)
         → 使能充电
         → 使能升压 (5V Grove)

步骤 5: 电池检测
         → 读取 Vbat
         → 如果低于 3.1V → 自动关机保护
         → 否则继续正常启动

步骤 6: SD 卡电源与检测
         → PYG3 → HIGH (PY_SD_PWR_EN)
         → PYG4 → HIGH (SD_DET_EN)
         → PYG1 → INPUT_PULLUP (CARD_DEC)

步骤 7: SPI 总线仲裁器
         → 创建互斥量（M5GFX 已初始化物理 SPI2_HOST）

步骤 8: RTC 唤醒引脚
         → M5PM1 PYG2 → WAKE 功能，下降沿触发
         → 连接 RX8130 IRQ 输出

步骤 9: 传感器就绪
         → SHT40、RX8130CE 在 I2C 总线上
         → 无需额外初始化，随时可读取
```

## 启动依赖关系

```
I2C 总线恢复
    │
    ▼
M5.begin()
    │
    ├──► I2C 可用
    ├──► 按键可用
    └──► 显示驱动加载
           │
           ▼
    离屏 Canvas (PSRAM)
           │
           ▼
    M5PM1.begin()
           │
           ├──► EPD 供电
           ├──► 音频供电
           ├──► SD 卡供电 + 检测
           ├──► 充电管理
           │
           ▼
    SPI 总线仲裁器 + RTC 唤醒引脚
```

## 关键规则

| 规则 | 说明 |
|------|------|
| EPD 必须先供电再操作 | 调用任何显示函数前确保 `pmu.digitalWrite(PYG0, HIGH)` |
| 音频有双重电源门控 | G45 = 编解码器供电，G46 = 功放使能，必须分别控制 |
| SD + EPD 共用 SPI | 操作 SD 前取消 EPD 片选，反之亦然 |
| 所有 I2C 共用一条总线 | 设备地址不可冲突 |
