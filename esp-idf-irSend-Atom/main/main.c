/*
	IR protocols example

	This example code is in the Public Domain (or CC0 licensed, at your option.)

	Unless required by applicable law or agreed to in writing, this
	software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
	CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
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

#define GPIO_INPUT GPIO_NUM_39
#define RMT_TX_GPIO_NUM	GPIO_NUM_12 /*!< GPIO number for transmitter signal */
#define MAX_CONFIG 20
#define MAX_CHARACTER 16

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

void buttonAtom(void *pvParameters)
{
	ESP_LOGI(pcTaskGetName(0), "Start");
	CMD_t cmdBuf;
	cmdBuf.taskHandle = xTaskGetCurrentTaskHandle();
	cmdBuf.command = CMD_SELECT;

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
			ESP_LOGI(pcTaskGetName(0), "Push Button");
			while(1) {
				level = gpio_get_level(GPIO_INPUT);
				if (level == 1) break;
				vTaskDelay(1);
			}
			xQueueSend(xQueueCmd, &cmdBuf, 0);
		}
		vTaskDelay(1);
	}
}

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
	ESP_LOGI(pcTaskGetName(0), "Reading file:maxText=%d",maxText);
	FILE* f = fopen("/spiffs/Display.def", "r");
	if (f == NULL) {
			ESP_LOGE(pcTaskGetName(0), "Failed to open define file for reading");
			ESP_LOGE(pcTaskGetName(0), "Please make Display.def");
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
		ESP_LOGI(pcTaskGetName(0), "line=[%s]", line);
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

void tft(void *pvParameters)
{
	// Setup IR transmitter
	rmt_channel_handle_t tx_channel = NULL;
	rmt_encoder_handle_t nec_encoder = NULL;
	rmt_transmit_config_t transmit_config = {};
	initializeRMT(&tx_channel, &nec_encoder, &transmit_config);

	// Read display information
	DISPLAY_t display[MAX_CONFIG];
	for(int i=0;i<MAX_CONFIG;i++) display[i].enable = false;
	int readLine = readDefineFile(display, MAX_CONFIG, MAX_CHARACTER);
	if (readLine == 0) {
		while(1) { vTaskDelay(1); }
	}
	for(int i=0;i<readLine;i++) {
		ESP_LOGI(pcTaskGetName(0), "display[%d].display_text=[%s]",i, display[i].display_text);
		ESP_LOGI(pcTaskGetName(0), "display[%d].ir_cmd=[0x%02x]",i, display[i].ir_cmd);
		ESP_LOGI(pcTaskGetName(0), "display[%d].ir_addr=[0x%02x]",i, display[i].ir_addr);
	}

	int selected = 0;
	CMD_t cmdBuf;
	while(1) {
		xQueueReceive(xQueueCmd, &cmdBuf, portMAX_DELAY);
		ESP_LOGI(pcTaskGetName(0),"cmdBuf.command=%d", cmdBuf.command);
		if (cmdBuf.command == CMD_SELECT) {
			ESP_LOGI(pcTaskGetName(0), "selected=%d",selected);
			ESP_LOGI(pcTaskGetName(0), "ir_cmd=0x%02x",display[selected].ir_cmd);
			ESP_LOGI(pcTaskGetName(0), "ir_addr=0x%02x",display[selected].ir_addr);
			uint16_t cmd = display[selected].ir_cmd;
			uint16_t addr = display[selected].ir_addr;
			cmd = ((~cmd) << 8) |  cmd; // Reverse cmd + cmd
			addr = ((~addr) << 8) | addr; // Reverse addr + addr
			ESP_LOGI(pcTaskGetName(0), "cmd=0x%x",cmd);
			ESP_LOGI(pcTaskGetName(0), "addr=0x%x",addr);

			// transmit IR NEC packets
			const ir_nec_scan_code_t scan_code = {
				.address = addr,
				.command = cmd,
			};
			ESP_ERROR_CHECK(rmt_transmit(tx_channel, nec_encoder, &scan_code, sizeof(scan_code), &transmit_config));

			if (selected == 0) {
				selected = 1;
			} else {
				selected = 0;
			}
		}
	} // end while

	// nerver reach here
	vTaskDelete(NULL);
}

void app_main(void)
{
	ESP_LOGI(TAG, "Initializing SPIFFS");
	ESP_ERROR_CHECK(mountSPIFFS("/spiffs", "storage", 10));
	listSPIFFS("/spiffs");

	/* Create Queue */
	xQueueCmd = xQueueCreate( 10, sizeof(CMD_t) );
	configASSERT( xQueueCmd );

	xTaskCreate(tft, "TFT", 1024*4, NULL, 2, NULL);

	xTaskCreate(buttonAtom, "BUTTON", 1024*4, NULL, 2, NULL);
}

