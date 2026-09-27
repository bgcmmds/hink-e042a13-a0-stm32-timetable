/**
  ******************************************************************************
  * @file    epd_test.c
  * @brief   墨水屏诊断自检（调试工具，正式固件默认不编译）
  *
  *          启用方式：cmake 配置时加 -DENABLE_DEBUG_TOOLS=ON，
  *          然后在 main.c 里调用 EPD_Diag()。
  *
  *          为什么单独放 debug/ 目录：它为了打点，直接操作 SPI/GPIO，
  *          绕过 epd.c 的封装（同时也重复了一套收发逻辑）。这属于「排障手段」，
  *          不该混进正式驱动层 —— 分开后 epd.c 保持干净，排查时再打开开关。
  *
  *          每一个动作都会打点，包括：
  *            - 各控制引脚的实际电平
  *            - BUSY 等待的实际耗时（正常全刷 2~4 秒；恒为超时 = BUSY 一直高）
  *            - SPI 是否发送成功
  ******************************************************************************
  */
#include "epd.h"
#include "epd_test.h"
#include "log.h"
#include "main.h"
#include <string.h>

extern SPI_HandleTypeDef hspi1;

/* 读引脚电平，返回 0/1 */
static int pin( GPIO_TypeDef *port, uint16_t pin )
{
    return HAL_GPIO_ReadPin(port, pin) == GPIO_PIN_SET ? 1 : 0;
}

/* 打印当前所有控制引脚电平 —— 接线是否正确一眼可见 */
static void dump_pins(const char *tag)
{
    LOGF("[PIN %s] DC=%d CS=%d RST=%d BUSY=%d\r\n", tag,
         pin(EPD_DC_GPIO_Port,   EPD_DC_Pin),
         pin(EPD_CS_GPIO_Port,   EPD_CS_Pin),
         pin(EPD_RST_GPIO_Port,  EPD_RST_Pin),
         pin(EPD_BUSY_GPIO_Port, EPD_BUSY_Pin));
}

/* 等 BUSY 变低，返回耗时(ms)；超时返回负值 */
static int wait_busy(const char *tag, uint32_t timeout_ms)
{
    uint32_t t0 = HAL_GetTick();
    while (pin(EPD_BUSY_GPIO_Port, EPD_BUSY_Pin)) {
        if (HAL_GetTick() - t0 > timeout_ms) {
            LOGF("[BUSY %s] *** 超时 %lums，BUSY 一直为高 ***\r\n",
                 tag, (unsigned long)(HAL_GetTick() - t0));
            return -1;
        }
    }
    LOGF("[BUSY %s] OK, 等待 %lums\r\n", tag,
         (unsigned long)(HAL_GetTick() - t0));
    return (int)(HAL_GetTick() - t0);
}

/* 发命令（带 SP 返回状态检查） */
static int send_cmd(uint8_t cmd)
{
    HAL_StatusTypeDef st;
    HAL_GPIO_WritePin(EPD_DC_GPIO_Port, EPD_DC_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(EPD_CS_GPIO_Port, EPD_CS_Pin, GPIO_PIN_RESET);
    st = HAL_SPI_Transmit(&hspi1, &cmd, 1, 100);
    HAL_GPIO_WritePin(EPD_CS_GPIO_Port, EPD_CS_Pin, GPIO_PIN_SET);
    return (st == HAL_OK) ? 0 : -1;
}

static int send_data(uint8_t d)
{
    HAL_StatusTypeDef st;
    HAL_GPIO_WritePin(EPD_DC_GPIO_Port, EPD_DC_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(EPD_CS_GPIO_Port, EPD_CS_Pin, GPIO_PIN_RESET);
    st = HAL_SPI_Transmit(&hspi1, &d, 1, 100);
    HAL_GPIO_WritePin(EPD_CS_GPIO_Port, EPD_CS_Pin, GPIO_PIN_SET);
    return (st == HAL_OK) ? 0 : -1;
}

/* ── 诊断主流程 ─────────────────────────────────────────────────────────── */
void EPD_Diag(void)
{
    LOG("\r\n\r\n");
    LOG("========================================\r\n");
    LOG(" EPD DIAG START\r\n");
    LOG("========================================\r\n");
    LOGF("SYSCLK=%lu Hz\r\n", (unsigned long)HAL_RCC_GetSysClockFreq());
    LOGF("HCLK=%lu  PCLK1=%lu  PCLK2=%lu\r\n",
         (unsigned long)HAL_RCC_GetHCLKFreq(),
         (unsigned long)HAL_RCC_GetPCLK1Freq(),
         (unsigned long)HAL_RCC_GetPCLK2Freq());

    /* --- 1. 上电初始电平（还没动过任何引脚） --- */
    dump_pins("上电初始");

    /* --- 2. 硬件复位 --- */
    LOG("---> 硬件复位 RST 高低高\r\n");
    HAL_GPIO_WritePin(EPD_RST_GPIO_Port, EPD_RST_Pin, GPIO_PIN_SET);
    HAL_Delay(50);
    dump_pins("RST=H");
    HAL_GPIO_WritePin(EPD_RST_GPIO_Port, EPD_RST_Pin, GPIO_PIN_RESET);
    HAL_Delay(2);
    dump_pins("RST=L");
    HAL_GPIO_WritePin(EPD_RST_GPIO_Port, EPD_RST_Pin, GPIO_PIN_SET);
    HAL_Delay(50);
    dump_pins("RST=H(复位完)");

    /* --- 3. 等 BUSY（复位后屏应进入空闲） --- */
    wait_busy("复位后", 3000);

    /* --- 4. SW RESET (0x12) --- */
    LOG("---> 发 0x12 SW RESET\r\n");
    send_cmd(0x12);
    HAL_Delay(10);
    dump_pins("发出0x12后");
    wait_busy("SWRESET", 3000);

    /* --- 5. Data Entry Mode (0x11 <- 0x03) --- */
    LOG("---> 发 0x11 DataEntry=0x03\r\n");
    send_cmd(0x11);
    send_data(0x03);
    wait_busy("0x11", 1000);

    /* --- 6. 设窗口 --- */
    LOG("---> 发 0x44/0x45 设置窗口\r\n");
    send_cmd(0x44); send_data(0x00); send_data(49);          /* X: 0..399 -> /8 = 0..49 */
    send_cmd(0x45); send_data(0x00); send_data(0x00);        /* Y: 0 */
    send_data(0x2B); send_data(0x01);                        /* Y: 299 = 0x012B */
    send_cmd(0x4E); send_data(0x00);                         /* X 光标 */
    send_cmd(0x4F); send_data(0x00); send_data(0x00);        /* Y 光标 */
    wait_busy("窗口设置", 1000);

    /* --- 7. 写全白到 BW RAM --- */
    LOG("---> 写 0x24 BW RAM (全白 0xFF)\r\n");
    send_cmd(0x24);
    for (uint32_t i = 0; i < EPD_BUF_SIZE; i++) {
        send_data(0xFF);
    }
    LOG("     写完 15000 字节\r\n");

    /* --- 8. 写 RED RAM（黑白屏填 0） --- */
    LOG("---> 写 0x26 RED RAM (全 0x00)\r\n");
    send_cmd(0x26);
    for (uint32_t i = 0; i < EPD_BUF_SIZE; i++) {
        send_data(0x00);
    }
    LOG("     写完 15000 字节\r\n");

    /* --- 9. 触发刷新 --- */
    LOG("---> 发 0x22<-0xF7, 0x20 MasterActivation\r\n");
    send_cmd(0x22); send_data(0xF7);
    send_cmd(0x20);

    uint32_t t0 = HAL_GetTick();
    int r = wait_busy("REFRESH", 15000);
    uint32_t dt = HAL_GetTick() - t0;
    if (r >= 0) {
        LOGF(">>> 刷新完成，耗时 %lums <<<\r\n", (unsigned long)dt);
        LOG(">>> 屏上应该出现全白。若没变化，是屏侧问题（供电/SW2/接线）<<<\r\n");
    } else {
        LOG(">>> 刷新超时！BUSY 一直高 <<<\r\n");
        LOG(">>> 排查：1)转接板 3V3 是否有电压 2)SW2 是否拨到 GND 档 <<<\r\n");
    }

    dump_pins("结束");
    LOG("========================================\r\n");
    LOG(" EPD DIAG END\r\n");
    LOG("========================================\r\n");
}

/* ── 硬件自检：4 张测试画面 ──────────────────────────────────────────────────
 * 每张停 3 秒，靠肉眼判读：
 *   ① 全白刷不出来        → SPI 接线 / CS / 供电 / BS1 模式
 *   ② 白黑颠倒            → 显存极性（把数据取反）
 *   ③ 四角/边框偏位       → 分辨率或 RAM 窗口设置
 *   ④ 竖条纹变横纹        → 显存行对齐问题
 * -------------------------------------------------------------------------*/
void EPD_TestPattern(void)
{
    /* ★ 必须先初始化：复位屏 + SW RESET + 设数据入口/窗口/光标。
     *   漏这一步，EPD_Display() 发的命令屏根本不会理（RST 还停在低电平）。*/
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

    /* ③ 四角 + 中心黑块 + 外框：验证坐标系与边界 */
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

    /* ④ 竖条纹：8 像素一个周期（4 白 4 黑），验证显存位序与行对齐。
     *    用公开绘图接口画，不去动驱动内部的显存数组。 */
    LOG(">>> [4/4] 竖条纹\r\n");
    for (int xb = 0; xb < EPD_WIDTH / 8; xb++) {
        uint8_t color = (xb & 1) ? 0 : 1;            /* 交替起始极性 */
        EPD_FillRect(xb * 8,     0, 4, EPD_HEIGHT, color);
        EPD_FillRect(xb * 8 + 4, 0, 4, EPD_HEIGHT, !color);
    }
    EPD_Display();
    HAL_Delay(3000);

    /* 收尾：全白 + 睡眠 */
    LOG(">>> 自检结束，进睡眠\r\n");
    EPD_Clear(1);
    EPD_Display();
    EPD_Sleep();
}

