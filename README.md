# STM32 墨水屏课程表

基于 **STM32F103C8T6** + **4.2 英寸黑白墨水屏（SSD1619A）** 的电子课程表。

5 天 × 5 大节的网格布局，表头（星期）与首列（节次）反白强调。

---

## 特性

- **SSD1619A 驱动**，从零实现，不依赖第三方图形库
  - 初始化 / 显存绘图（点、线、矩形）/ 刷新 / 深度睡眠
  - 命令集依据数据手册逐条核对
- **显存 1bpp**，400×300 = 15000 字节，直接位操作
- **ASCII 8×16 字库**，含 `EPD_DrawChar` / `EPD_DrawString`
- **课程表业务模块**与驱动分层，布局参数集中在头文件，改布局不动驱动
- **硬件自检**（4 张测试画面）+ **串口诊断**，方便定位接线问题

---

## 硬件

| 部件   | 型号 / 规格                                       |
| ------ | ------------------------------------------------- |
| MCU    | STM32F103C8T6（Cortex-M3，64KB Flash / 20KB RAM） |
| 屏     | 4.2" 黑白墨水屏，400×300，SSD1619A，24pin FPC    |
| 驱动板 | 24pin 通用转接板（纯转接）                        |
| 调试器 | CMSIS-DAP / DAPLink（SWD 4 线）                   |

接线表、CubeMX 配置、注意事项 → 见 [HARDWARE.md](HARDWARE.md)

关于驱动板：参考 [gitee.com/uYuToo/minieink](https://gitee.com/uYuToo/minieink)

---

## 编译

### 第 0 步：先生成 HAL 库（重要）

本仓库**不包含** `Drivers/`（ST 的 HAL 库与 CMSIS），以保持仓库轻量。
**首次编译前必须先补上**，方法二选一：

用 CubeMX 重新生成

1. 用 STM32CubeMX 打开根目录的 `timetable.ioc`
2. 确认 `Project Manager → Toolchain/IDE` 选的是 **CMake**
3. 点 **GENERATE CODE**

CubeMX 会生成 `Drivers/` 目录，并补齐 `CMakeLists.txt` / `main.c` 等文件的
框架部分（你的应用代码在 `USER CODE` 块内，不受影响）。


### 第 1 步：编译

需要 `arm-none-eabi-gcc`、`cmake`（≥3.22）、`ninja`。

```bash
cmake --preset Debug
cmake --build build/Debug
```

产物：`build/Debug/timetable.elf`

若要生成 bin / hex：

```bash
arm-none-eabi-objcopy -O binary build/Debug/timetable.elf build/Debug/timetable.bin
arm-none-eabi-objcopy -O ihex   build/Debug/timetable.elf build/Debug/timetable.hex
```

> 若报找不到编译器，把 `cmake/gcc-arm-none-eabi.cmake` 里的路径改成你本机
> arm-none-eabi-gcc 的位置。

## 烧录

```bash
openocd -f interface/cmsis-dap.cfg -f target/stm32f1x.cfg \
  -c "program build/Debug/timetable.elf verify reset exit"
```

期望输出 `** Verified OK **`。也可用 ST-Link / J-Link 等任意 SWD 工具，
或直接烧 `timetable.bin` / `timetable.hex`。

---

## 代码结构

```
Core/
├── Src/
│   ├── epd.c               ★ 墨水屏驱动（初始化/绘图/文字/刷新/睡眠）
│   ├── font.c                ASCII 8x16 字库
│   ├── timetable.c         ★ 课程表布局与绘制（业务模块）
│   ├── timetable_demo.c      演示数据（改成你自己的课表）
│   ├── log.c                 串口调试打点
│   ├── epd_test.c            硬件自检 / 串口诊断（正式使用可删）
│   ├── main.c                主流程
│   └── spi.c / gpio.c / usart.c   外设配置（CubeMX 生成）
└── Inc/                    对应头文件

Drivers/                    ST HAL 库 + CMSIS（不随仓库分发，用 CubeMX 生成）
timetable.ioc               CubeMX 工程文件
cmake/                      CMake 构建脚本
```

**分层设计**：

```
font.c       字模数据        （不依赖任何东西）
   ↓
epd.c        画点/线/框/字/刷新（依赖 SPI + GPIO）
   ↓
timetable.c  课表业务        （只用 epd 的画图接口）
```

改布局 → 只动 `timetable.h` 的尺寸宏；改驱动 → 只动 `epd.c`。互不影响。

---

## 怎么改成你自己的课表

1. 打开 `Core/Src/timetable_demo.c`
2. 把 `TT_SetCourse(day, period, "课程名")` 换成你的课程：
   - `day`：`0`~`4` = 周一~周五
   - `period`：`0`~`4` = 上午1 / 上午2 / 下午1 / 下午2 / 晚上
3. 编译烧录

调整布局（列宽、行高、节次名称）改 `Core/Inc/timetable.h` 顶部的宏。

---

## 中文字库

**当前只带 ASCII 字库**，课程名先用英文占位。要显示中文课程名，需要额外字库

**推荐方案 —— 子集字库**：

1. 列出你的课表里用到的**所有汉字**（通常几百个）
2. 用取模工具（如 PCtoLCD2002）或 Python 生成 16×16 点阵
3. 追加到 `font.c`，按现有格式存（详见 `font.h` 顶部的格式说明）
4. 加一个 `FONT_GetChinese()` 类似的取模接口

其他方案：外挂 SPI Flash 存全字库；或上位机动态下发字模。
