/*
	IR protocols example

	This example code is in the Public Domain (or CC0 licensed, at your option.)

	Unless required by applicable law or agreed to in writing, this
	software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
	CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_vfs.h"
#include "esp_spiffs.h"
#include "driver/rmt_tx.h"
#include "ir_nec_encoder.h"

#define EXAMPLE_IR_RESOLUTION_HZ 1000000 // 1MHz resolution, 1 tick = 1us

#if defined(M5STACK)
#define CONFIG_STACK 1
#elif defined(M5STICK)
#define CONFIG_STICK 1
#elif defined(M5STICK_C)
#define CONFIG_STICKC	1
#elif defined(M5STICK_C_PLUS)
#define CONFIG_STICKC_PLUS 1
#elif defined(M5STICK_C_PLUS2)
#define CONFIG_STICKC_PLUS2 1
#endif

#if CONFIG_STACK
#include "ili9340.h"
#include "fontx.h"
#endif

#if CONFIG_STICK
#include "sh1107.h"
#include "font8x8_basic.h"
#endif

#if CONFIG_STICKC
#include "axp192.h"
#include "st7735s.h"
#include "fontx.h"
#endif

#if CONFIG_STICKC_PLUS
#include "axp192.h"
#include "st7789.h"
#include "fontx.h"
#endif

#if CONFIG_STICKC_PLUS2
#include "sgm2578.h"
#include "st7789.h"
#include "fontx.h"
#endif

#if CONFIG_STACK
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240
#define CS_GPIO 14
#define DC_GPIO 27
#define RESET_GPIO 33
#define BL_GPIO 32
#define FONT_WIDTH 12
#define FONT_HEIGHT 24
#define MAX_CONFIG 20
#define MAX_LINE 8
#define MAX_CHARACTER 26
#define GPIO_INPUT_A GPIO_NUM_39
#define GPIO_INPUT_B GPIO_NUM_38
#define GPIO_INPUT_C GPIO_NUM_37
// GROVE PORT A
#define RMT_TX_GPIO_NUM	GPIO_NUM_21 /*!< GPIO number for transmitter signal */
// GROVE PORT B
//#define RMT_TX_GPIO_NUM GPIO_NUM_26 /*!< GPIO number for transmitter signal */
// GROVE PORT C
//#define RMT_TX_GPIO_NUM GPIO_NUM_17 /*!< GPIO number for transmitter signal */
#endif

#if CONFIG_STICK
#define MAX_CONFIG 14
#define MAX_LINE 14
#define MAX_CHARACTER 8
#define GPIO_INPUT GPIO_NUM_35
#define GPIO_BUZZER GPIO_NUM_26
#define RMT_TX_GPIO_NUM	GPIO_NUM_17 /*!< GPIO number for transmitter signal */
#endif

#if CONFIG_STICKC
#define SCREEN_WIDTH 80
#define SCREEN_HEIGHT 160
#define OFFSET_X 26
#define OFFSET_Y 1
#define GPIO_MOSI 15
#define GPIO_SCLK 13
#define GPIO_CS 5
#define GPIO_DC 23
#define GPIO_RESET 18
#define FONT_WIDTH 8
#define FONT_HEIGHT 16
#define MAX_CONFIG 20
#define MAX_LINE 8
#define MAX_CHARACTER 10
#define GPIO_INPUT_A GPIO_NUM_37
#define GPIO_INPUT_B GPIO_NUM_39
#define RMT_TX_GPIO_NUM	GPIO_NUM_9 /*!< GPIO number for transmitter signal */
#endif

#if CONFIG_STICKC_PLUS
#define SCREEN_WIDTH 135
#define SCREEN_HEIGHT 240
#define OFFSET_X 52
#define OFFSET_Y 40
#define GPIO_MOSI 15
#define GPIO_SCLK 13
#define GPIO_CS 5 
#define GPIO_DC 23
#define GPIO_RESET 18
#define GPIO_BL -1
#define FONT_WIDTH 8
#define FONT_HEIGHT 16
#define MAX_CONFIG 20
#define MAX_LINE 12
#define MAX_CHARACTER 16
#define GPIO_INPUT_A GPIO_NUM_37
#define GPIO_INPUT_B GPIO_NUM_39
#define RMT_TX_GPIO_NUM	GPIO_NUM_9 /*!< GPIO number for transmitter signal */
#endif

#if CONFIG_STICKC_PLUS2
#define SCREEN_WIDTH 135
#define SCREEN_HEIGHT 240
#define OFFSET_X 52
#define OFFSET_Y 40
#define GPIO_MOSI 15
#define GPIO_SCLK 13
#define GPIO_CS 5 
#define GPIO_DC 14
#define GPIO_RESET 12
#define GPIO_BL -1
#define FONT_WIDTH 8
#define FONT_HEIGHT 16
#define MAX_CONFIG 20
#define MAX_LINE 12
#define MAX_CHARACTER 16
#define GPIO_INPUT_A GPIO_NUM_37
#define GPIO_INPUT_B GPIO_NUM_39
#define RMT_TX_GPIO_NUM	GPIO_NUM_19 /*!< GPIO number for transmitter signal */
#endif

typedef enum {CMD_UP, CMD_DOWN, CMD_TOP, CMD_BOTTOM, CMD_SELECT} COMMAND;

QueueHandle_t xQueueCmd;

static const char *TAG = "M5Remote";

typedef struct {
	uint16_t command;
	TaskHandle_t taskHandle;
} CMD_t;

typedef struct {
	bool enable;
	char display_text[MAX_CHARACTER+1];
	uint16_t ir_cmd;
	uint16_t ir_addr;
} DISPLAY_t;


static void listSPIFFS(char * path) {
	DIR* dir = opendir(path);
	assert(dir != NULL);
	while (true) {
		struct dirent*pe = readdir(dir);
		if (!pe) break;
		ESP_LOGI(TAG,"d_name=%s d_ino=%d d_type=%x", pe->d_name,pe->d_ino, pe->d_type);
	}
	closedir(dir);
}

esp_err_t mountSPIFFS(char * path, char * label, int max_files) {
	esp_vfs_spiffs_conf_t conf = {
		.base_path = path,
		.partition_label = label,
		.max_files = max_files,
		.format_if_mount_failed = true
	};

	// Use settings defined above to initialize and mount SPIFFS filesystem.
	// Note: esp_vfs_spiffs_register is an all-in-one convenience function.
	esp_err_t ret = esp_vfs_spiffs_register(&conf);

	if (ret != ESP_OK) {
		if (ret ==ESP_FAIL) {
			ESP_LOGE(TAG, "Failed to mount or format filesystem");
		} else if (ret== ESP_ERR_NOT_FOUND) {
			ESP_LOGE(TAG, "Failed to find SPIFFS partition");
		} else {
			ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)",esp_err_to_name(ret));
		}
		return ret;
	}

#if 0
	ESP_LOGI(TAG, "Performing SPIFFS_check().");
	ret = esp_spiffs_check(conf.partition_label);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "SPIFFS_check() failed (%s)", esp_err_to_name(ret));
		return ret;
	} else {
			ESP_LOGI(TAG, "SPIFFS_check() successful");
	}
#endif

	size_t total = 0, used = 0;
	ret = esp_spiffs_info(conf.partition_label, &total, &used);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG,"Failed to get SPIFFS partition information (%s)",esp_err_to_name(ret));
	} else {
		ESP_LOGI(TAG,"Mount %s to %s success", path, label);
		ESP_LOGI(TAG,"Partition size: total: %d, used: %d", total, used);
	}

	return ret;
}

#if CONFIG_STICK
void buttonStick(void *pvParameters)
{
	ESP_LOGI(pcTaskGetName(NULL), "Start");
	CMD_t cmdBuf;
	cmdBuf.taskHandle = xTaskGetCurrentTaskHandle();

	// set the GPIO as a input
	//gpio_reset_pin(GPIO_INPUT);
	//gpio_set_direction(GPIO_INPUT, GPIO_MODE_DEF_INPUT);
	gpio_config_t io_conf = {};
	io_conf.mode = GPIO_MODE_INPUT;
	io_conf.pin_bit_mask = (1ULL<<GPIO_INPUT);
	gpio_config(&io_conf);

	while(1) {
		int level = gpio_get_level(GPIO_INPUT);
		if (level == 0) {
			ESP_LOGI(pcTaskGetName(NULL), "Push Button");
			TickType_t startTick = xTaskGetTickCount();
			while(1) {
				level = gpio_get_level(GPIO_INPUT);
				if (level == 1) break;
				vTaskDelay(1);
			}
			TickType_t endTick = xTaskGetTickCount();
			TickType_t diffTick = endTick-startTick;
			ESP_LOGI(pcTaskGetName(NULL),"diffTick=%"PRIu32, diffTick);
			cmdBuf.command = CMD_DOWN;
			if (diffTick > 100) cmdBuf.command = CMD_SELECT;
			xQueueSend(xQueueCmd, &cmdBuf, 0);
		}
		vTaskDelay(1);
	}
}
#endif

#if CONFIG_STICKC || CONFIG_STICKC_PLUS || CONFIG_STICKC_PLUS2 || CONFIG_STACK
void buttonA(void *pvParameters)
{
	ESP_LOGI(pcTaskGetName(NULL), "Start");
	CMD_t cmdBuf;
	cmdBuf.command = CMD_SELECT;
	cmdBuf.taskHandle = xTaskGetCurrentTaskHandle();

	// set the GPIO as a input
	//gpio_reset_pin(GPIO_INPUT_A);
	//gpio_set_direction(GPIO_INPUT_A, GPIO_MODE_DEF_INPUT);
	gpio_config_t io_conf = {};
	io_conf.mode = GPIO_MODE_INPUT;
	io_conf.pin_bit_mask = (1ULL<<GPIO_INPUT_A);
	gpio_config(&io_conf);

	while(1) {
		int level = gpio_get_level(GPIO_INPUT_A);
		if (level == 0) {
			ESP_LOGI(pcTaskGetName(NULL), "Push Button");
			while(1) {
				level = gpio_get_level(GPIO_INPUT_A);
				if (level == 1) break;
				vTaskDelay(1);
			}
			xQueueSend(xQueueCmd, &cmdBuf, 0);
		}
		vTaskDelay(1);
	}
}
#endif // CONFIG_STICKC || CONFIG_STICKC_PLUS || CONFIG_STICKC_PLUS2 || CONFIG_STACK

#if CONFIG_STICKC || CONFIG_STICKC_PLUS || CONFIG_STICKC_PLUS2
void buttonB(void *pvParameters)
{
	ESP_LOGI(pcTaskGetName(NULL), "Start");
	CMD_t cmdBuf;
	cmdBuf.taskHandle = xTaskGetCurrentTaskHandle();

	// set the GPIO as a input
	//gpio_reset_pin(GPIO_INPUT_B);
	//gpio_set_direction(GPIO_INPUT_B, GPIO_MODE_DEF_INPUT);
	gpio_config_t io_conf = {};
	io_conf.mode = GPIO_MODE_INPUT;
	io_conf.pin_bit_mask = (1ULL<<GPIO_INPUT_B);
	gpio_config(&io_conf);

	while(1) {
		int level = gpio_get_level(GPIO_INPUT_B);
		if (level == 0) {
			ESP_LOGI(pcTaskGetName(NULL), "Push Button");
			TickType_t startTick = xTaskGetTickCount();
			while(1) {
				level = gpio_get_level(GPIO_INPUT_B);
				if (level == 1) break;
				vTaskDelay(1);
			}
			TickType_t endTick = xTaskGetTickCount();
			TickType_t diffTick = endTick-startTick;
			ESP_LOGI(pcTaskGetName(NULL),"diffTick=%"PRIu32, diffTick);
			cmdBuf.command = CMD_DOWN;
			if (diffTick > 100) cmdBuf.command = CMD_TOP;
			xQueueSend(xQueueCmd, &cmdBuf, 0);
		}
		vTaskDelay(1);
	}
}
#endif // CONFIG_STICKC || CONFIG_STICKC_PLUS || CONFIG_STICKC_PLUS2

#if CONFIG_STACK
void buttonB(void *pvParameters)
{
	ESP_LOGI(pcTaskGetName(NULL), "Start");
	CMD_t cmdBuf;
	cmdBuf.taskHandle = xTaskGetCurrentTaskHandle();

	// set the GPIO as a input
	//gpio_reset_pin(GPIO_INPUT_B);
	//gpio_set_direction(GPIO_INPUT_B, GPIO_MODE_DEF_INPUT);
	gpio_config_t io_conf = {};
	io_conf.mode = GPIO_MODE_INPUT;
	io_conf.pin_bit_mask = (1ULL<<GPIO_INPUT_B);
	gpio_config(&io_conf);

	while(1) {
		int level = gpio_get_level(GPIO_INPUT_B);
		if (level == 0) {
			ESP_LOGI(pcTaskGetName(NULL), "Push Button");
			TickType_t startTick = xTaskGetTickCount();
			while(1) {
				level = gpio_get_level(GPIO_INPUT_B);
				if (level == 1) break;
				vTaskDelay(1);
			}
			TickType_t endTick = xTaskGetTickCount();
			TickType_t diffTick = endTick-startTick;
			ESP_LOGI(pcTaskGetName(NULL),"diffTick=%"PRIu32, diffTick);
			cmdBuf.command = CMD_DOWN;
			if (diffTick > 100) cmdBuf.command = CMD_BOTTOM;
			xQueueSend(xQueueCmd, &cmdBuf, 0);
		}
		vTaskDelay(1);
	}
}
#endif // CONFIG_STACK

#if CONFIG_STACK
void buttonC(void *pvParameters)
{
	ESP_LOGI(pcTaskGetName(NULL), "Start");
	CMD_t cmdBuf;
	cmdBuf.taskHandle = xTaskGetCurrentTaskHandle();

	// set the GPIO as a input
	//gpio_reset_pin(GPIO_INPUT_C);
	//gpio_set_direction(GPIO_INPUT_C, GPIO_MODE_DEF_INPUT);
	gpio_config_t io_conf = {};
	io_conf.mode = GPIO_MODE_INPUT;
	io_conf.pin_bit_mask = (1ULL<<GPIO_INPUT_C);
	gpio_config(&io_conf);

	while(1) {
		int level = gpio_get_level(GPIO_INPUT_C);
		if (level == 0) {
			ESP_LOGI(pcTaskGetName(NULL), "Push Button");
			TickType_t startTick = xTaskGetTickCount();
			while(1) {
				level = gpio_get_level(GPIO_INPUT_C);
				if (level == 1) break;
				vTaskDelay(1);
			}
			TickType_t endTick = xTaskGetTickCount();
			TickType_t diffTick = endTick-startTick;
			ESP_LOGI(pcTaskGetName(NULL),"diffTick=%"PRIu32, diffTick);
			cmdBuf.command = CMD_UP;
			if (diffTick > 100) cmdBuf.command = CMD_TOP;
			xQueueSend(xQueueCmd, &cmdBuf, 0);
		}
		vTaskDelay(1);
	}
}
#endif // CONFIG_STACK

static int parseLine(char *line, int size1, int size2, char arr[size1][size2])
{
	ESP_LOGD(TAG, "line=[%s]", line);
	int dst = 0;
	int pos = 0;
	int llen = strlen(line);
	bool inq = false;

	for(int src=0;src<llen;src++) {
		char c = line[src];
		ESP_LOGD(TAG, "src=%d c=%c", src, c);
		if (c == ',') {
			if (inq) {
				if (pos == (size2-1)) continue;
				arr[dst][pos++] = line[src];
				arr[dst][pos] = 0;
			} else {
				ESP_LOGD(TAG, "arr[%d]=[%s]",dst,arr[dst]);
				dst++;
				if (dst == size1) break;
				pos = 0;
			}

		} else if (c == ';') {
			if (inq) {
				if (pos == (size2-1)) continue;
				arr[dst][pos++] = line[src];
				arr[dst][pos] = 0;
			} else {
				ESP_LOGD(TAG, "arr[%d]=[%s]",dst,arr[dst]);
				dst++;
				break;
			}

		} else if (c == '"') {
			inq = !inq;

		} else if (c == '\'') {
			inq = !inq;

		} else {
			if (pos == (size2-1)) continue;
			arr[dst][pos++] = line[src];
			arr[dst][pos] = 0;
		}
	}

	return dst;
}

static int readDefineFile(DISPLAY_t *display, size_t maxLine, size_t maxText) {
	int readLine = 0;
	ESP_LOGI(pcTaskGetName(NULL), "Reading file:maxText=%d",maxText);
	FILE* f = fopen("/spiffs/Display.def", "r");
	if (f == NULL) {
			ESP_LOGE(pcTaskGetName(NULL), "Failed to open define file for reading");
			ESP_LOGE(pcTaskGetName(NULL), "Please make Display.def");
			return 0;
	}
	char line[64];
	char result[10][32];
	while (1){
		if ( fgets(line, sizeof(line) ,f) == 0 ) break;
		// strip newline
		char* pos = strchr(line, '\n');
		if (pos) {
			*pos = '\0';
		}
		ESP_LOGI(pcTaskGetName(NULL), "line=[%s]", line);
		if (strlen(line) == 0) continue;
		if (line[0] == '#') continue;

		int ret = parseLine(line, 10, 32, result);
		ESP_LOGI(TAG, "parseLine=%d", ret);
		for(int i=0;i<ret;i++) ESP_LOGI(TAG, "result[%d]=[%s]", i, &result[i][0]);
		display[readLine].enable = true;
		//strlcpy(display[readLine].display_text, &result[0][0], maxText);
		strlcpy(display[readLine].display_text, &result[0][0], maxText+1);
		display[readLine].ir_cmd = strtol(&result[1][0], NULL, 16);
		display[readLine].ir_addr = strtol(&result[2][0], NULL, 16);

		readLine++;
		if (readLine == maxLine) break;
	}
	fclose(f);
	return readLine;
}

void initializeRMT(rmt_channel_handle_t *tx_channel, rmt_encoder_handle_t *nec_encoder, rmt_transmit_config_t *transmit_config) {
	// Setup IR transmitter
	ESP_LOGI(TAG, "create RMT TX channel");
	rmt_tx_channel_config_t _tx_channel_cfg = {
		.clk_src = RMT_CLK_SRC_DEFAULT,
		.resolution_hz = EXAMPLE_IR_RESOLUTION_HZ,
		.mem_block_symbols = 64, // amount of RMT symbols that the channel can store at a time
		.trans_queue_depth = 4,  // number of transactions that allowed to pending in the background, this example won't queue multiple transactions, so queue depth > 1 is sufficient
		.gpio_num = RMT_TX_GPIO_NUM,
	};
	rmt_channel_handle_t _tx_channel = NULL;
	ESP_ERROR_CHECK(rmt_new_tx_channel(&_tx_channel_cfg, &_tx_channel));

	ESP_LOGI(TAG, "modulate carrier to TX channel");
	rmt_carrier_config_t _carrier_cfg = {
		.duty_cycle = 0.33,
		.frequency_hz = 38000, // 38KHz
	};
	ESP_ERROR_CHECK(rmt_apply_carrier(_tx_channel, &_carrier_cfg));

	// this example won't send NEC frames in a loop
	rmt_transmit_config_t _transmit_config = {
		.loop_count = 0, // no loop
	};

	ESP_LOGI(TAG, "install IR NEC encoder");
	ir_nec_encoder_config_t _nec_encoder_cfg = {
		.resolution = EXAMPLE_IR_RESOLUTION_HZ,
	};
	rmt_encoder_handle_t _nec_encoder = NULL;
	ESP_ERROR_CHECK(rmt_new_ir_nec_encoder(&_nec_encoder_cfg, &_nec_encoder));

	ESP_LOGI(TAG, "enable RMT TX channels");
	ESP_ERROR_CHECK(rmt_enable(_tx_channel));

	*tx_channel = _tx_channel;
	*nec_encoder = _nec_encoder;
	*transmit_config = _transmit_config;
}

#if CONFIG_STICKC || CONFIG_STICKC_PLUS || CONFIG_STICKC_PLUS2 || CONFIG_STACK
void tft(void *pvParameters)
{
	// set font file
#if CONFIG_STICKC || CONFIG_STICKC_PLUS || CONFIG_STICKC_PLUS2
	FontxFile fxG[2];
	InitFontx(fxG,"/spiffs/ILGH16XB.FNT",""); // 8x16Dot Gothic
	FontxFile fxM[2];
	InitFontx(fxM,"/spiffs/ILMH16XB.FNT",""); // 8x16Dot Mincyo
#endif

#if CONFIG_STACK
	FontxFile fxG[2];
	InitFontx(fxG,"/spiffs/ILGH24XB.FNT",""); // 12x24Dot Gothic
	FontxFile fxM[2];
	InitFontx(fxM,"/spiffs/ILMH24XB.FNT",""); // 12x24Dot Mincyo
#endif

	// Setup IR transmitter
	rmt_channel_handle_t tx_channel = NULL;
	rmt_encoder_handle_t nec_encoder = NULL;
	rmt_transmit_config_t transmit_config = {};
	initializeRMT(&tx_channel, &nec_encoder, &transmit_config);

	// Setup Screen
#if CONFIG_STICKC
	ST7735_t dev;
	spi_master_init(&dev, GPIO_MOSI, GPIO_SCLK, GPIO_CS, GPIO_DC, GPIO_RESET);
	lcdInit(&dev, SCREEN_WIDTH, SCREEN_HEIGHT, OFFSET_X, OFFSET_Y);
#endif

#if CONFIG_STICKC_PLUS || CONFIG_STICKC_PLUS2
	TFT_t dev;
	spi_master_init(&dev, GPIO_MOSI, GPIO_SCLK, GPIO_CS, GPIO_DC, GPIO_RESET, GPIO_BL);
	lcdInit(&dev, SCREEN_WIDTH, SCREEN_HEIGHT, OFFSET_X, OFFSET_Y);
#endif

#if CONFIG_STACK
	TFT_t dev;
	spi_master_init(&dev, CS_GPIO, DC_GPIO, RESET_GPIO, BL_GPIO);
	lcdInit(&dev, 0x9341, SCREEN_WIDTH, SCREEN_HEIGHT, 0, 0);
#endif
	ESP_LOGI(pcTaskGetName(NULL), "Setup Screen done");

	// Read display information
	DISPLAY_t display[MAX_CONFIG];
	for(int i=0;i<MAX_CONFIG;i++) display[i].enable = false;
	int readLine = readDefineFile(display, MAX_CONFIG, MAX_CHARACTER);
	ESP_LOGI(pcTaskGetName(NULL), "readLine=%d",readLine);
	if (readLine == 0) {
		while(1) { vTaskDelay(1); }
	}
	for(int i=0;i<readLine;i++) {
		ESP_LOGI(pcTaskGetName(NULL), "display[%d].display_text=[%s]",i, display[i].display_text);
		ESP_LOGI(pcTaskGetName(NULL), "display[%d].ir_cmd=[0x%02x]",i, display[i].ir_cmd);
		ESP_LOGI(pcTaskGetName(NULL), "display[%d].ir_addr=[0x%02x]",i, display[i].ir_addr);
	}

	// Initial Screen
	uint16_t color;
	uint8_t ascii[MAX_CHARACTER+1];
	uint16_t ypos;
	lcdFillScreen(&dev, BLACK);
	color = RED;
	lcdSetFontDirection(&dev, 0);
	ypos = FONT_HEIGHT-1;
#if CONFIG_STICKC
	strcpy((char *)ascii, "M5 StickC");
#endif
#if CONFIG_STICKC_PLUS
	strcpy((char *)ascii, "M5 StickC+");
#endif
#if CONFIG_STICKC_PLUS2
	strcpy((char *)ascii, "M5 StickC+2");
#endif
#if CONFIG_STACK
	strcpy((char *)ascii, "M5 Stack");
#endif
	lcdDrawString(&dev, fxG, 0, ypos, ascii, color);

	int offset = 0;
	for(int i=0;i<MAX_LINE;i++) {
		ypos = FONT_HEIGHT * (i+3) - 1;
		ascii[0] = 0;
		if (display[i+offset].enable) strcpy((char *)ascii, display[i+offset].display_text);
		if (i == 0) {
			lcdDrawString(&dev, fxG, 0, ypos, ascii, YELLOW);
		} else {
			lcdDrawString(&dev, fxG, 0, ypos, ascii, CYAN);
		}
	}

	int selected = 0;
	CMD_t cmdBuf;
	while(1) {
		xQueueReceive(xQueueCmd, &cmdBuf, portMAX_DELAY);
		ESP_LOGI(pcTaskGetName(NULL),"cmdBuf.command=%d", cmdBuf.command);
		if (cmdBuf.command == CMD_DOWN) {
			ESP_LOGI(pcTaskGetName(NULL), "selected=%d offset=%d readLine=%d",selected, offset, readLine);
			if ((selected+offset+1) == readLine) continue;

			ypos = FONT_HEIGHT * (selected+3) - 1;
			strcpy((char *)ascii, display[selected+offset].display_text);
			lcdDrawString(&dev, fxG, 0, ypos, ascii, CYAN);

			// Scroll Down
			if (selected+1 == MAX_LINE) {
				lcdDrawFillRect(&dev, 0, FONT_HEIGHT-1, SCREEN_WIDTH-1, SCREEN_HEIGHT-1, BLACK);
				offset++;
				for(int i=0;i<MAX_LINE;i++) {
					ypos = FONT_HEIGHT * (i+3) - 1;
					ascii[0] = 0;
					if (display[i+offset].enable) strcpy((char *)ascii, display[i+offset].display_text);
					lcdDrawString(&dev, fxG, 0, ypos, ascii, CYAN);
				}
			} else {
				selected++;
			}

			ypos = FONT_HEIGHT * (selected+3) - 1;
			strcpy((char *)ascii, display[selected+offset].display_text);
			//lcdDrawString(&dev, fxG, 0, ypos, ascii, BLACK);
			lcdDrawString(&dev, fxG, 0, ypos, ascii, YELLOW);

		} else if (cmdBuf.command == CMD_UP) {
			ESP_LOGI(pcTaskGetName(NULL), "selected=%d offset=%d",selected, offset);
			if (selected+offset == 0) continue;

			ypos = FONT_HEIGHT * (selected+3) - 1;
			strcpy((char *)ascii, display[selected+offset].display_text);
			lcdDrawString(&dev, fxG, 0, ypos, ascii, CYAN);

			// Scroll Up
			if (offset > 0) {
				lcdDrawFillRect(&dev, 0, FONT_HEIGHT-1, SCREEN_WIDTH-1, SCREEN_HEIGHT-1, BLACK);
				offset--;
				for(int i=0;i<MAX_LINE;i++) {
					ypos = FONT_HEIGHT * (i+3) - 1;
					ascii[0] = 0;
					if (display[i+offset].enable) strcpy((char *)ascii, display[i+offset].display_text);
					lcdDrawString(&dev, fxG, 0, ypos, ascii, CYAN);
				}
			} else {
				selected--;
			}
			ypos = FONT_HEIGHT * (selected+3) - 1;
			strcpy((char *)ascii, display[selected+offset].display_text);
			//lcdDrawString(&dev, fxG, 0, ypos, ascii, BLACK);
			lcdDrawString(&dev, fxG, 0, ypos, ascii, YELLOW);

		} else if (cmdBuf.command == CMD_TOP) {
			offset = 0;
			selected = 0;
			lcdDrawFillRect(&dev, 0, FONT_HEIGHT-1, SCREEN_WIDTH-1, SCREEN_HEIGHT-1, BLACK);
			for(int i=0;i<MAX_LINE;i++) {
				ypos = FONT_HEIGHT * (i+3) - 1;
				ascii[0] = 0;
				if (display[i+offset].enable) strcpy((char *)ascii, display[i+offset].display_text);
				if (i == 0) {
					lcdDrawString(&dev, fxG, 0, ypos, ascii, YELLOW);
				} else {
					lcdDrawString(&dev, fxG, 0, ypos, ascii, CYAN);
				}
			}

		} else if (cmdBuf.command == CMD_BOTTOM) {
			ESP_LOGI(pcTaskGetName(NULL), "readLine=%d MAX_LINE=%d",readLine, MAX_LINE );
			offset = 0;
			selected = readLine-1;
			if (readLine > MAX_LINE) {
				offset = readLine - MAX_LINE;
				selected = MAX_LINE - 1;
			}
			ESP_LOGI(pcTaskGetName(NULL), "selected=%d offset=%d",selected, offset);
			lcdDrawFillRect(&dev, 0, FONT_HEIGHT-1, SCREEN_WIDTH-1, SCREEN_HEIGHT-1, BLACK);
			for(int i=0;i<MAX_LINE;i++) {
				ypos = FONT_HEIGHT * (i+3) - 1;
				ascii[0] = 0;
				if (display[i+offset].enable) strcpy((char *)ascii, display[i+offset].display_text);
				//if (i == 0) {
				if (i+offset+1 == readLine) {
					lcdDrawString(&dev, fxG, 0, ypos, ascii, YELLOW);
				} else {
					lcdDrawString(&dev, fxG, 0, ypos, ascii, CYAN);
				}
			}

		} else if (cmdBuf.command == CMD_SELECT) {
			ESP_LOGI(pcTaskGetName(NULL), "selected=%d offset=%d",selected, offset);
			ESP_LOGI(pcTaskGetName(NULL), "ir_cmd=0x%02x",display[selected+offset].ir_cmd);
			ESP_LOGI(pcTaskGetName(NULL), "ir_addr=0x%02x",display[selected+offset].ir_addr);
			uint16_t cmd = display[selected+offset].ir_cmd;
			uint16_t addr = display[selected+offset].ir_addr;;
			cmd = ((~cmd) << 8) |  cmd; // Reverse cmd + cmd
			addr = ((~addr) << 8) | addr; // Reverse addr + addr
			ESP_LOGI(pcTaskGetName(NULL), "cmd=0x%x",cmd);
			ESP_LOGI(pcTaskGetName(NULL), "addr=0x%x",addr);

			// transmit IR NEC packets
			const ir_nec_scan_code_t scan_code = {
				.address = addr,
				.command = cmd,
			};
			ESP_ERROR_CHECK(rmt_transmit(tx_channel, nec_encoder, &scan_code, sizeof(scan_code), &transmit_config));
		}
	} // end while

	// nerver reach here
	vTaskDelete(NULL);
}
#endif // CONFIG_STICKC || CONFIG_STICKC_PLUS || CONFIG_STICKC_PLUS2 || CONFIG_STACK


#if CONFIG_STICK
void buzzerON(void) {
	gpio_reset_pin( GPIO_BUZZER );
	gpio_set_direction( GPIO_BUZZER, GPIO_MODE_OUTPUT );
	gpio_set_level( GPIO_BUZZER, 0 );
	for(int i=0;i<50;i++){
		gpio_set_level( GPIO_BUZZER, 1 );
		vTaskDelay(1);
		gpio_set_level( GPIO_BUZZER, 0 );
		vTaskDelay(1);
	}
}

void tft(void *pvParameters)
{
	// Setup IR transmitter
	rmt_channel_handle_t tx_channel = NULL;
	rmt_encoder_handle_t nec_encoder = NULL;
	rmt_transmit_config_t transmit_config = {};
	initializeRMT(&tx_channel, &nec_encoder, &transmit_config);

	// Setup Screen
	SH1107_t dev;
	spi_master_init(&dev);
	spi_init(&dev, 64, 128);
	ESP_LOGI(pcTaskGetName(NULL), "Setup Screen done");

	// Read display information
	DISPLAY_t display[MAX_CONFIG];
	for(int i=0;i<MAX_CONFIG;i++) display[i].enable = false;
	int readLine = readDefineFile(display, MAX_CONFIG, MAX_CHARACTER);
	if (readLine == 0) {
		while(1) { vTaskDelay(1); }
	}
	for(int i=0;i<readLine;i++) {
		ESP_LOGI(pcTaskGetName(NULL), "display[%d].display_text=[%s]",i, display[i].display_text);
		ESP_LOGI(pcTaskGetName(NULL), "display[%d].ir_cmd=[0x%02x]",i, display[i].ir_cmd);
		ESP_LOGI(pcTaskGetName(NULL), "display[%d].ir_addr=[0x%02x]",i, display[i].ir_addr);
	}

	// Initial Screen
	clear_screen(&dev, false);
	display_contrast(&dev, 0xff);
	char ascii[MAX_CHARACTER+1];
	uint16_t ypos;
	strcpy(ascii, "M5 Stick");
	display_text(&dev, 0, ascii, 8, false);

	for(int i=0;i<MAX_LINE;i++) {
		ypos = i + 2;
		ascii[0] = 0;
		if (display[i].enable) strcpy(ascii, display[i].display_text);
		if (i == 0) {
			display_text(&dev, ypos, ascii, strlen(ascii), true);
		} else {
			display_text(&dev, ypos, ascii, strlen(ascii), false);
		}
	} // end for

	int selected = 0;
	CMD_t cmdBuf;
	while(1) {
		xQueueReceive(xQueueCmd, &cmdBuf, portMAX_DELAY);
		ESP_LOGI(pcTaskGetName(NULL),"cmdBuf.command=%d", cmdBuf.command);
		if (cmdBuf.command == CMD_DOWN) {
			strcpy(ascii, display[selected].display_text);
			ypos = selected + 2;
			display_text(&dev, ypos, ascii, strlen(ascii), false);

			selected++;
			if (selected == readLine) selected = 0;
			strcpy(ascii, display[selected].display_text);
			ypos = selected + 2;
			display_text(&dev, ypos, ascii, strlen(ascii), true);

		} else if (cmdBuf.command == CMD_SELECT) {
			ESP_LOGI(pcTaskGetName(NULL), "selected=%d",selected);
			ESP_LOGI(pcTaskGetName(NULL), "ir_cmd=0x%02x",display[selected].ir_cmd);
			ESP_LOGI(pcTaskGetName(NULL), "ir_addr=0x%02x",display[selected].ir_addr);
			uint16_t cmd = display[selected].ir_cmd;
			uint16_t addr = display[selected].ir_addr;
			cmd = ((~cmd) << 8) |  cmd; // Reverse cmd + cmd
			addr = ((~addr) << 8) | addr; // Reverse addr + addr
			ESP_LOGI(pcTaskGetName(NULL), "cmd=0x%x",cmd);
			ESP_LOGI(pcTaskGetName(NULL), "addr=0x%x",addr);

			// transmit IR NEC packets
			const ir_nec_scan_code_t scan_code = {
				.address = addr,
				.command = cmd,
			};
			ESP_ERROR_CHECK(rmt_transmit(tx_channel, nec_encoder, &scan_code, sizeof(scan_code), &transmit_config));
		}
	} // end while

	// nerver reach here
	vTaskDelete(NULL);
}
#endif // CONFIG_STICK

void app_main(void)
{
	ESP_LOGI(TAG, "Initializing SPIFFS");
	ESP_ERROR_CHECK(mountSPIFFS("/spiffs", "storage", 10));
	listSPIFFS("/spiffs");

	/* Create Queue */
	xQueueCmd = xQueueCreate( 10, sizeof(CMD_t) );
	configASSERT( xQueueCmd );

#if CONFIG_STICKC
	// power on
	AXP192_Initialize(I2C_NUM_0);
	AXP192_PowerOn();
#endif

#if CONFIG_STICKC_PLUS
	// power on
	AXP192_Initialize(I2C_NUM_0);
	AXP192_PowerOn();
	AXP192_ScreenBreath(11);
#endif

#if CONFIG_STICKC_PLUS2
	// power on
	#define POWER_HOLD_GPIO 4
	gpio_reset_pin( POWER_HOLD_GPIO );
	gpio_set_direction( POWER_HOLD_GPIO, GPIO_MODE_OUTPUT );
	gpio_set_level( POWER_HOLD_GPIO, 1 );
	// Enable SGM2578. VLED is supplied by SGM2578
	#define SGM2578_ENABLE_GPIO 27
	sgm2578_Enable(SGM2578_ENABLE_GPIO);
#endif

	xTaskCreate(tft, "TFT", 1024*4, NULL, 2, NULL);

#if CONFIG_STACK
	xTaskCreate(buttonA, "SELECT", 1024*4, NULL, 2, NULL);
	xTaskCreate(buttonB, "DOWN", 1024*4, NULL, 2, NULL);
	xTaskCreate(buttonC, "UP", 1024*4, NULL, 2, NULL);
#endif

#if CONFIG_STICKC || CONFIG_STICKC_PLUS || CONFIG_STICKC_PLUS2
	xTaskCreate(buttonA, "SELECT", 1024*4, NULL, 2, NULL);
	xTaskCreate(buttonB, "DOWN", 1024*4, NULL, 2, NULL);
#endif

#if CONFIG_STICK
	xTaskCreate(buttonStick, "BUTTON", 1024*4, NULL, 2, NULL);
#endif
}

