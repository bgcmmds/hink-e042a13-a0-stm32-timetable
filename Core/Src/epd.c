/**
  ******************************************************************************
  * @file    epd.c
  * @brief   4.2" 墨水屏驱动（400x300 黑白）
  *
  *  屏：HINK-E042A13-A0（24pin FPC，单 IC，SSD1619A）
  *
  *  ★ 命令集说明（重要，不要按 SSD1619/UC8176 写）：
  *    SSD1619A 的命令映射是 SSD1680 系列那一套，**不是**早期 SSD1619/UC8176 的。
  *    例如：0x12=SW RESET（不是刷新）、0x24=Write RAM(BW)、0x22=Display Update
  *    Control 2、0x20=Master Activation、0x10=Deep Sleep。
  *
  *  依据：
  *    1) SSD1619A 数据手册 §7 命令表
  *       —— 已逐条核对 0x11/0x12/0x20/0x21/0x22/0x24/0x26/0x3C/0x44/0x45/0x4E/0x4F
  *    2) 已实测点亮同规格 4.2" 屏（GDEH042Z96 兼容）的开源驱动实现，
  *       命令用法与手册命令表逐条吻合。
  *
  *  BUSY 语义：**高电平 = 忙, 低电平 = 空闲**
  *    手册 §8："BUSY pad will output high during operation"
  ******************************************************************************
  */
#include "epd.h"
#include "font.h"      /* ASCII 8x16 字库 */
#include "font_cn.h"   /* 16x16 中文字库（tools/font_export.py 生成） */
#include "log.h"       /* 串口打点 */
#include "main.h"      /* CubeMX 生成的引脚宏（EPD_*_Pin / EPD_*_GPIO_Port） */
#include <string.h>

/* ── 硬件 SPI 句柄（由 spi.c 定义）──────────────────────────────────────────*/
extern SPI_HandleTypeDef hspi1;

/* ── 命令定义（SSD1619A 手册 §7 命令表）─────────────────────────────────────*/
#define EPD_CMD_DRIVER_OUTPUT_CTRL   0x01   /* Driver Output control  (MUX/扫描方向) */
#define EPD_CMD_DATA_ENTRY_MODE      0x11   /* Data Entry mode setting */
#define EPD_CMD_SW_RESET             0x12   /* SW RESET */
#define EPD_CMD_MASTER_ACTIVATION    0x20   /* Master Activation */
#define EPD_CMD_DISPLAY_UPDATE_CTRL1 0x21   /* Display Update Control 1 */
#define EPD_CMD_DISPLAY_UPDATE_CTRL2 0x22   /* Display Update Control 2（刷新触发） */
#define EPD_CMD_WRITE_RAM_BW         0x24   /* Write RAM (BW)  1=白 0=黑 */
#define EPD_CMD_WRITE_RAM_RED        0x26   /* Write RAM (RED) 黑白用法填 0x00 */
#define EPD_CMD_BORDER_WAVEFORM      0x3C   /* Border Waveform Control */
#define EPD_CMD_SET_RAM_X_RANGE      0x44   /* Set RAM X address start/end */
#define EPD_CMD_SET_RAM_Y_RANGE      0x45   /* Set RAM Y address start/end */
#define EPD_CMD_SET_RAM_X_COUNTER    0x4E   /* Set RAM X address counter */
#define EPD_CMD_SET_RAM_Y_COUNTER    0x4F   /* Set RAM Y address counter */
#define EPD_CMD_DEEP_SLEEP           0x10   /* Deep Sleep mode */

/* 显存：1 bit/像素，1=白 0=黑。400/8*300 = 15000 字节 */
static uint8_t epd_buffer[EPD_BUF_SIZE];

/* ── 底层：命令 / 数据 / 复位 / 忙等待 ───────────────────────────────────────
 * 4 线 SPI：DC=0 收命令，DC=1 收数据；CS 低有效；Mode 0（CPOL=0,CPHA=0）MSB first
 * -------------------------------------------------------------------------*/

static void EPD_WriteByte(uint8_t data, uint8_t is_data)
{
    HAL_GPIO_WritePin(EPD_DC_GPIO_Port, EPD_DC_Pin,
                      is_data ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(EPD_CS_GPIO_Port, EPD_CS_Pin, GPIO_PIN_RESET);
    HAL_SPI_Transmit(&hspi1, &data, 1, HAL_MAX_DELAY);
    HAL_GPIO_WritePin(EPD_CS_GPIO_Port, EPD_CS_Pin, GPIO_PIN_SET);
}

static void EPD_SendCommand(uint8_t cmd)
{
    EPD_WriteByte(cmd, 0);
}

static void EPD_SendData(uint8_t data)
{
    EPD_WriteByte(data, 1);
}

/* 硬件复位：RST 高 → 低 → 高。手册要求低电平 >=10us，这里给足余量（2ms）*/
static void EPD_Reset(void)
{
    HAL_GPIO_WritePin(EPD_RST_GPIO_Port, EPD_RST_Pin, GPIO_PIN_SET);
    HAL_Delay(50);
    HAL_GPIO_WritePin(EPD_RST_GPIO_Port, EPD_RST_Pin, GPIO_PIN_RESET);
    HAL_Delay(2);
    HAL_GPIO_WritePin(EPD_RST_GPIO_Port, EPD_RST_Pin, GPIO_PIN_SET);
    HAL_Delay(50);
}

/* 等待 BUSY 变低（屏空闲）。
 * ★ BUSY 高=忙。4.2" 全刷最长约 2~4 秒，超时给 10 秒保护，
 *   避免 SPI/供电有问题时死等在这里（现象上就能区分"卡死"和"刷不出来"）。*/
static void EPD_ReadBusy(void)
{
    uint32_t tickstart = HAL_GetTick();
    while (HAL_GPIO_ReadPin(EPD_BUSY_GPIO_Port, EPD_BUSY_Pin) == GPIO_PIN_SET) {
        if (HAL_GetTick() - tickstart > 10000) {
            break;
        }
    }
}

/* 设定 RAM 写入窗口（X 以 8 像素为单位，Y 为像素） */
static void EPD_SetWindows(uint16_t xs, uint16_t ys, uint16_t xe, uint16_t ye)
{
    EPD_SendCommand(EPD_CMD_SET_RAM_X_RANGE);
    EPD_SendData((xs >> 3) & 0xFF);
    EPD_SendData((xe >> 3) & 0xFF);

    EPD_SendCommand(EPD_CMD_SET_RAM_Y_RANGE);
    EPD_SendData(ys & 0xFF);
    EPD_SendData((ys >> 8) & 0xFF);
    EPD_SendData(ye & 0xFF);
    EPD_SendData((ye >> 8) & 0xFF);
}

/* 设定 RAM 写入光标起点 */
static void EPD_SetCursor(uint16_t xs, uint16_t ys)
{
    EPD_SendCommand(EPD_CMD_SET_RAM_X_COUNTER);
    EPD_SendData((xs >> 3) & 0xFF);

    EPD_SendCommand(EPD_CMD_SET_RAM_Y_COUNTER);
    EPD_SendData(ys & 0xFF);
    EPD_SendData((ys >> 8) & 0xFF);
}

/* ── 初始化 ────────────────────────────────────────────────────────────────
 * 采用「极简初始化」：只做 SW RESET + 数据入口模式 + 窗口/光标。
 * 升压与波形全部使用 IC 的 OTP 出厂默认值 —— 参考项目实测这样即可正常显示，
 * 且避免了乱写 LUT 导致红闪/花屏。若后续显示异常，再考虑补 0x0C/0x3C 等配置。
 * -------------------------------------------------------------------------*/
void EPD_Init(void)
{
    EPD_Reset();
    EPD_ReadBusy();

    /* 软件复位 */
    EPD_SendCommand(EPD_CMD_SW_RESET);
    EPD_ReadBusy();

    /* 数据入口模式：0x03 = X 递增、Y 递增（与我们显存的排布一致）*/
    EPD_SendCommand(EPD_CMD_DATA_ENTRY_MODE);
    EPD_SendData(0x03);

    /* 全屏窗口 + 光标归零 */
    EPD_SetWindows(0, 0, EPD_WIDTH - 1, EPD_HEIGHT - 1);
    EPD_SetCursor(0, 0);

    EPD_ReadBusy();

    /* 显存初始化成白色 */
    EPD_Clear(1);
}

/* ── 把显存推到屏上并刷新 ────────────────────────────────────────────────────*/
void EPD_Display(void)
{
    /* BW RAM：我们的显存（1=白 0=黑）*/
    EPD_SendCommand(EPD_CMD_WRITE_RAM_BW);
    for (uint32_t i = 0; i < EPD_BUF_SIZE; i++) {
        EPD_SendData(epd_buffer[i]);
    }

    /* RED RAM：黑白屏不用红色，填 0x00（无红）*/
    EPD_SendCommand(EPD_CMD_WRITE_RAM_RED);
    for (uint32_t i = 0; i < EPD_BUF_SIZE; i++) {
        EPD_SendData(0x00);
    }

    /* 触发刷新：0x22 写入更新序列，0x20 Master Activation 执行 */
    EPD_SendCommand(EPD_CMD_DISPLAY_UPDATE_CTRL2);
    EPD_SendData(0xF7);                  /* 全刷序列（含 LUT 载入 + 显示）*/
    EPD_SendCommand(EPD_CMD_MASTER_ACTIVATION);
    EPD_ReadBusy();
}

/* ── 进深度睡眠 ────────────────────────────────────────────────────────────
 * 深度睡眠后屏断电保持画面（墨水屏特性），静态显示不耗电。
 * 唤醒需要重新上电或硬件复位。*/
void EPD_Sleep(void)
{
    EPD_SendCommand(EPD_CMD_DEEP_SLEEP);
    EPD_SendData(0x01);
}

/* ── 显存绘图 ──────────────────────────────────────────────────────────────*/
void EPD_Clear(uint8_t color)
{
    memset(epd_buffer, color ? 0xFF : 0x00, EPD_BUF_SIZE);
}

void EPD_DrawPixel(int16_t x, int16_t y, uint8_t color)
{
    if (x < 0 || x >= EPD_WIDTH || y < 0 || y >= EPD_HEIGHT) return;

    uint16_t idx = (uint16_t)y * (EPD_WIDTH / 8) + (uint16_t)x / 8;
    uint8_t  bit = 0x80 >> (x % 8);
    if (color) {
        epd_buffer[idx] |= bit;
    } else {
        epd_buffer[idx] &= (uint8_t)~bit;
    }
}

void EPD_DrawHLine(int16_t x, int16_t y, int16_t w, uint8_t color)
{
    for (int16_t i = 0; i < w; i++) {
        EPD_DrawPixel(x + i, y, color);
    }
}

void EPD_DrawVLine(int16_t x, int16_t y, int16_t h, uint8_t color)
{
    for (int16_t i = 0; i < h; i++) {
        EPD_DrawPixel(x, y + i, color);
    }
}

void EPD_DrawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color)
{
    EPD_DrawHLine(x, y, w, color);
    EPD_DrawHLine(x, y + h - 1, w, color);
    EPD_DrawVLine(x, y, h, color);
    EPD_DrawVLine(x + w - 1, y, h, color);
}

void EPD_FillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color)
{
    for (int16_t j = 0; j < h; j++) {
        for (int16_t i = 0; i < w; i++) {
            EPD_DrawPixel(x + i, y + j, color);
        }
    }
}

/* ── 文字绘制（ASCII 8x16，字模见 font.c）────────────────────────────────────
 * 字模格式：16 字节 = 上半8行(8B) + 下半8行(8B)，每字节一列，bit0 在最上。
 * 画法：逐列取出字节，逐位判断该像素是否点亮，再写入显存。
 * -------------------------------------------------------------------------*/
int16_t EPD_DrawChar(int16_t x, int16_t y, uint8_t ch, uint8_t color)
{
    const uint8_t *dots = FONT_GetAscii(ch);
    if (dots == 0) {
        return FONT_ASCII_W;          /* 不支持的字符：留空位 */
    }

    for (int16_t col = 0; col < FONT_ASCII_W; col++) {
        /* 上半 8 行 */
        uint8_t byte_top = dots[col];
        /* 下半 8 行（数据在 +8 偏移处） */
        uint8_t byte_bot = dots[col + FONT_ASCII_W];

        for (int16_t row = 0; row < 8; row++) {
            /* bit0 在最上 → 第 row 行对应 bit(row) */
            if (byte_top & (1 << row)) {
                EPD_DrawPixel(x + col, y + row, color);
            }
            if (byte_bot & (1 << row)) {
                EPD_DrawPixel(x + col, y + 8 + row, color);
            }
        }
    }
    return FONT_ASCII_W;
}

/* ── 中文字库绘制 ────────────────────────────────────────────────────────────
 * 点阵格式与 font.c 相同：每字节一列 8 像素、bit0 在上。汉字 16x16 = 32 字节
 * （上半 16 列 + 下半 16 列）。
 * 注：文本的测量/折行/居中由 textlayout.c 负责，驱动层只管「把码点画到坐标」。
 * -------------------------------------------------------------------------*/

/* 画一个「缺字」提示框：空心方框，让人一眼看出字库里没有这个字 */
static void EPD_DrawMissingGlyph(int16_t x, int16_t y, uint8_t color)
{
    EPD_DrawRect(x, y, FONT_CN_W, FONT_CN_H, color);
}

int16_t EPD_DrawChinese(int16_t x, int16_t y, uint16_t code, uint8_t color)
{
    const uint8_t *dots = FONT_GetChinese(code);
    if (dots == 0) {
        EPD_DrawMissingGlyph(x, y, color);
        return FONT_CN_W;
    }

    for (int16_t col = 0; col < FONT_CN_W; col++) {
        uint8_t byte_top = dots[col];                  /* 上半 8 行（列 0..15） */
        uint8_t byte_bot = dots[col + FONT_CN_W];      /* 下半 8 行（列 16..31） */
        for (int16_t row = 0; row < 8; row++) {
            if (byte_top & (1 << row)) EPD_DrawPixel(x + col, y + row, color);
            if (byte_bot & (1 << row)) EPD_DrawPixel(x + col, y + 8 + row, color);
        }
    }
    return FONT_CN_W;
}

/* ── 硬件自检：4 张测试画面 ──────────────────────────────────────────────────
 * 每张停 3 秒。判读方法：
 *   ① 全白刷不出来        → SPI 接线 / CS / 供电 / BS1 模式
 *   ② 白黑颠倒            → 显存极性（把 EPD_Display 里的数据取反）
 *   ③ 四角/边框偏位       → 分辨率或 RAM 窗口设置
 *   ④ 竖条纹变横纹        → 显存行对齐问题
 * -------------------------------------------------------------------------*/
void EPD_TestPattern(void)
{
    /* ★ 必须先初始化：复位屏 + SW RESET + 设数据入口/窗口/光标。
     *   漏这一步，EPD_Display() 发出的命令屏根本不会理（RST 还停在低电平）。*/
    EPD_Init();

    /* ① 全白 */
    LOG(">>> [1/4] 全白\r\n");
    EPD_Clear(1);
    EPD_Display();
    HAL_Delay(3000);

    /* ② 全黑 */
    LOG(">>> [2/4] 全黑\r\n");
    EPD_Clear(0);
    EPD_Display();
    HAL_Delay(3000);

    /* ③ 四角 + 中心黑块 + 外框 */
    LOG(">>> [3/4] 四角+外框\r\n");
    EPD_Clear(1);
    EPD_FillRect(0,   0,   20, 20, 0);
    EPD_FillRect(380, 0,   20, 20, 0);
    EPD_FillRect(0,   280, 20, 20, 0);
    EPD_FillRect(380, 280, 20, 20, 0);
    EPD_FillRect(190, 140, 20, 20, 0);
    EPD_DrawRect(0, 0, EPD_WIDTH, EPD_HEIGHT, 0);
    EPD_Display();
    HAL_Delay(3000);

    /* ④ 竖条纹（每 8 像素一个字节，0xF0/0x0F 交替）*/
    LOG(">>> [4/4] 竖条纹\r\n");
    uint8_t *buf = epd_buffer;
    for (int16_t y = 0; y < EPD_HEIGHT; y++) {
        for (int16_t xb = 0; xb < EPD_WIDTH / 8; xb++) {
            buf[y * (EPD_WIDTH / 8) + xb] = (xb & 1) ? 0x0F : 0xF0;
        }
    }
    EPD_Display();
    HAL_Delay(3000);

    /* 收尾：全白 + 睡眠 */
    LOG(">>> 自检结束，进睡眠\r\n");
    EPD_Clear(1);
    EPD_Display();
    EPD_Sleep();
}
