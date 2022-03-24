/* ADC2 Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_wpa2.h"
#include "esp_event.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_smartconfig.h"
#include "driver/gpio.h"
#include "driver/adc.h"
#include "driver/adc_common.h"
#include "mqtt_client.h"
#include "driver/dac.h"
#include "esp_system.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "math.h"
#include "esp_log.h"

#define FIRMWARE_VERSION	0.65
#define UPDATE_JSON_URL		"http://aodaycom.ipower.com/ota/test.json"
uint8_t system_update=4;


#define DAC_EXAMPLE_CHANNEL     CONFIG_EXAMPLE_DAC_CHANNEL
//#define ADC2_EXAMPLE_CHANNEL2    2//CONFIG_EXAMPLE_ADC2_CHANNEL
//#define ADC2_EXAMPLE_CHANNEL3    3//CONFIG_EXAMPLE_ADC2_CHANNEL
//#define ADC2_EXAMPLE_CHANNEL7    7//CONFIG_EXAMPLE_ADC2_CHANNEL
#define Beta	3435
#define R25	10000
#define Rs 9300
#define T0 25

//extern const char server_cert_pem_start[] = "-----BEGIN CERTIFICATE-----\n-----END CERTIFICATE-----";
extern const char server_cert_pem_start[] asm("_binary_aodaycom_pem_start");
extern const char server_cert_pem_end[] asm("_binary_aodaycom_pem_end");

//double_t Temp =0;
enum chan {ADC2_0=0,ADC2_3=3,ADC2_4,ADC2_5,ADC2_6,ADC2_7};
uint8_t chanv[] ={ADC2_0,ADC2_3,ADC2_4,ADC2_5,ADC2_6,ADC2_7};
typedef enum {Idle,circulating_on,antiFreez_on,Manual} Motor_state_t;
Motor_state_t Motor_state=Idle;
double_t result[7];
uint32_t raw_result[7];
uint8_t ave_count[7]={0,0,0,0,0,0,0};
char a[12]="Hello Aoday",b[20]="0000000";
char ssid[33] = { 0 };
char password[65] = { 0 };
static char serialNo[8]={0};
static uint8_t wifi_f=0;
static uint8_t set_f=0,connected_f=0;
TaskHandle_t SChandle;
TaskHandle_t Sending_handle;
esp_mqtt_client_handle_t client;


const char *host = "mqtts://6e5792e2fead42febf15259a437835c2.s1.eu.hivemq.cloud";
const char *username="aodayh";
const char *mqtt_password="Ady@1968";


#if CONFIG_IDF_TARGET_ESP32
static const adc_bits_width_t width = ADC_WIDTH_BIT_12;
#elif CONFIG_IDF_TARGET_ESP32S2
static const adc_bits_width_t width = ADC_WIDTH_BIT_13;
#endif

static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event,
   but we only care about one event - are we connected
   to the AP with an IP? */
static const int CONNECTED_BIT = BIT0;
static const int ESPTOUCH_DONE_BIT = BIT1;
static const char *TAG = "smartconfig";
nvs_handle_t my_handle,setup_handle;
static int8_t  On_diff=4, Off_diff =2, anti_freez_hi=0,anti_freez_lo=-30;
static uint8_t Tank_temp=70;
static void smartconfig_example_task(void * parm);
static void ADC_Read();
static void sendDataTask();
static void mqtt_app_start();
void sendingData(){
	char Stemp[10];
	char topic[16];
	sprintf(Stemp,"%.2f",result[0]);
	sprintf(topic,"%s/TU",serialNo);
	esp_mqtt_client_publish(client, topic, Stemp, 0, 0, 0);

	sprintf(Stemp,"%.2f",result[1]);
	sprintf(topic,"%s/TD",serialNo);
	esp_mqtt_client_publish(client, topic, Stemp, 0, 0, 0);

	sprintf(Stemp,"%.2f",result[2]);
	sprintf(topic,"%s/C1",serialNo);
	esp_mqtt_client_publish(client, topic, Stemp, 0, 0, 0);

	sprintf(Stemp,"%.2f",result[3]);
	sprintf(topic,"%s/C2",serialNo);
	esp_mqtt_client_publish(client, topic, Stemp, 0, 0, 0);

	sprintf(Stemp,"%.2f",result[4]);
	sprintf(topic,"%s/C3",serialNo);
	esp_mqtt_client_publish(client, topic, Stemp, 0, 0, 0);

	sprintf(Stemp,"%.2f",result[5]);
	sprintf(topic,"%s/C4",serialNo);
	esp_mqtt_client_publish(client, topic, Stemp, 0, 0, 0);

	sprintf(Stemp,"%d",Motor_state);
	sprintf(topic,"%s/state",serialNo);
	esp_mqtt_client_publish(client, topic, Stemp, 0, 0, 0);

}
static void sendDataTask(){
//	uint8_t i;
	while(1){
		if (connected_f==1){
			sendingData();
			connected_f=0;

		}
		vTaskDelay( 100 * portTICK_PERIOD_MS );
	}
}

static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    wifi_config_t wifi_config;
    ESP_LOGE(TAG, "Event dispatched from event loop base=%s, event_id=%d", event_base, event_id);
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
    	if (wifi_f!=1){
    		nvs_open("Wifi", NVS_READWRITE, &my_handle);
			nvs_set_str(my_handle, "SSID", ssid);
			nvs_set_str(my_handle, "PASS", password);
			nvs_set_u8(my_handle, "wifi", 1);
			nvs_commit(my_handle);
			nvs_close(my_handle);
			esp_restart();
    	}

//		gpio_set_level(GPIO_NUM_2, 1);
    	system_update=0;
		mqtt_app_start();
    	ESP_LOGE("WIFI.......","connected %d", event_id);
    }
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
    	system_update=4;

    }
//    if (event_base == MQTT_EVENTS ) {
//    	ESP_LOGE("MQTT.......event=","%d", event_id);
//    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        	if (wifi_f==1){
                bzero(&wifi_config, sizeof(wifi_config_t));
                memcpy(wifi_config.sta.ssid, ssid, sizeof(ssid));
                memcpy(wifi_config.sta.password, password, sizeof(password));
//                memcpy(wifi_config.sta.password, "00000", 6);
                ESP_LOGI(TAG, "SSID:%s", ssid);
                ESP_LOGI(TAG, "PASSWORD:%s", password);
                ESP_ERROR_CHECK( esp_wifi_disconnect() );
                ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
                esp_wifi_connect();

        		xEventGroupSetBits(s_wifi_event_group, ESPTOUCH_DONE_BIT);

        	} else {
            	xTaskCreate(smartconfig_example_task, "smartconfig_example_task", 4096, NULL, 3, SChandle);

        	}
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
    	printf("WIFI disconnected /n");
        esp_wifi_connect();
        xEventGroupClearBits(s_wifi_event_group, CONNECTED_BIT);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_wifi_event_group, CONNECTED_BIT);
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_SCAN_DONE) {
        ESP_LOGI(TAG, "Scan done");
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_FOUND_CHANNEL) {
        ESP_LOGI(TAG, "Found channel");
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_GOT_SSID_PSWD) {
        ESP_LOGI(TAG, "Got SSID and password");

        smartconfig_event_got_ssid_pswd_t *evt = (smartconfig_event_got_ssid_pswd_t *)event_data;
        uint8_t rvd_data[33] = { 0 };

        bzero(&wifi_config, sizeof(wifi_config_t));
        memcpy(wifi_config.sta.ssid, evt->ssid, sizeof(wifi_config.sta.ssid));
        memcpy(wifi_config.sta.password, evt->password, sizeof(wifi_config.sta.password));
        wifi_config.sta.bssid_set = evt->bssid_set;
        if (wifi_config.sta.bssid_set == true) {
            memcpy(wifi_config.sta.bssid, evt->bssid, sizeof(wifi_config.sta.bssid));
        }

        memcpy(ssid, evt->ssid, sizeof(evt->ssid));
        memcpy(password, evt->password, sizeof(evt->password));
        ESP_LOGI(TAG, "SSID:%s", ssid);
        ESP_LOGI(TAG, "PASSWORD:%s", password);
        if (evt->type == SC_TYPE_ESPTOUCH_V2) {
            ESP_ERROR_CHECK( esp_smartconfig_get_rvd_data(rvd_data, sizeof(rvd_data)) );
            ESP_LOGI(TAG, "RVD_DATA:");
            for (int i=0; i<33; i++) {
                printf("%02x ", rvd_data[i]);
            }
            printf("\n");
        }

        ESP_ERROR_CHECK( esp_wifi_disconnect() );
        ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
        esp_wifi_connect();
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_SEND_ACK_DONE) {
        xEventGroupSetBits(s_wifi_event_group, ESPTOUCH_DONE_BIT);
    }
}
static void save_action(esp_mqtt_event_handle_t event){
    char Temp_topc[25]={0};
    char Temp_data[25]={0};

    sprintf(Temp_topc,"%s/Tank",serialNo);
    if (strncmp(event->topic,Temp_topc,event->topic_len)==0){
    	sprintf(Temp_data,"%.*s", event->data_len, event->data);
    	Tank_temp=atoi(Temp_data);
    	ESP_LOGE("testing......","Tank =%d  %s",Tank_temp,Temp_data);
    }
    sprintf(Temp_topc,"%s/OffDif",serialNo);
    if (strncmp(event->topic,Temp_topc,event->topic_len)==0){
    	sprintf(Temp_data,"%.*s", event->data_len, event->data);
    	Off_diff=atoi(Temp_data);
    	ESP_LOGE("testing......","off_d =%d  %s",Off_diff,Temp_data);
    }
    sprintf(Temp_topc,"%s/OnDiff",serialNo);
    if (strncmp(event->topic,Temp_topc,event->topic_len)==0){
    	sprintf(Temp_data,"%.*s", event->data_len, event->data);
    	On_diff=atoi(Temp_data);
    	ESP_LOGE("testing......","on_d =%d  %s",On_diff,Temp_data);
    }
    sprintf(Temp_topc,"%s/antiHi",serialNo);
    if (strncmp(event->topic,Temp_topc,event->topic_len)==0){
    	sprintf(Temp_data,"%.*s", event->data_len, event->data);
    	anti_freez_hi=atoi(Temp_data);
    	ESP_LOGE("testing......","anti_hi =%d  %s",anti_freez_hi,Temp_data);
    }
    sprintf(Temp_topc,"%s/antiLo",serialNo);
    if (strncmp(event->topic,Temp_topc,event->topic_len)==0){
    	sprintf(Temp_data,"%.*s", event->data_len, event->data);
    	anti_freez_lo=atoi(Temp_data);
    	ESP_LOGE("testing......","anti_lo =%d  %s",anti_freez_lo,Temp_data);
    }
    sprintf(Temp_topc,"%s/manual",serialNo);
    if (strncmp(event->topic,Temp_topc,event->topic_len)==0){
    	sprintf(Temp_data,"%.*s", event->data_len, event->data);
    	Motor_state=atoi(Temp_data);
//    	sendDataTask();
    	sendingData();
    	ESP_LOGE("testing......","motor state =%d  %s",Motor_state,Temp_data);
    }

    nvs_open("Setup", NVS_READWRITE, &setup_handle);
	nvs_set_u8(setup_handle, "Set_f", 1);
	nvs_set_str(setup_handle, "SerialNo", "123456");
	nvs_set_u8(setup_handle, "Tank_tem", Tank_temp);
	nvs_set_i8(setup_handle, "On_diff", On_diff);
	nvs_set_i8(setup_handle, "Off_diff", Off_diff);
	nvs_set_i8(setup_handle, "Anti_freez_hi", anti_freez_hi);
	nvs_set_i8(setup_handle, "Anti_freez_lo", anti_freez_lo);
	nvs_commit(setup_handle);
	nvs_close(setup_handle);

}
static esp_err_t mqtt_event_handler_cb(esp_mqtt_event_handle_t event)
{
    client = event->client;
    int msg_id;
    char Temp_topc[25]={0},tem_data[16]={0};
    // your_context_t *context = event->context;
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        	xTaskCreate(sendDataTask, "Sending_Data", 4096, NULL, 3, Sending_handle);
        	system_update=2;

        	sprintf(Temp_topc,"%s/#",serialNo);
        	msg_id = esp_mqtt_client_subscribe(client, Temp_topc, 1);
            ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d topic:=%s", msg_id,Temp_topc);
            break;
        case MQTT_EVENT_DISCONNECTED:
        	if (Sending_handle!=NULL) {
            	system_update=0;
        		vTaskDelete(Sending_handle);
        	}
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            break;

        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT_EVENT_DATA");
            printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
            printf("DATA=%.*s\r\n", event->data_len, event->data);
        	sprintf(Temp_topc,"%s/status",serialNo);
            if ((strncmp(event->topic, Temp_topc, event->topic_len) == 0) && (strncmp(event->data, "Connected", event->data_len)==0)){
                connected_f=1;
            } else {
            	sprintf(Temp_topc,"%s/Setup",serialNo);
            	if (strncmp(event->topic,Temp_topc,event->topic_len)==0){
                	sprintf(Temp_topc,"%s/Setup/Tank",serialNo);
                	itoa(Tank_temp,tem_data,10);
            		esp_mqtt_client_publish(client, Temp_topc, tem_data, 0, 0, 0);
                	sprintf(Temp_topc,"%s/Setup/OffDif",serialNo);
                	itoa(Off_diff,tem_data,10);
            		esp_mqtt_client_publish(client, Temp_topc, tem_data, 0, 0, 0);
                	sprintf(Temp_topc,"%s/Setup/OnDiff",serialNo);
                	itoa(On_diff,tem_data,10);
            		esp_mqtt_client_publish(client, Temp_topc, tem_data, 0, 0, 0);
                	sprintf(Temp_topc,"%s/Setup/antiHi",serialNo);
                	itoa(anti_freez_hi,tem_data,10);
            		esp_mqtt_client_publish(client, Temp_topc, tem_data, 0, 0, 0);
                	sprintf(Temp_topc,"%s/Setup/antiLo",serialNo);
                	itoa(anti_freez_lo,tem_data,10);
            		esp_mqtt_client_publish(client, Temp_topc, tem_data, 0, 0, 0);
            	} else{
            		save_action(event);
            	}
            }
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                ESP_LOGI(TAG, "Last error code reported from esp-tls: 0x%x", event->error_handle->esp_tls_last_esp_err);
                ESP_LOGI(TAG, "Last tls stack error number: 0x%x", event->error_handle->esp_tls_stack_err);
                ESP_LOGI(TAG, "Last captured errno : %d (%s)",  event->error_handle->esp_transport_sock_errno,
                                                                strerror(event->error_handle->esp_transport_sock_errno));
            } else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
                ESP_LOGI(TAG, "Connection refused error: 0x%x", event->error_handle->connect_return_code);
            } else {
                ESP_LOGW(TAG, "Unknown error type: 0x%x", event->error_handle->error_type);
            }
            break;
        default:
            ESP_LOGI(TAG, "Other event id:%d", event->event_id);
            break;
    }
    return ESP_OK;
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    ESP_LOGE(TAG, "Event dispatched from event loop base=%s, event_id=%d", base, event_id);
    mqtt_event_handler_cb(event_data);
}

static void mqtt_app_start(void)
{
    const esp_mqtt_client_config_t mqtt_cfg = {
        .uri = host,
		.username=username,
		.password=mqtt_password,
		.port=8883,
//        .cert_pem = (const char *)mqtt_eclipseprojects_io_pem_start,
    };

    ESP_LOGI(TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size());
    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, client);
    esp_mqtt_client_start(client);
}

static void initialise_wifi(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );

    ESP_ERROR_CHECK( esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL) );
    ESP_ERROR_CHECK( esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL) );
    ESP_ERROR_CHECK( esp_event_handler_register(SC_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL) );

    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK( esp_wifi_start() );
}

double_t convert_Raw_to_temp(int temp)
{
	double t3=0;
//	t3=temp/(4095);
	if (temp<4096){
	t3=220*temp/(3905-temp);
//	t3=(1/(25.0+273.0))+(logf(t3/R25)/Beta);
	t3=0.254*t3-254;
//	t3=1/t3-273;
	}
//	t3=temp * 3300/4095;
	return t3;

}

void ADC_Read(){
    int     i;
//    esp_err_t r2;
    printf("Starting................");
	 while(1) {
	        printf("gpio_0= %d   version = %f\n",gpio_get_level(0),FIRMWARE_VERSION);
	        if (gpio_get_level(GPIO_NUM_0)==0){
	        	nvs_open("Wifi", NVS_READWRITE, &my_handle);
				nvs_set_u8(my_handle, "wifi", 0);
				nvs_commit(my_handle);
				nvs_close(my_handle);
//	        	nvs_flash_erase_partition("wifi");
				ESP_LOGE("","SSID and Password have been reset successfully now restarting");
				vTaskDelay( 30 * portTICK_PERIOD_MS );
//				 vTaskDelete(NULL);
//				   printf("Restarting now.\n");
//				    fflush(stdout);
				    esp_restart();

	        }
		 for (i=0;i<sizeof(chanv) ;i++){
			 adc1_config_width(width);
			 raw_result[i] =raw_result[i]+ adc1_get_raw( chanv[i]);
			 ave_count[i]++;
			 if (ave_count[i]>=20){
				 result[i]=convert_Raw_to_temp((int)(raw_result[i]/20));
	            ESP_LOGI("Reading ","Channel No.: %d  %d Temp=%.2f State=%d", raw_result[i]/20, chanv[i], result[i],Motor_state );
				 raw_result[i]=0;
				 ave_count[i]=0;
			 }

		 }
		 switch (Motor_state) {
		 case Idle:
			 gpio_set_level(GPIO_NUM_5, 1);
			 if (((result[2]-result[1])>=On_diff) || ((result[3]-result[1])>=On_diff) || ((result[4]-result[1])>=On_diff) || ((result[5]-result[1])>=On_diff)){
				 Motor_state=circulating_on;
			 };

			 if ((((result[2])<=anti_freez_lo)&&(result[2]>-40)) || (((result[3])<=anti_freez_lo)&&(result[3]>-40)) || (((result[4])<=anti_freez_lo)&&(result[4]>-40)) || (((result[5])<=anti_freez_lo)&&(result[5]>-40))){
				 Motor_state=antiFreez_on;
			 };

			 break;
		 case circulating_on:
			 gpio_set_level(GPIO_NUM_5, 0);
			 if (((result[2]-result[1])<=Off_diff) && ((result[3]-result[1])<=Off_diff) && ((result[4]-result[1])<=Off_diff) && ((result[5]-result[1])<=Off_diff)){
				 Motor_state=Idle;
			 };

			 break;
		 case antiFreez_on:
			 gpio_set_level(GPIO_NUM_5, 0);
			 if (((result[2])>=anti_freez_hi) && ((result[3])>=anti_freez_hi) && ((result[4])>=anti_freez_hi) && ((result[5])>=anti_freez_hi)){
				 Motor_state=Idle;
			 };

			 break;
		 case Manual:
			 gpio_set_level(GPIO_NUM_5, 0);

			 break;
		 };

	        vTaskDelay( 5 * portTICK_PERIOD_MS );
	    }
}
void smartconfigOver(void){

    ESP_LOGI(TAG, "smartconfig over");
    esp_smartconfig_stop();
    xTaskCreate(ADC_Read, "ADC_Read_Task", 1024*2,NULL, 5, NULL);
    vTaskDelete(SChandle);
}
static void smartconfig_example_task(void * parm)
{
    EventBits_t uxBits;
    ESP_ERROR_CHECK( esp_smartconfig_set_type(SC_TYPE_ESPTOUCH) );
    smartconfig_start_config_t cfg = SMARTCONFIG_START_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_smartconfig_start(&cfg) );
    while (1) {
        uxBits = xEventGroupWaitBits(s_wifi_event_group, CONNECTED_BIT | ESPTOUCH_DONE_BIT, true, false, portMAX_DELAY);
        if(uxBits & CONNECTED_BIT) {
            ESP_LOGI(TAG, "WiFi Connected to AP");
        }
        if(uxBits & ESPTOUCH_DONE_BIT) {
        	smartconfigOver();
        }
    }
}

void Blink(){
while(1){
		if (system_update==0){
		gpio_set_level(GPIO_NUM_2, 1);
//		gpio_set_level(GPIO_NUM_5, 1);
		vTaskDelay( 10 * portTICK_PERIOD_MS );
		gpio_set_level(GPIO_NUM_2, 0);
//		gpio_set_level(GPIO_NUM_5, 0);
		vTaskDelay( 10 * portTICK_PERIOD_MS );
	} else if (system_update==1) {
		gpio_set_level(GPIO_NUM_2, 1);
		vTaskDelay( 2 * portTICK_PERIOD_MS );
		gpio_set_level(GPIO_NUM_2, 0);
		vTaskDelay( 2 * portTICK_PERIOD_MS );
	} else if (system_update==2) {
		gpio_set_level(GPIO_NUM_2, 1);
		vTaskDelay( 2 * portTICK_PERIOD_MS );

	} else {
		gpio_set_level(GPIO_NUM_2, 0);
		vTaskDelay( 2 * portTICK_PERIOD_MS );

	}
}
}

char rcv_buffer[200];

// esp_http_client event handler
esp_err_t _http_event_handler(esp_http_client_event_t *evt) {

	switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            break;
        case HTTP_EVENT_ON_CONNECTED:
            break;
        case HTTP_EVENT_HEADER_SENT:
            break;
        case HTTP_EVENT_ON_HEADER:
            break;
        case HTTP_EVENT_ON_DATA:
            if (!esp_http_client_is_chunked_response(evt->client)) {
				strncpy(rcv_buffer, (char*)evt->data, evt->data_len);
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            break;
        case HTTP_EVENT_DISCONNECTED:
            break;
    }
    return ESP_OK;
}

void check_update_task(void *pvParameter)
{

	while(1) {

		printf("Looking for a new firmware...\n");

		// configure the esp_http_client
		esp_http_client_config_t config = {
        .url = UPDATE_JSON_URL,
        .event_handler = _http_event_handler,
		};
		esp_http_client_handle_t client = esp_http_client_init(&config);

		// downloading the json file
		esp_err_t err = esp_http_client_perform(client);
		if(err == ESP_OK) {

			// parse the json file
			cJSON *json = cJSON_Parse(rcv_buffer);
			if(json == NULL) printf("downloaded file is not a valid json, aborting...\n");
			else {
				cJSON *version = cJSON_GetObjectItemCaseSensitive(json, "version");
				cJSON *file = cJSON_GetObjectItemCaseSensitive(json, "file");

				// check the version
				if(!cJSON_IsNumber(version)) printf("unable to read new version, aborting...\n");
				else {

					double new_version = version->valuedouble;
					if(new_version > FIRMWARE_VERSION) {

						printf("current firmware version (%.1f) is lower than the available one (%.1f), upgrading...\n", FIRMWARE_VERSION, new_version);
						printf("file location: %s",file->valuestring);
						if(cJSON_IsString(file) && (file->valuestring != NULL)) {
							printf("downloading and installing new firmware (%s)...\n", file->valuestring);

							esp_http_client_config_t ota_client_config = {
								.url = file->valuestring,
								.cert_pem = server_cert_pem_start,
//								.username="aoday",
//								.password="Ady@1968",

							};
							system_update=1;
							esp_err_t ret = esp_https_ota(&ota_client_config);
							if (ret == ESP_OK) {
								printf("OTA OK, restarting...\n");
								esp_restart();
							} else {
								printf("OTA failed...\n");
								esp_restart();
							}
						}
						else printf("unable to read the new file name, aborting...\n");
					}
					else printf("current firmware version (%.1f) is greater or equal to the available one (%.1f), nothing to do...\n", FIRMWARE_VERSION, new_version);
				}
			}
		}
		else printf("unable to download the json file, aborting...\n");

		// cleanup
		esp_http_client_cleanup(client);

		printf("Delay.........................\n");
        vTaskDelay(60000 / portTICK_PERIOD_MS);
    }
}


void app_main(void)
{
    esp_err_t r;
    int i=0;
    size_t j;

//    gpio_set_level(GPIO_NUM_2, 1);
    ESP_ERROR_CHECK( nvs_flash_init() );
    printf("Opening Non-Volatile Storage (NVS) handle... ");
    r = nvs_open("Wifi", NVS_READWRITE, &my_handle);
    if (r != ESP_OK) {
        printf("Error (%s) opening NVS handle!\n", esp_err_to_name(r));
    } else {
        printf("Done\n");
//        nvs_set_str(my_handle, "a",a);
//        nvs_commit(my_handle);
        r=nvs_get_str(my_handle, "PASS", NULL, &j);
        ESP_LOGI("NVS","%s",esp_err_to_name(r));
        if (r==ESP_OK){
			ESP_LOGI("NVS","%d",j);
			r=nvs_get_str(my_handle, "PASS", password, &j);
			ESP_LOGI("NVS","Pssword : %s",password);
	        r=nvs_get_str(my_handle, "PASS", NULL, &j);
			r=nvs_get_str(my_handle, "SSID", ssid, &j);
			r=nvs_get_u8(my_handle, "wifi", &wifi_f);
			ESP_LOGI("NVS","SSID: %s",ssid);
			ESP_LOGI("NVS","Flag: %d",wifi_f);
			if (set_f==0){
				nvs_set_str(my_handle, "SerialNo", "123456");
			}
			nvs_close(my_handle);
        }
        r = nvs_open("Setup", NVS_READWRITE, &setup_handle);
		ESP_LOGE(TAG,"serial no0:= %s",serialNo);
		if (r != ESP_OK) {
            printf("Error (%s) opening NVS handle!\n", esp_err_to_name(r));
			ESP_LOGE(TAG,"serial no2:= %s",serialNo);

        } else {
			r=nvs_get_u8(setup_handle, "Set_f", &set_f);
//			set_f=0;
			if (set_f==0){
				nvs_set_u8(setup_handle, "Set_f", 1);
				nvs_set_str(setup_handle, "SerialNo", "123456");
				nvs_set_u8(setup_handle, "Tank_tem", Tank_temp);
				nvs_set_i8(setup_handle, "On_diff", On_diff);
				nvs_set_i8(setup_handle, "Off_diff", Off_diff);
				nvs_set_i8(setup_handle, "Anti_freez_hi", anti_freez_hi);
				nvs_set_i8(setup_handle, "Anti_freez_lo", anti_freez_lo);
				nvs_commit(setup_handle);
				ESP_LOGE(TAG,"serial no3:= %s",serialNo);
			}
	        r=nvs_get_str(setup_handle, "SerialNo", NULL, &j);
	        ESP_LOGE(TAG,"serial no1:= %d",j);

			r=nvs_get_str(setup_handle, "SerialNo", serialNo, &j);
			nvs_get_u8(setup_handle, "Tank_tem", &Tank_temp);
			nvs_get_i8(setup_handle, "On_diff", &On_diff);
			nvs_get_i8(setup_handle, "Off_diff", &Off_diff);
			nvs_get_i8(setup_handle, "Anti_freez_hi", &anti_freez_hi);
			nvs_get_i8(setup_handle, "Anti_freez_lo", &anti_freez_lo);
			nvs_close(setup_handle);
			ESP_LOGE(TAG,"serial no:= %s",serialNo);

        }

    }

    initialise_wifi();


    gpio_num_t adc_gpio_num;
    for (i=0;i<sizeof(chanv);i++){
    r = adc1_pad_get_io_num( chanv[i], &adc_gpio_num );
//    assert( r == ESP_OK );
    ESP_LOGE("GPIO assigned ", "ADC1 channel %d @ GPIO %d ", chanv[i], adc_gpio_num );
//    printf("adc2_init...\n");
    adc1_config_channel_atten( chanv[i], ADC_ATTEN_DB_11 );
    r=gpio_pullup_dis(adc_gpio_num);
    r=gpio_pulldown_dis(adc_gpio_num);
    printf("error code   %d\n",r);
    }
    gpio_set_direction(GPIO_NUM_0 ,GPIO_MODE_INPUT  );
    gpio_set_direction(GPIO_NUM_2 ,GPIO_MODE_OUTPUT  );
    gpio_set_direction(GPIO_NUM_5 ,GPIO_MODE_OUTPUT_OD  );
	 gpio_set_level(GPIO_NUM_5, 1);

//    gpio_pullup_en(GPIO_NUM_5);


    vTaskDelay(2 * portTICK_PERIOD_MS);

    printf("start conversion.\n");
    if (wifi_f==1) {
    	xTaskCreate(ADC_Read, "ADC_Read_Task", 1024*2,NULL, 5, NULL);
    	xTaskCreate(Blink, "Blink", 1024,NULL, 5, NULL);
    	xTaskCreate(&check_update_task, "check_update_task", 8192, NULL, 5, NULL);
    }

}
