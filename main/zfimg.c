/*******
 * Motion detected Photo taker
********/

#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "esp_tls.h"
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
#include "esp_crt_bundle.h"
#endif

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

#include "esp_camera.h"
#include "esp_http_client.h"

// CAMERA_MODEL_AI_THINKER
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

#define MAX_HTTP_RECV_BUFFER 512
#define MAX_HTTP_OUTPUT_BUFFER 2048


#define WIFI_CONN_MAX_RETRY	10

#define WIFI_SSID		"NAbbey406"
#define WIFI_PASSWORD	"AkmeBalletH1002"
//#define WIFI_SSID		"AndroidAPba37"
//#define WIFI_PASSWORD	"hjqc3219"

uint8_t wifi_sta_mac[6];
char wifi_sta_mac_str[16] = {0};


static const char *TAG = "ZFIMG";
static const char *post_url = "https://464g2u0mf7.execute-api.us-east-1.amazonaws.com/default/Post2S3";
static const char *post_host = "464g2u0mf7.execute-api.us-east-1.amazonaws.com";
static const char *post_path = "/default/Post2S3";

extern const char server_root_cert_pem_start[] asm("_binary_server_root_cert_pem_start");
extern const char server_root_cert_pem_end[]   asm("_binary_server_root_cert_pem_end");


/*****
 * Flash stuff
 *****/
int32_t f_snapcount = 0;

static void init_nvs_variables()
{
	f_snapcount = 0;
}

static esp_err_t read_nvs()
{
	esp_err_t err;
    nvs_handle_t my_handle;
    err = nvs_open("storage", NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
		return err;
    } else {
		init_nvs_variables();
		err = nvs_get_i32(my_handle, "f_snapcount", &f_snapcount);

        switch (err) {
	case ESP_OK:
		break;
            case ESP_ERR_NVS_NOT_FOUND:
                ESP_LOGE(TAG, "The value is not initialized yet!");
				err = ESP_OK;
                break;
            default :
                ESP_LOGE(TAG, "Error (%s) reading from NVS!", esp_err_to_name(err));
        }
	}
	nvs_close(my_handle);
	return err;
}

static esp_err_t write_nvs()
{
	esp_err_t err;
    nvs_handle_t my_handle;
    err = nvs_open("storage", NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
		return err;
    } else {
		err = nvs_set_i32(my_handle, "f_snapcount", f_snapcount);
		if (err != ESP_OK) {
			ESP_LOGE(TAG, "Error (%s) writing to NVS!", esp_err_to_name(err));
			return err;
		}
		
		err = nvs_commit(my_handle);
		if (err != ESP_OK) {
			ESP_LOGE(TAG, "Error (%s) committing to NVS!", esp_err_to_name(err));
			return err;
		}
	}
	nvs_close(my_handle);
	return err;
}


/****
 * Debug stuff
 ****/

static void dump_memory(const char * msg)
{
    ESP_LOGI(TAG, "Memory %s", msg);
    ESP_LOGI(TAG, "  Free heap size:     %ld", esp_get_free_heap_size());
    ESP_LOGI(TAG, "  Min Free heap size: %ld", esp_get_minimum_free_heap_size());
}


static void dump_camera_fb(camera_fb_t * fb)
{
	ESP_LOGI(TAG, "  len       = %d", fb->len);
	ESP_LOGI(TAG, "  width     = %d", fb->width);
	ESP_LOGI(TAG, "  height    = %d", fb->height);
	ESP_LOGI(TAG, "  pixformat = %d", fb->format);
	ESP_LOGI(TAG, "  tv_sec    = %d", (int)fb->timestamp.tv_sec);
	ESP_LOGI(TAG, "  tv_usec   = %d", (int)fb->timestamp.tv_usec);
}

static void dump_camera_sensor(sensor_t* sensor)
{
    camera_status_t * s = &sensor->status;
    ESP_LOGI(TAG, "--> Camera Sensor <--");
    ESP_LOGI(TAG, "  framesize         = %d", s->framesize);
    ESP_LOGI(TAG, "  scale             = %d", s->scale);
    ESP_LOGI(TAG, "  binning           = %d", s->binning);
    ESP_LOGI(TAG, "  quality           = %d", s->quality);
    ESP_LOGI(TAG, "  brightness        = %d", s->brightness);
    ESP_LOGI(TAG, "  contrast          = %d", s->contrast);
    ESP_LOGI(TAG, "  saturation        = %d", s->saturation);
    ESP_LOGI(TAG, "  sharpness         = %d", s->sharpness);
    ESP_LOGI(TAG, "  denoise           = %d", s->denoise);
    ESP_LOGI(TAG, "  special_effect    = %d", s->special_effect);
    ESP_LOGI(TAG, "  wb_mode           = %d", s->wb_mode);
    ESP_LOGI(TAG, "  awb               = %d", s->awb);
    ESP_LOGI(TAG, "  awb_gain          = %d", s->awb_gain);
    ESP_LOGI(TAG, "  aec               = %d", s->aec);
    ESP_LOGI(TAG, "  aec2              = %d", s->aec2);
    ESP_LOGI(TAG, "  ae_level          = %d", s->ae_level);
    ESP_LOGI(TAG, "  aec_value         = %d", s->aec_value);
    ESP_LOGI(TAG, "  agc               = %d", s->agc);
    ESP_LOGI(TAG, "  agc_gain          = %d", s->agc_gain);
    ESP_LOGI(TAG, "  gainceiling       = %d", s->gainceiling);
    ESP_LOGI(TAG, "  bpc               = %d", s->bpc);
    ESP_LOGI(TAG, "  wpc               = %d", s->wpc);
    ESP_LOGI(TAG, "  raw_gma           = %d", s->raw_gma);
    ESP_LOGI(TAG, "  lenc              = %d", s->lenc);
    ESP_LOGI(TAG, "  hmirror           = %d", s->hmirror);
    ESP_LOGI(TAG, "  vflip             = %d", s->vflip);
    ESP_LOGI(TAG, "  dcw               = %d", s->dcw);
    ESP_LOGI(TAG, "  colorbar          = %d", s->colorbar);

}

static void dump_log(char *buf, int len)
{
	for (int ix = 0; ix < len; ix++) {
		printf("%c", buf[ix]);
	}
	printf("\n");
	for (int ix = 0; ix < len; ix++) {
		printf("%02x ", buf[ix]);
	}
	printf("\n");
}


/*****
 * Camera stuff
 *****/
static esp_err_t init_camera()
{
	ESP_LOGI(TAG, "starting init_camera");
	camera_config_t config;
	config.ledc_channel = LEDC_CHANNEL_0;
	config.ledc_timer = LEDC_TIMER_0;
	config.pin_d0 = Y2_GPIO_NUM;
	config.pin_d1 = Y3_GPIO_NUM;
	config.pin_d2 = Y4_GPIO_NUM;
	config.pin_d3 = Y5_GPIO_NUM;
	config.pin_d4 = Y6_GPIO_NUM;
	config.pin_d5 = Y7_GPIO_NUM;
	config.pin_d6 = Y8_GPIO_NUM;
	config.pin_d7 = Y9_GPIO_NUM;
	config.pin_xclk = XCLK_GPIO_NUM;
	config.pin_pclk = PCLK_GPIO_NUM;
	config.pin_vsync = VSYNC_GPIO_NUM;
	config.pin_href = HREF_GPIO_NUM;
	//config.pin_sscb_sda = SIOD_GPIO_NUM;
	//config.pin_sscb_scl = SIOC_GPIO_NUM;
	config.pin_sccb_sda = SIOD_GPIO_NUM;
	config.pin_sccb_scl = SIOC_GPIO_NUM;
	config.pin_pwdn = PWDN_GPIO_NUM;
	config.pin_reset = RESET_GPIO_NUM;
	config.xclk_freq_hz = 20000000;
	config.pixel_format = PIXFORMAT_JPEG;
	//init with high specs to pre-allocate larger buffers
	/*
	if (psramFound()) {
		config.frame_size = FRAMESIZE_UXGA;
		config.jpeg_quality = 10;
		config.fb_count = 2;
	} 	else {
		config.frame_size = FRAMESIZE_SVGA;
		config.jpeg_quality = 12;
		config.fb_count = 1;
	}
	*/
	/*
	config.frame_size = FRAMESIZE_SVGA;
	config.jpeg_quality = 12;
	config.fb_count = 1;
	*/
	config.frame_size = FRAMESIZE_UXGA;
	config.jpeg_quality = 10;
	config.fb_count = 2;

	// camera init
	esp_err_t err = esp_camera_init(&config);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "Camera init failed with error 0x%x", err);
		return err;
	}
	ESP_LOGI(TAG, "End init_camera");
	return ESP_OK;
}


/*****
 * HTTP stuff
 *****/
esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    static char *output_buffer;  // Buffer to store response of http request from event handler
    static int output_len;       // Stores number of bytes read
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            if (!esp_http_client_is_chunked_response(evt->client)) {
                // If user_data buffer is configured, copy the response into the buffer
                if (evt->user_data) {
                    memcpy(evt->user_data + output_len, evt->data, evt->data_len);
                } else {
                    if (output_buffer == NULL) {
                        output_buffer = (char *) malloc(esp_http_client_get_content_length(evt->client));
                        output_len = 0;
                        if (output_buffer == NULL) {
                            ESP_LOGE(TAG, "Failed to allocate memory for output buffer");
                            return ESP_FAIL;
                        }
                    }
                    memcpy(output_buffer + output_len, evt->data, evt->data_len);
                }
                output_len += evt->data_len;
            }

            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            if (output_buffer != NULL) {
                // Response is accumulated in output_buffer. Uncomment the below line to print the accumulated response
                // ESP_LOG_BUFFER_HEX(TAG, output_buffer, output_len);
                free(output_buffer);
                output_buffer = NULL;
            }
            output_len = 0;
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
            int mbedtls_err = 0;
            esp_err_t err = esp_tls_get_and_clear_last_error((esp_tls_error_handle_t)evt->data, &mbedtls_err, NULL);
            if (err != 0) {
                ESP_LOGI(TAG, "Last esp error code: 0x%x", err);
                ESP_LOGI(TAG, "Last mbedtls failure: 0x%x", mbedtls_err);
            }
            if (output_buffer != NULL) {
                free(output_buffer);
                output_buffer = NULL;
            }
            output_len = 0;
            break;
        case HTTP_EVENT_REDIRECT:
            ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
            esp_http_client_set_header(evt->client, "From", "user@example.com");
            esp_http_client_set_header(evt->client, "Accept", "text/html");
            esp_http_client_set_redirection(evt->client);
            break;
    }
    return ESP_OK;
}

static esp_err_t send_photo(camera_fb_t* fb, const char * url)
{
    //ESP_LOGI(TAG, "start send photo");
    esp_err_t err;
    char resp_buf[MAX_HTTP_OUTPUT_BUFFER] = {0};
    esp_http_client_config_t config = {
        .host = post_host,
        .path = post_path,
        .method = HTTP_METHOD_POST,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .user_data = resp_buf,
        .event_handler = http_event_handler,
        .cert_pem = server_root_cert_pem_start,
        .timeout_ms = 10000,
        .is_async = 0,
    };
	
    char filename[16];
    ESP_LOGI(TAG, "Photo number: %04ld", f_snapcount);
    sprintf(filename, "XX-%04ld.jpg", f_snapcount++);

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_method(client, HTTP_METHOD_POST);

    esp_http_client_set_post_field(client, (const char *)fb->buf, fb->len);
    esp_http_client_set_header(client, "Content-Type", "image/jpg");
    esp_http_client_set_header(client, "X-Device-Id", wifi_sta_mac_str);
    esp_http_client_set_header(client, "X-Filename", filename);	

    int i;
    for (i = 0; i < 3; i++) {
        //dump_memory("client perform");
        err = esp_http_client_perform(client);

        if (err == ESP_OK) {
            int status = esp_http_client_get_status_code(client);
            int64_t content_length = esp_http_client_get_content_length(client);

            ESP_LOGI(TAG, "HTTPS Status = %d, content_length = %lld", status, content_length);

            //ESP_LOG_BUFFER_HEX(TAG, local_response_buffer, strlen(local_response_buffer));
            //dump_log(resp_buf, content_length);

            break;

        } else {
            ESP_LOGE(TAG, "Error perform http request %s", esp_err_to_name(err));
            //dump_memory("failed");

            if ( i < 5) ESP_LOGI(TAG, "Retrying https request");
        }

    }

	
    esp_http_client_cleanup(client);
    //ESP_LOGI(TAG, "end send photo");
    return err;
}


/****
 * WIFI stuff
 ***/
static int s_retry_num = 0;
static EventGroupHandle_t s_wifi_event_group;

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    //ESP_LOGI(TAG, "wifi_event_handler: event_base=%x, event_id=%ld", (int)event_base, event_id);
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        //ESP_LOGI(TAG, "WIFI_STA_START event");
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        //ESP_LOGI(TAG, "WIFI_STA_DISCONNECTED event");
        if (s_retry_num < WIFI_CONN_MAX_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        //ESP_LOGI(TAG, "IP_STA_GOT_IP event");
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static esp_err_t wifi_init_sta(void)
{
    esp_err_t err;
    //ESP_LOGI(TAG, "Start wifi_init_sta");
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    //ESP_ERROR_CHECK(esp_event_loop_create_default());
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "event loop create default err=%x", err);
        return ESP_FAIL;
    }
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
        ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
        IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
	    // TODO how to configure this?
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
	    // TODO make it scan for authmode
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or 
     * connection failed for the maximum number of re-tries (WIFI_FAIL_BIT).
     * The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned,
     * hence we can test which event actually happened. */
    esp_err_t ret;
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID: %s password: %s", WIFI_SSID, WIFI_PASSWORD);
        ret = ESP_OK;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID: %s, password: %s", WIFI_SSID, WIFI_PASSWORD);
        ret = ESP_FAIL;
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
        ret = ESP_FAIL;
    }
    //
    //ESP_LOGI(TAG, "End wifi_init_sta");
    return ret;
}

static esp_err_t get_wifi_mac()
{
    esp_err_t err = esp_read_mac(wifi_sta_mac, ESP_MAC_WIFI_STA);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Unable to read mac");
        return err;
    }

    int i;
    char byte[4];
    for (i = 0; i < 6; i++) {
    sprintf(byte, "%02X", wifi_sta_mac[i]);
    strcat(wifi_sta_mac_str, byte);
    }

    ESP_LOGI(TAG, "wifi_mac_sta_str=%s", wifi_sta_mac_str);
    return ESP_OK;
}

/*****
 * App stuff
 *****/
static camera_fb_t * take_photo()
{
    ESP_LOGI(TAG, "start take picture");

    camera_fb_t * fb = NULL;
    fb = esp_camera_fb_get();
    if (!fb) {
        ESP_LOGE(TAG, "Camera capture buf failed");
        return NULL;
    }

    ESP_LOGI(TAG, "end take picture");

    dump_camera_fb(fb);

    // TODO tune camera sensor
    //ESP_LOGI(TAG, "start get sensor");
    //sensor_t * sensor = esp_camera_sensor_get();
    //ESP_LOGI(TAG, "end get sensor");
    //dump_camera_sensor(sensor);

    return fb;
}

static void take_send_photo(void * pvParameters)
{
    dump_memory("before photo");
    camera_fb_t * fb = NULL;

    fb = take_photo();
    if (fb == NULL) {
	ESP_LOGE(TAG, "Camera capture failed");
	return;
    }

    esp_err_t err = send_photo(fb, post_url);

    if (err != ERR_OK) {
	ESP_LOGE(TAG, "Send photo failed");
    } else {
	ESP_LOGI(TAG, "Took and sent photo!");
	write_nvs();
    }

    esp_camera_fb_return(fb);
    dump_memory("after photo");
	
    vTaskDelete(NULL);
	
}


void app_main(void)
{
    dump_memory("Start app");
  
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_ERROR_CHECK(read_nvs());
    ESP_ERROR_CHECK(init_camera());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(wifi_init_sta());
    ESP_ERROR_CHECK(get_wifi_mac());

    xTaskCreate(&take_send_photo, "take_send_photo", 8192, NULL, 5, NULL);
	
}
