#include <freertos/FreeRTOS.h>
#include <esp_wifi.h>
#include <esp_system.h>
#include <esp_event.h>
#include <esp_event_loop.h>
#include <nvs_flash.h>
#include <driver/gpio.h>
#include <driver/adc.h>
#include <freertos/portmacro.h>
#include <freertos/event_groups.h>
#include <esp_log.h>
#include <tcpip_adapter.h>
static EventGroupHandle_t wifi_event_group;
const int CONNECTED_BIT = BIT0;
//static char* TAG = "app_main";

#include <lwip/err.h>
#include <string.h>

#include <cJSON.h>

#define LED_BUILTIN 16
#define delay(ms) (vTaskDelay(ms/portTICK_RATE_MS))

char* json_unformatted;

wifi_config_t sta_config = {
    .sta = {
        .ssid = "ICTEX51",
        .password = "espwroom32",
        .bssid_set = false
    }
};


const static char http_html_hdr[] =
    "HTTP/1.1 200 OK\r\nContent-type: text/html\r\n\r\n";
const static char http_index_hml[] = "<!DOCTYPE html>"
      "<html>\n"
      "<head>\n"
      "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
      "  <style type=\"text/css\">\n"
      "    html, body, iframe { margin: 0; padding: 0; height: 100%; }\n"
      "    iframe { display: block; width: 100%; border: none; }\n"
      "  </style>\n"
      "<title>HELLO ESP32</title>\n"
      "</head>\n"
      "<body>\n"
      "<h1>Hello World, from ESP32!</h1>\n"
      "<a href=\"h\">HTML</a>\n"
      "<a href=\"j\">JSON</a>\n"
      "</body>\n"
      "</html>\n";


#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/api.h"

static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    switch(event->event_id) {
    case SYSTEM_EVENT_STA_START:
        esp_wifi_connect();
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
        xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
        printf("got ip\n");
        printf("ip: " IPSTR "\n", IP2STR(&event->event_info.got_ip.ip_info.ip));
        printf("netmask: " IPSTR "\n", IP2STR(&event->event_info.got_ip.ip_info.netmask));
        printf("gw: " IPSTR "\n", IP2STR(&event->event_info.got_ip.ip_info.gw));
        printf("\n");
        fflush(stdout);
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        /* This is a workaround as ESP32 WiFi libs don't currently
           auto-reassociate. */
        esp_wifi_connect();
        xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
        break;
    default:
        break;
    }
    return ESP_OK;
}

static void initialise_wifi(void)
{
    tcpip_adapter_init();
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK( esp_event_loop_init(event_handler, NULL) );
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );

    ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_STA, &sta_config) );
    ESP_ERROR_CHECK( esp_wifi_start() );
}

static void
http_server_netconn_serve(struct netconn *conn)
{
  struct netbuf *inbuf;
  char *buf;
  u16_t buflen;
  err_t err;

  /* Read the data from the port, blocking if nothing yet there.
   We assume the request (the part we care about) is in one netbuf */
  err = netconn_recv(conn, &inbuf);

  if (err == ERR_OK) {
    netbuf_data(inbuf, (void**)&buf, &buflen);

    // strncpy(_mBuffer, buf, buflen);

    /* Is this an HTTP GET command? (only check the first 5 chars, since
    there are other formats for GET, and we're keeping it very simple )*/
    printf("buffer = %s \n", buf);
    if (buflen>=5 &&
        buf[0]=='G' &&
        buf[1]=='E' &&
        buf[2]=='T' &&
        buf[3]==' ' &&
        buf[4]=='/' ) {
          printf("buf[5] = %c\n", buf[5]);
      /* Send the HTML header
             * subtract 1 from the size, since we dont send the \0 in the string
             * NETCONN_NOCOPY: our data is const static, so no need to copy it
       */

      netconn_write(conn, http_html_hdr, sizeof(http_html_hdr)-1, NETCONN_NOCOPY);

      if(buf[5]=='h') {
        gpio_set_level(LED_BUILTIN, 0);
        /* Send our HTML page */
        netconn_write(conn, http_index_hml, sizeof(http_index_hml)-1, NETCONN_NOCOPY);
      }
      else if(buf[5]=='l') {
        gpio_set_level(LED_BUILTIN, 1);
        /* Send our HTML page */
        netconn_write(conn, http_index_hml, sizeof(http_index_hml)-1, NETCONN_NOCOPY);
      }
      else if(buf[5]=='j') {
    	  netconn_write(conn, json_unformatted, strlen(json_unformatted), NETCONN_NOCOPY);
      }
      else {
          netconn_write(conn, http_index_hml, sizeof(http_index_hml)-1, NETCONN_NOCOPY);
      }
    }

  }
  /* Close the connection (server closes in HTTP) */
  netconn_close(conn);

  /* Delete the buffer (netconn_recv gives us ownership,
   so we have to make sure to deallocate the buffer) */
  netbuf_delete(inbuf);
}

static void http_server(void *pvParameters)
{
  struct netconn *conn, *newconn;
  err_t err;
  conn = netconn_new(NETCONN_TCP);
  netconn_bind(conn, NULL, 80);
  netconn_listen(conn);
  do {
     err = netconn_accept(conn, &newconn);
     if (err == ERR_OK) {
       http_server_netconn_serve(newconn);
       netconn_delete(newconn);
     }
   } while(err == ERR_OK);
   netconn_close(conn);
   netconn_delete(conn);
}

static void generate_json() {
	cJSON *root, *info, *d;

	root = cJSON_CreateObject();
	cJSON_AddItemToObject(root, "d", d = cJSON_CreateObject());
	cJSON_AddItemToObject(root, "info", info = cJSON_CreateObject());

	cJSON_AddStringToObject(d, "myName", "ESP WROOM 32");
	cJSON_AddNumberToObject(d, "ADC", 0);

	cJSON_AddStringToObject(info, "ssid", (char*)sta_config.sta.ssid);
	cJSON_AddNumberToObject(info, "heap", esp_get_free_heap_size());
	cJSON_AddStringToObject(info, "sdk", esp_get_idf_version());
	cJSON_AddNumberToObject(info, "time", esp_timer_get_time());

	while (1) {
        int ad = adc1_get_raw(ADC1_CHANNEL_6);
    	cJSON_ReplaceItemInObject(d, "ADC", cJSON_CreateNumber(ad));
		cJSON_ReplaceItemInObject(info, "heap",
				cJSON_CreateNumber(esp_get_free_heap_size()));
		cJSON_ReplaceItemInObject(info, "time",
				cJSON_CreateNumber(esp_timer_get_time()));
		cJSON_ReplaceItemInObject(info, "sdk",
				cJSON_CreateString(esp_get_idf_version()));

		json_unformatted = cJSON_PrintUnformatted(root);
		printf("[len = %d]  ", strlen(json_unformatted));

		for (int var = 0; var < strlen(json_unformatted); ++var) {
			putc(json_unformatted[var], stdout);
		}

		printf("\n");
		fflush(stdout);
		delay(2000);
		free(json_unformatted);
	}
}

int app_main(void)
{
    nvs_flash_init();
    initialise_wifi();
    esp_timer_init();

    ESP_LOGI("main", "Initialize ADC");
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC1_CHANNEL_6, ADC_ATTEN_DB_11);

    gpio_pad_select_gpio(LED_BUILTIN);
    /* Set the GPIO as a push/pull output */
    gpio_set_direction(LED_BUILTIN, GPIO_MODE_OUTPUT);
    xTaskCreate(&generate_json, "json", 2048, NULL, 5, NULL);
    xTaskCreate(&http_server, "http_server", 2048, NULL, 5, NULL);
    return 0;
}

