/*
 * This file is part of the WiCAN project.
 *
 * Copyright (C) 2022  Meatpi Electronics.
 * Written by Ali Slim <ali@meatpi.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include  "freertos/queue.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include <string.h>
#include "comm_server.h"
#include "lwip/sockets.h"
#include "driver/twai.h"
#include "types.h"
#include "esp_timer.h"
#include "config_server.h"
#include "realdash.h"
#include "slcan.h"
#include "can.h"
#include "ble.h"
#include "wifi_network.h"
#include "esp_mac.h"
#include "esp_ota_ops.h"
#include "nvs.h"
#include "nvs_flash.h"
// #include "driver/adc.h"
#include "esp_adc/adc_continuous.h"
// #include "esp_adc_cal.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
// #include "esp_adc/adc_cali.h"
#include "sleep_mode.h"
#include "ble.h"
#include "esp_sleep.h"
#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "esp_log.h"
#include "mqtt_client.h"
#include "ver.h"


#define SENSE_V_DIGITAL			0
#define FORCE_ON_GPIO_NUM		17
#define SENSE_V_DIG_GPIO_NUM	8
#define SENSE_V_ANA_GPIO_NUM	9

#define SECONDS_TO_STAY_ON      120

#define TAG 		__func__

#if SENSE_V_DIGITAL == 0

#define TIMES              256
#define ADC_RESULT_BYTE     4
#define ADC_CONV_LIMIT_EN   0
#define ADC_CONV_MODE       ADC_CONV_SINGLE_UNIT_1
#define ADC_OUTPUT_TYPE     ADC_DIGI_OUTPUT_FORMAT_TYPE2
static adc_channel_t channel[1] = {ADC_CHANNEL_8};


//#define THRESHOLD_VOLTAGE		13.0f
// #define SLEEP_TIME_DELAY		(180*1000*1000)
#define WAKEUP_TIME_DELAY		(200*1000)

static EventGroupHandle_t s_mqtt_event_group = NULL;
#define MQTT_CONNECTED_BIT 			BIT0
#define PUB_SUCCESS_BIT     		BIT1

static float sleep_voltage = 13.1f;
static uint8_t enable_sleep = 0;
static QueueHandle_t voltage_queue = NULL;
// static esp_adc_cal_characteristics_t adc1_chars;


static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%ld", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
//    esp_mqtt_client_handle_t client = event->client;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        xEventGroupSetBits(s_mqtt_event_group, MQTT_CONNECTED_BIT);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        xEventGroupClearBits(s_mqtt_event_group, MQTT_CONNECTED_BIT);
//        esp_mqtt_client_stop(client);
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        xEventGroupSetBits(s_mqtt_event_group, PUB_SUCCESS_BIT);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
//        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
//            log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
//            log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
//            log_error_if_nonzero("captured as transport's socket errno",  event->error_handle->esp_transport_sock_errno);
//            ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
//
//        }
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}
static esp_mqtt_client_handle_t client = NULL;
static void mqtt_init(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
		.broker.address.uri = config_server_get_alert_url(),
		.broker.address.port = config_server_get_alert_port(),
		.credentials.username = config_server_get_alert_mqtt_user(),
		.credentials.authentication.password = config_server_get_alert_mqtt_pass(),
		.network.disable_auto_reconnect = true,
		.network.reconnect_timeout_ms = 4000,
//         .uri = config_server_get_alert_url(),
// 		.port = config_server_get_alert_port(),
// 		.username = config_server_get_alert_mqtt_user(),
// 		.password = config_server_get_alert_mqtt_pass(),
// //		.disable_auto_reconnect = 1,
// 		.reconnect_timeout_ms = 4000
    };
    ESP_LOGI(TAG, "mqtt_cfg.uri: %s", mqtt_cfg.broker.address.uri);
    if(client == NULL)
    {
    	client = esp_mqtt_client_init(&mqtt_cfg);
        /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
        esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
        esp_mqtt_client_start(client);
    }
    else
    {
    	esp_mqtt_client_reconnect(client);
    }

    EventBits_t bits = xEventGroupWaitBits(s_mqtt_event_group,
						MQTT_CONNECTED_BIT,
						pdFALSE,
						pdFALSE,
						pdMS_TO_TICKS(10000));
    if (bits & MQTT_CONNECTED_BIT)
    {
    	static char pub_data[128];
    	float batt_voltage = 0;
    	sleep_mode_get_voltage(&batt_voltage);
    	sprintf(pub_data, "{\"alert\": \"low_battery\", \"battery_voltage\": %f}", batt_voltage);
        int msg_id = esp_mqtt_client_publish(client, config_server_get_alert_topic(), pub_data, 0, 1, 0);
        ESP_LOGI(TAG, "publish, msg_id=%d", msg_id);

    }
    else
    {
    	ESP_LOGE(TAG, "unable to connect to broker...");
    }
}

static TaskHandle_t s_task_handle;

static bool IRAM_ATTR s_conv_done_cb(adc_continuous_handle_t handle, const adc_continuous_evt_data_t *edata, void *user_data)
{
    BaseType_t mustYield = pdFALSE;
    //Notify that ADC continuous driver has done enough number of conversions
    vTaskNotifyGiveFromISR(s_task_handle, &mustYield);

    return (mustYield == pdTRUE);
}

static void continuous_adc_init(adc_channel_t *channel, uint8_t channel_num, adc_continuous_handle_t *out_handle)
{
	adc_continuous_handle_t handle = NULL;

    adc_continuous_handle_cfg_t adc_config = {
        .max_store_buf_size = 1024,
        .conv_frame_size = TIMES,
    };
    ESP_ERROR_CHECK(adc_continuous_new_handle(&adc_config, &handle));

    adc_continuous_config_t dig_cfg = {
        .sample_freq_hz = 10 * 1000,
        .conv_mode = ADC_CONV_MODE,
        .format = ADC_OUTPUT_TYPE,
    };

    adc_digi_pattern_config_t adc_pattern[SOC_ADC_PATT_LEN_MAX] = {0};
    dig_cfg.pattern_num = channel_num;
    for (int i = 0; i < channel_num; i++) {
	
        adc_pattern[i].atten = ADC_ATTEN_DB_11;
        adc_pattern[i].channel = channel[i];
        adc_pattern[i].unit = ADC_UNIT_1;
        adc_pattern[i].bit_width = SOC_ADC_DIGI_MAX_BITWIDTH;

        ESP_LOGI(TAG, "adc_pattern[%d].atten is :%"PRIx8, i, adc_pattern[i].atten);
        ESP_LOGI(TAG, "adc_pattern[%d].channel is :%"PRIx8, i, adc_pattern[i].channel);
        ESP_LOGI(TAG, "adc_pattern[%d].unit is :%"PRIx8, i, adc_pattern[i].unit);
    }
    dig_cfg.adc_pattern = adc_pattern;
    ESP_ERROR_CHECK(adc_continuous_config(handle, &dig_cfg));

    *out_handle = handle;

}

//ADC Attenuation
#define ADC_EXAMPLE_ATTEN           ADC_ATTEN_DB_11

//ADC Calibration
#if CONFIG_IDF_TARGET_ESP32
#define ADC_EXAMPLE_CALI_SCHEME     ESP_ADC_CAL_VAL_EFUSE_VREF
#elif CONFIG_IDF_TARGET_ESP32S2
#define ADC_EXAMPLE_CALI_SCHEME     ESP_ADC_CAL_VAL_EFUSE_TP
#elif CONFIG_IDF_TARGET_ESP32C3
#define ADC_EXAMPLE_CALI_SCHEME     ESP_ADC_CAL_VAL_EFUSE_TP
#elif CONFIG_IDF_TARGET_ESP32S3
#define ADC_EXAMPLE_CALI_SCHEME     ESP_ADC_CAL_VAL_EFUSE_TP_FIT
#endif
// static esp_adc_cal_characteristics_t adc1_chars;
static bool adc_calibration_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t *out_handle)
{
     adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;
    bool calibrated = false;

    if (!calibrated) {
        ESP_LOGI(TAG, "calibration scheme version is %s", "Curve Fitting");
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = unit,
            .chan = channel,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            calibrated = true;
        }
    }

    *out_handle = handle;
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Calibration Success");
    } else if (ret == ESP_ERR_NOT_SUPPORTED || !calibrated) {
        ESP_LOGW(TAG, "eFuse not burnt, skip software calibration");
    } else {
        ESP_LOGE(TAG, "Invalid arg or no memory");
    }

    return calibrated;
}

static void adc_calibration_deinit(adc_cali_handle_t handle)
{
	ESP_LOGI(TAG, "deregister %s calibration scheme", "Curve Fitting");
    ESP_ERROR_CHECK(adc_cali_delete_scheme_curve_fitting(handle));
}

#if !CONFIG_IDF_TARGET_ESP32
static bool check_valid_data(const adc_digi_output_data_t *data)
{
    const unsigned int unit = data->type2.unit;
    if (unit > 2) return false;
    if (data->type2.channel >= SOC_ADC_CHANNEL_NUM(unit)) return false;

    return true;
}
#endif

#define RUN_STATE			0
#define	SLEEP_DETECTED		1
#define SLEEP_STATE			2
#define WAKEUP_STATE		3

static void adc_task(void *pvParameters)
{
	s_task_handle = xTaskGetCurrentTaskHandle();

    esp_err_t ret;
    uint32_t ret_num = 0;
    uint8_t result[TIMES] = {0};
    static uint8_t sleep_state = 0;
    static int64_t sleep_detect_time = 0;
    static int64_t wakeup_detect_time = 0;
    static int64_t pub_time = 0;
    static float alert_voltage = 0;
    static uint64_t alert_time;
	uint64_t sleep_time = 0;
    alert_time = config_server_get_alert_time();
    alert_time *= (3600000000);
//    alert_time = 10000000;
    ESP_LOGW(TAG, "%" PRIu64 "\n", alert_time);

    if(config_server_get_alert_volt(&alert_voltage) != -1)
    {
    	alert_voltage = 16.0f;
    }

    memset(result, 0xcc, TIMES);
	adc_cali_handle_t adc1_cali_handle = NULL;
    adc_calibration_init(ADC_UNIT_1, channel[0], ADC_ATTEN_DB_11, &adc1_cali_handle);
	adc_continuous_handle_t handle = NULL;
    continuous_adc_init(channel, sizeof(channel) / sizeof(adc_channel_t), &handle);
	    adc_continuous_evt_cbs_t cbs = {
        .on_conv_done = s_conv_done_cb,
    };
    ESP_ERROR_CHECK(adc_continuous_register_event_callbacks(handle, &cbs, NULL));
    ESP_ERROR_CHECK(adc_continuous_start(handle));


	if(config_server_get_sleep_time((uint32_t*)&sleep_time) == -1)
	{
		sleep_time = 3;
	}
	sleep_time *= (60*1000000); //convert to microseconds

	while (1) {
		uint32_t count = 0;
    	uint64_t avg = 0;
    	uint32_t adc_val = 0;
		ret = adc_continuous_read(handle, result, TIMES, &ret_num, 0);
		if (ret == ESP_OK) {
			// ESP_LOGI(TAG, "ret is %x, ret_num is %"PRIu32" bytes", ret, ret_num);
			count = 0;
			avg = 0;
			for (int i = 0; i < ret_num; i += SOC_ADC_DIGI_RESULT_BYTES) {
				adc_digi_output_data_t *p = (adc_digi_output_data_t*)&result[i];
				/* Check the channel number validation, the data is invalid if the channel num exceed the maximum channel */
				if (check_valid_data(p)) {
					count++;
					int voltage;
					ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc1_cali_handle, p->type2.data, &voltage));
					avg += voltage;

					// ESP_LOGI(TAG, "Unit: %d, Channel: %d, Value: %d", p->type2.unit+1, p->type2.channel, p->type2.data);
				} else {
					ESP_LOGW(TAG, "Invalid data [%d_%d_%x]", p->type2.unit+1, p->type2.channel, p->type2.data);

				}
			}
			/**
			 * Because printing is slow, so every time you call `ulTaskNotifyTake`, it will immediately return.
			 * To avoid a task watchdog timeout, add a delay here. When you replace the way you process the data,
			 * usually you don't need this delay (as this task will block for a while).
			 */
			vTaskDelay(10);
		} else if (ret == ESP_ERR_TIMEOUT) {
			//We try to read `EXAMPLE_READ_LEN` until API returns timeout, which means there's no available data
			ESP_LOGW(TAG, "No data, increase timeout or reduce conv_num_each_intr");
			vTaskDelay(2000);
			continue;
		}

    	adc_val = (uint32_t)(avg/count);

    	float battery_voltage = (adc_val*14.21f)/(3000.0f);
		// ESP_LOGI(TAG, "battery voltage, value: %lu, voltage: %f", adc_val, battery_voltage);

    	xQueueOverwrite( voltage_queue, &battery_voltage );
    	if(enable_sleep == 1)
    	{
			switch(sleep_state)
			{
				case RUN_STATE:
				{
					if(battery_voltage < sleep_voltage)
					{
						ESP_LOGI(TAG, "low voltage, value: %lu, voltage: %f",adc_val, battery_voltage);
						sleep_detect_time = esp_timer_get_time();
						sleep_state++;
					}
					break;
				}
				case SLEEP_DETECTED:
				{
					if(battery_voltage > sleep_voltage)
					{
						ESP_LOGI(TAG, "high voltage, value: %lu, voltage: %f",adc_val, battery_voltage);
						sleep_state = RUN_STATE;
					}

					if((esp_timer_get_time() - sleep_detect_time) > sleep_time)
					{
						sleep_state = SLEEP_STATE;
	//    	    		wifi_network_deinit();
	//    	    		ble_disable();
					}

					break;
				}
				case SLEEP_STATE:
				{
					ESP_LOGI(TAG, "Go to sleep");
					if(battery_voltage > sleep_voltage)
					{
						wakeup_detect_time = esp_timer_get_time();
						ESP_LOGI(TAG, "low voltage, value: %lu, voltage: %f",adc_val, battery_voltage);
						sleep_state = WAKEUP_STATE;
					}

					if(config_server_get_battery_alert_config())
					{
						if(battery_voltage < alert_voltage)
						{
							ESP_LOGW(TAG, "battery alert!");
							if(((esp_timer_get_time() - pub_time) > alert_time) || (pub_time == 0))
							{
								pub_time = esp_timer_get_time();
								wifi_network_init(config_server_get_alert_ssid(), config_server_get_alert_pass());
								vTaskDelay(1000 / portTICK_PERIOD_MS);
								uint8_t count = 0;
								while(!wifi_network_is_connected())
								{
									vTaskDelay(1000 / portTICK_PERIOD_MS);
									if(count++ > 10)
									{
										break;
									}
								}
								if(wifi_network_is_connected())
								{
									ESP_LOGI(TAG, " wifi connectred try to publish");
									mqtt_init();
									EventBits_t bits = xEventGroupWaitBits(s_mqtt_event_group,
																			PUB_SUCCESS_BIT,
																			pdFALSE,
																			pdFALSE,
																			pdMS_TO_TICKS(10000));
									if (bits & PUB_SUCCESS_BIT)
									{
										ESP_LOGI(TAG, "publish ok");
										xEventGroupClearBits(s_mqtt_event_group, PUB_SUCCESS_BIT);
									}
									else
									{
										ESP_LOGE(TAG, "publish error");
									}
									esp_mqtt_client_disconnect(client);
									vTaskDelay(1000 / portTICK_PERIOD_MS);
									wifi_network_deinit();
								}
							}
						}
					}
					break;
				}
				case WAKEUP_STATE:
				{
					if(battery_voltage > sleep_voltage)
					{
						if((esp_timer_get_time() - wakeup_detect_time) > WAKEUP_TIME_DELAY)
						{
							ESP_LOGI(TAG, "Wake up now...");
							esp_restart();

						}
					}
					else if(battery_voltage < sleep_voltage)
					{
						sleep_state = SLEEP_STATE;
					}
					break;
				}
			}

	//    	ESP_LOGI(TAG, "value: %u",adc_val);
			if(sleep_state == SLEEP_STATE)
			{
				ESP_LOGW(TAG, "sleeping");
				can_disable();
				wifi_network_deinit();
				ble_disable();
				esp_sleep_enable_timer_wakeup(2*1000000);
				esp_light_sleep_start();;
			}
			else vTaskDelay(pdMS_TO_TICKS(1000));
    	}
    	else
    	{
    		vTaskDelay(pdMS_TO_TICKS(1000));
    	}
    }

    ESP_ERROR_CHECK(adc_continuous_stop(handle));
    ESP_ERROR_CHECK(adc_continuous_deinit(handle));
	adc_calibration_deinit(adc1_cali_handle);
}

int8_t sleep_mode_get_voltage(float *val)
{
	if(voltage_queue != NULL)
	{
		if(xQueuePeek( voltage_queue, val, 0 ))
		{
			return 1;
		}
		else return -1;
	}
	return -1;
}

int8_t sleep_mode_init(uint8_t enable, float sleep_volt)
{
	enable_sleep = enable;
	sleep_voltage = sleep_volt;
	ESP_LOGW(TAG, "sleep_volt: %2.2f", sleep_volt);
	s_mqtt_event_group = xEventGroupCreate();
	voltage_queue = xQueueCreate(1, sizeof( float) );
	xTaskCreate(adc_task, "adc_task", 4096, (void*)AF_INET, 5, NULL);

	return 1;
}

#else

static float sleep_voltage = 13.0f;
static uint8_t enable_sleep = 0;

static void check_digital_sensor(void *pvParameters)
{
	unsigned long timestamp = 0;
	bool countdown = false;

    while(1)
    {
		unsigned long now = esp_timer_get_time();

		int active = gpio_get_level(SENSE_V_DIG_GPIO_NUM);
		if( !countdown && !active ) 
		{
			countdown = true;
			timestamp = now;              
			ESP_LOGI(TAG, "Shutdown activated in %d seconds", SECONDS_TO_STAY_ON);
		} 
		else if (countdown && active)
		{
			countdown = false;
			timestamp = 0;
			ESP_LOGI(TAG, "Shutdown canceled");
   
		}

		if ( countdown  &&  (now - timestamp) / 1000000L >= SECONDS_TO_STAY_ON ) 
		{
			ESP_LOGI(TAG, "Shutdown now");
			gpio_set_level(FORCE_ON_GPIO_NUM, 0);
		}
		else if (countdown)
		{
			ESP_LOGI(TAG, "Shutdown in: %d secs", SECONDS_TO_STAY_ON - ((int)((now - timestamp) / 1000000L)));
		}
		vTaskDelay(pdMS_TO_TICKS(1000));
    }

}

int8_t sleep_mode_init(uint8_t enable, float sleep_volt)
{
	enable_sleep = enable;
	gpio_set_direction(SENSE_V_DIG_GPIO_NUM, GPIO_MODE_INPUT);
	gpio_set_pull_mode(SENSE_V_DIG_GPIO_NUM, GPIO_PULLUP_ONLY);
	gpio_set_direction(FORCE_ON_GPIO_NUM, GPIO_MODE_OUTPUT);
	if(enable_sleep)
	{
		gpio_set_level(FORCE_ON_GPIO_NUM, 1);
	}
	sleep_voltage = sleep_volt;
	ESP_LOGW(TAG, "sleep_volt: %2.2f", sleep_volt);
	xTaskCreate(check_digital_sensor, "check_digital_sensor", 4096, (void*)AF_INET, 5, NULL);

	return 1;
}

int8_t sleep_mode_get_voltage(float *val)
{
	return sleep_voltage; //fixed sleep voltage, my board doesn't support analog read
}

#endif