#include "freertos/FreeRTOS.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include "nvs_flash.h"
#include "driver/adc.h"
#include "u8g2_esp32_hal.h"

#include "websocket.h"

#define SSID "xd"
#define PASS "xdddddd1337"

void onOpen ( void ){
    printf ( "websocket connected\r\n" );
}

char buffer[100];

void onMessage ( websocket_frame_t * frame ){
    printf ( "receieved data: %s, data length: %d, frame type: %02x\r\n", frame->data, frame->length, frame->opcode );    
	memcpy(buffer, frame->data, frame->length);
}

void onClose ( void ){
    printf ( "websocket closed\r\n" );
}

void websocket_task1 ( void * p ){

    websocket_t * websocket = new_websocket( IP_ADDR_ANY, 1000 );
    websocket->onopen = onOpen;
    websocket->onmessage = onMessage;
    websocket->onclose = onClose;

    char adc[10];

    while ( 1 ) {
	    sprintf(adc, "%d", adc1_get_voltage(ADC1_CHANNEL_0));
        websocket_send ( websocket, ( u8_t * )adc, strlen ( adc ) );
    }
}

void websocket_task2 ( void * p ){
	
    while ( 1 ) {
	    size_t xd = xPortGetFreeHeapSize();
	    printf("ram: %d\n", xd);
	    vTaskDelay(100);
    }
}

void task_test_SSD1306i2c(void *ignore) {
	
	u8g2_esp32_hal_t u8g2_esp32_hal = U8G2_ESP32_HAL_DEFAULT;
	u8g2_esp32_hal.sda   = 5;
	u8g2_esp32_hal.scl  = 4;
	u8g2_esp32_hal_init(u8g2_esp32_hal);


	u8g2_t u8g2;  // a structure which will contain all the data for one display
	
	u8g2_Setup_ssd1306_128x64_noname_f(
		&u8g2,
		U8G2_R0,
		//u8x8_byte_sw_i2c,
		u8g2_esp32_msg_i2c_cb,
		u8g2_esp32_msg_i2c_and_delay_cb);   // init u8g2 structure
	
	u8x8_SetI2CAddress(&u8g2.u8x8, 0x78);

	u8g2_InitDisplay(&u8g2);  // send init sequence to the display, display is in sleep mode after this,
	u8g2_SetPowerSave(&u8g2, 0);  // wake up display
	u8g2_ClearBuffer(&u8g2);
	u8g2_SetFont(&u8g2, u8g2_font_courB10_tf);
	u8g2_SetContrast(&u8g2, 1);

	while (1)
	{
		u8g2_ClearBuffer(&u8g2);
		u8g2_SetFont(&u8g2, u8g2_font_fur20_tf);
		u8g2_DrawStr(&u8g2, 2, 22, dupa);
		u8g2_SendBuffer(&u8g2);
	}
}

esp_err_t event_handler(void *ctx, system_event_t *event)
{
    return ESP_OK;
}

void app_main(void)
{
    nvs_flash_init();
    tcpip_adapter_init();
    ESP_ERROR_CHECK( esp_event_loop_init(event_handler, NULL) );
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    wifi_config_t sta_config = {
        .sta = {
            .ssid = SSID,
            .password = PASS,
            .bssid_set = false
        }
    };
    ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_STA, &sta_config) );
    ESP_ERROR_CHECK( esp_wifi_start() );
    ESP_ERROR_CHECK( esp_wifi_connect() );

    adc1_config_width( ADC_WIDTH_9Bit );

    xTaskCreate ( websocket_task1, "websocket1", 2048, NULL, 0, NULL );
    xTaskCreate ( websocket_task2, "websocket2", 2048, NULL, 0, NULL );
	xTaskCreate(task_test_SSD1306i2c, "lcd", 4096, NULL, 0, NULL);

    vTaskDelete ( xTaskGetCurrentTaskHandle() );
}

