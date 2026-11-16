# Foot sensor board firmware: integration steps

Copy the `App/` and `tools/` folders into your `SensorBoardFW` project root
(next to `Core/`, `Drivers/`, `CMakeLists.txt`).

## 1. CMakeLists.txt (project root)

Find the two "user" blocks and add the lines marked `+`:

```cmake
# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user sources here
+   App/Src/app.c
+   App/Src/cli.c
+   App/Src/config_store.c
+   App/Src/control_table.c
+   App/Src/dxl_protocol.c
+   App/Src/dxl_slave.c
+   App/Src/fsr_adc.c
+   App/Src/platform.c
+   App/Src/rs485.c
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
+   App/Inc
)
```

## 2. STM32G431xx_FLASH.ld (project root)

Reserve the last 2 KB flash page for the saved ID/baud:

```
FLASH    (rx)    : ORIGIN = 0x8000000,   LENGTH = 128K
```
change `128K` to `126K`:
```
FLASH    (rx)    : ORIGIN = 0x8000000,   LENGTH = 126K
```

## 3. Core/Src/main.c

```c
/* USER CODE BEGIN Includes */
#include "app.h"
/* USER CODE END Includes */
```
```c
  /* USER CODE BEGIN 2 */
  app_init();
  /* USER CODE END 2 */
```
```c
    /* USER CODE BEGIN 3 */
    app_loop();
  }
  /* USER CODE END 3 */
```

## 4. Core/Src/stm32g4xx_it.c

```c
/* USER CODE BEGIN Includes */
#include "rs485.h"
/* USER CODE END Includes */
```
Inside `void DMA1_Channel1_IRQHandler(void)`:
```c
  /* USER CODE BEGIN DMA1_Channel1_IRQn 0 */
  rs485_dma_rx_irq();
  /* USER CODE END DMA1_Channel1_IRQn 0 */
```
Inside `void USART1_IRQHandler(void)`:
```c
  /* USER CODE BEGIN USART1_IRQn 0 */
  rs485_usart_irq();
  /* USER CODE END USART1_IRQn 0 */
```

All edits sit inside USER CODE blocks, so re-generating from CubeMX keeps them.
The linker script edit (step 2) is NOT preserved if CubeMX regenerates the
.ld file. Re-check it after every regeneration. `config_save()` refuses to
erase if the firmware ever reaches the config page, so a missed edit can't
brick the board, but saving would fail (status flag FLASH_ERROR).

## 5. Build, flash, test

1. Build (status bar **Build**). Expect no errors.
2. Flash with ST-LINK on J2 (SWD).
3. USB-C debug port, 921600 baud (e.g. `screen /dev/tty.usbserial-* 921600`).
   You should see the banner. Type `help`, `info`, `read`, `stream`.
4. Set IDs **before** both boards share a bus: left foot `id 30`, right foot `id 31`.
5. On the Pi: `python3 tools/fsr_test.py --id 30`

## Register map (read 12 bytes from address 128 each control cycle)

| Addr | Size | Access | Content |
|---|---|---|---|
| 0 | 2 | R | Model number = 9000 |
| 6 | 1 | R | Firmware version |
| 7 | 1 | RW | ID (saved) |
| 8 | 1 | RW | Baud index (saved), 7 = 4.5 Mbps |
| 9 | 1 | RW | Return delay ×2 µs (saved), default 0 |
| 65 | 1 | RW | LED: 0 = status pattern, 1 = on (identify board) |
| 68 | 1 | RW | Status return level (default 2) |
| 70 | 1 | R | Status flags: b0 default config, b1 ADC stalled, b2 flash error, b3 ADC start fail |
| 128–135 | 2×4 | R | FSR0–FSR3 raw 12-bit |
| 136 | 4 | R | Sample counter (1 kHz) |
| 140, 142 | 2 | R | Reserved (VREFINT / temperature option) |
| 144 | 2 | R | Packets handled |
| 146 | 2 | R | CRC errors |

## Known limits

- Answers PING/READ/WRITE/REBOOT/FACTORY_RESET by its own ID. Ignores
  SYNC/BULK READ (the Pi must use plain READ) and broadcast PING.
- Saving a new ID/baud erases flash (~20 ms). Bus replies are delayed during
  that time, so one Pi read may time out. This only happens when config changes.
- Raising the poll rate on Linux: set the FTDI latency timer to 1 ms:
  `echo 1 | sudo tee /sys/bus/usb-serial/devices/ttyUSB0/latency_timer`
