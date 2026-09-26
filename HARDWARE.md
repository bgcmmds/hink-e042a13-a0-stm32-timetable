# 硬件接线与配置

> **屏**：4.2" 黑白墨水屏，400×300，**SSD1619A**（24pin FPC，单 IC）
> **驱动板**：24pin 通用转接板
> **MCU**：STM32F103C8T6 @ 72MHz（HSE 8MHz 晶振）
> **调试器**：CMSIS-DAP（DAPLink / DAPMini），SWD 4 线

---

## 一、接线表（转接板 → STM32F103）

| 墨水屏信号 | 常见丝印       | STM32 引脚     | CubeMX 标签  | 方向 | 说明                     |
| ---------- | -------------- | -------------- | ------------ | ---- | ------------------------ |
| SCK        | CLK / SCK      | **PA5**  | SPI1_SCK     | AF   | SPI1 时钟                |
| MOSI       | DIN / SDA / SI | **PA7**  | SPI1_MOSI    | AF   | SPI1 数据                |
| DC (D/C#)  | DC             | **PB10** | `EPD_DC`   | 输出 | 0=命令，1=数据           |
| CS#        | CS / CS1       | **PB12** | `EPD_CS`   | 输出 | 片选，低有效             |
| RST        | RST / RES      | **PB13** | `EPD_RST`  | 输出 | 复位，低有效             |
| BUSY       | BUSY / BUS     | **PB14** | `EPD_BUSY` | 输入 | **高=忙，低=空闲** |
| VCC        | 3V3 / 3V       | 3.3V           | —           | 电源 |                          |
| GND        | GND            | GND            | —           | 电源 | 必须共地                 |

### 板载开关

| 开关      | 作用                                               | 本项目的设置                             |
| --------- | -------------------------------------------------- | ---------------------------------------- |
| BS / BS1  | 接口模式：GND=4-wire SPI，VCC=3-wire SPI           | **接 GND（4-wire SPI）**           |
| RESE 电阻 | 升压环反馈电阻（不同板子有 0.47R / 3R / 固定值等） | 按实际硬件试；理论参考值约 2.2Ω（选3R） |

---

## 二、CubeMX 配置要点

> 本仓库的 `timetable.ioc` 已包含以下配置，可直接用 CubeMX 打开。
> 若要自己配，参数如下。

### 1. SPI1

| 项                    | 值                                                  |
| --------------------- | --------------------------------------------------- |
| Mode                  | Full-Duplex Master                                  |
| Data Size             | 8 Bits                                              |
| Clock Polarity (CPOL) | Low                                                 |
| Clock Phase (CPHA)    | 1 Edge → 合起来是**SPI Mode 0**              |
| NSS                   | **Software**（不是 Hardware NSS）             |
| Baud Rate Prescaler   | 8（72MHz/8 =**9MHz**；不稳可改 16 → 4.5MHz） |
| First Bit             | MSB First                                           |
| CRC                   | Disabled                                            |

### 2. GPIO

| 引脚 | 模式             | 上下拉  | 速度 | 标签（User Label） |
| ---- | ---------------- | ------- | ---- | ------------------ |
| PB10 | Output Push Pull | No pull | High | `EPD_DC`         |
| PB12 | Output Push Pull | No pull | High | `EPD_CS`         |
| PB13 | Output Push Pull | No pull | High | `EPD_RST`        |
| PB14 | Input mode       | No pull | —   | `EPD_BUSY`       |

> **标签名必须是 `EPD_DC` / `EPD_CS` / `EPD_RST` / `EPD_BUSY`** —— CubeMX 据此生成
> `EPD_DC_Pin` / `EPD_DC_GPIO_Port` 等宏，`epd.c` 直接使用这些宏。

### 3. USART1（可选，调试用）

| 项          | 值                                                         |
| ----------- | ---------------------------------------------------------- |
| Mode        | Asynchronous                                               |
| Baud Rate   | 115200                                                     |
| Word Length | 8 Bits（无校验）                                           |
| Stop Bits   | 1                                                          |
| 引脚        | PA9=TX，PA10=RX（接 USB-TTL 时**TX/RX 交叉**，共地） |

### 4. 时钟

- HSE：**Crystal/Ceramic Resonator**（8MHz 外部晶振）
- PLL ×9 → SYSCLK **72MHz**，APB1 = /2，APB2 = /1

### 5. 重新生成代码后的注意事项

CubeMX **重新生成会覆盖 `main.c` 的 USER CODE 内容**（会被清空，不是"块内保留"）。
生成后检查：

1. **`main.c`** —— 确认这两段还在：

   ```c
   /* USER CODE BEGIN Includes */
   #include "epd.h"
   #include "log.h"
   #include "timetable.h"
   /* USER CODE END Includes */
   ```

   ```c
   /* USER CODE BEGIN 2 */
   LOG_Init();
   TT_Demo();
   /* USER CODE END 2 */
   ```
2. **`CMakeLists.txt`** —— `target_sources` 段里的用户源文件列表（CubeMX 会重置成空）。

> 注：`spi.c` 里 CubeMX 会**自动生成 `HAL_SPI_MspInit`**（含 PA5/PA7 复用配置），
> 不要再到 `stm32f1xx_hal_msp.c` 里重复写，否则重复定义链接失败。

---

## 三、编译 & 烧录

```bash
# 编译（Debug）
cmake --preset Debug
cmake --build build/Debug

# 生成 bin / hex（可选）
arm-none-eabi-objcopy -O binary build/Debug/timetable.elf build/Debug/timetable.bin
arm-none-eabi-objcopy -O ihex   build/Debug/timetable.elf build/Debug/timetable.hex

# 烧录（CMSIS-DAP + OpenOCD）
openocd -f interface/cmsis-dap.cfg -f target/stm32f1x.cfg \
  -c "program build/Debug/timetable.elf verify reset exit"
```

期望输出：`** Verified OK **`。

> 若 `cmake` 报 `CMakePresets.json` 找不到编译器，把 `CMakePresets.json` 里的
> 工具链路径改成你本机 arm-none-eabi-gcc 的位置。

---

## 四、自检：4 张测试画面

`EPD_TestPattern()`（在 `epd_test.c`）会依次刷 4 张画面，每张停 3 秒，
用来快速验证接线、极性、坐标、显存对齐：

| 顺序 | 画面                   | 验证什么               | 异常现象 → 可能原因               |
| ---- | ---------------------- | ---------------------- | ---------------------------------- |
| ①   | 全白                   | 能否刷新               | 出不来 → SPI 接线 / CS / 供电     |
| ②   | 全黑                   | 黑白极性               | 白黑颠倒 → 显存极性取反           |
| ③   | 四角 + 中心黑块 + 外框 | 坐标系 / 400×300 边界 | 偏位/缺角 → 分辨率或 RAM 窗口设置 |
| ④   | 竖条纹                 | 显存位序               | 变横纹 → Y 方向行对齐问题         |

在 `main()` 里把 `TT_Demo()` 换成 `EPD_TestPattern()` 即可运行自检。

> 4.2" 全刷一次约 **2~4 秒**，属正常，不要当成死机。

---

## 五、串口诊断

`EPD_Diag()`（在 `epd_test.c`）通过 USART1 逐条报告：

- 各控制引脚的实际电平
- 每步 BUSY 等待的实际耗时（超时说明 BUSY 一直为高）
- SPI 发送是否成功

屏不亮时，先用它定位是**硬件问题**还是**代码问题**。

---

## 六、代码结构

```
Core/
├── Src/
│   ├── main.c              # 主流程（CubeMX 生成 + USER CODE）
│   ├── epd.c               # ★ 墨水屏驱动：初始化 / 显存绘图 / 文字 / 刷新
│   ├── font.c              # ASCII 8x16 字库
│   ├── timetable.c         # ★ 课程表布局与绘制（业务模块）
│   ├── timetable_demo.c    # 演示数据（替换成你自己的课表）
│   ├── log.c               # 串口调试打点
│   ├── epd_test.c          # 硬件自检 / 诊断（正式使用可删）
│   ├── spi.c / gpio.c / usart.c   # 外设配置（CubeMX 生成）
│   └── stm32f1xx_hal_msp.c / stm32f1xx_it.c
└── Inc/                    # 对应头文件
```

**分层**：`font.c`（字模）→ `epd.c`（画点/线/框/字/刷新）→ `timetable.c`（课表业务）。
改布局不动驱动，改驱动不动布局。
