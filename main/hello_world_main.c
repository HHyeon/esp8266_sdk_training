/* Hello World Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>

#include "protocol_examples_common.h"
#include "sdkconfig.h"

#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_event_loop.h"
#include "esp_system.h"

#include "tcpip_adapter.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

#include "driver/gpio.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#include "esp_netif.h"
#include "nvs.h"
#include "nvs_flash.h"

#include <esp_http_server.h>
#include <http_parser.h>



const char *TAG = "mainsession";

EventGroupHandle_t wifi_connection_groupevt;

static void tcp_client_task(void *arg)
{
    char rx_buffer[128];
    char addr_str[128];

	ESP_LOGI(TAG, "tcp_client_task started");
		
	while(1)
	{
		EventBits_t evtbit = xEventGroupGetBits(wifi_connection_groupevt);

		if(evtbit>>0)
		{
			xEventGroupClearBits(wifi_connection_groupevt, BIT(0));
			break;
		}
	
		struct sockaddr_in destAddr;
		destAddr.sin_addr.s_addr = inet_addr("192.168.0.28");
	    destAddr.sin_family = AF_INET;
	    destAddr.sin_port = htons(9009);
		inet_ntoa_r(destAddr.sin_addr, addr_str, sizeof(addr_str) - 1);

		int sock =	socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
		if(sock < 0)
		{
			ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
			goto quit;
		}
		
		int err = connect(sock, (struct sockaddr *)&destAddr, sizeof(destAddr));
	    if (err != 0) {
	        ESP_LOGE(TAG, "Socket unable to connect: errno %d", errno);
	        close(sock);
			vTaskDelay(100 / portTICK_PERIOD_MS);
			continue;
	    }
		
		//ESP_LOGI(TAG, "Successfully connected");

		
		int len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
		
		if (len < 0) {
			ESP_LOGE(TAG, "recv failed: errno %d", errno);
			goto quit;
		}

		rx_buffer[len] = 0;
		ESP_LOGI(TAG, "recv - %s", rx_buffer);
		
		static const char *payload = "Message from ESP32 ";
		
	    err = send(sock, payload, strlen(payload), 0);
	    if (err < 0) {
	        ESP_LOGE(TAG, "Error occured during sending: errno %d", errno);
			goto quit;
	    }

quit:

	    if (sock != -1) {
	        shutdown(sock, 0);
	        close(sock);
		
			vTaskDelay(1000 / portTICK_PERIOD_MS);

	    }
	}

	ESP_LOGI(TAG, "tcp_client_task terminated");

    vTaskDelete(NULL);
	
}

EventGroupHandle_t gpiomonevt;

static void gpiowrtiteout_task(void *arg)
{
	while(1)
	{
		EventBits_t evtbit = xEventGroupGetBits(gpiomonevt);
		
		if((evtbit>>GPIO_NUM_4) & 1)
			gpio_set_level(GPIO_NUM_5, 1);
		else
			gpio_set_level(GPIO_NUM_5, 0);
			
	
		vTaskDelay(50 / portTICK_PERIOD_MS);
	}
}


static void gpioreadin_task(void *arg)
{
	while(1)
	{
		if(gpio_get_level(GPIO_NUM_4))
			xEventGroupSetBits(gpiomonevt, BIT(GPIO_NUM_4));
		else
			xEventGroupClearBits(gpiomonevt, BIT(GPIO_NUM_4));
		
		vTaskDelay(50 / portTICK_PERIOD_MS);
	}
}

esp_err_t hello_type_get_handler(httpd_req_t *req)
{
	size_t qrylen = httpd_req_get_url_query_len(req);
	char *buf = malloc(qrylen+1);
	httpd_req_get_url_query_str(req, buf, qrylen+1);
	buf[qrylen] = 0;
	
	ESP_LOGI(TAG, "querystr - %d:%s", qrylen, buf);
	
    free (buf);
	
#define STR "Hello World!"
    httpd_resp_set_type(req, HTTPD_TYPE_TEXT);
    httpd_resp_send(req, STR, strlen(STR));
    return ESP_OK;
#undef STR
}

httpd_uri_t basic_handlers = {
		.uri = "/test",
		.method = HTTP_GET,
		.handler = hello_type_get_handler,
		.user_ctx = NULL
};


void register_basic_handlers(httpd_handle_t hd)
{
    ESP_LOGI(TAG, "Registering basic handlers");
    if (httpd_register_uri_handler(hd, &basic_handlers) != ESP_OK) {
        ESP_LOGW(TAG, "register uri failed");
        return;
    }
    ESP_LOGI(TAG, "Success");
}


int pre_start_mem, post_stop_mem;
httpd_handle_t start_httpd()
{
    pre_start_mem = esp_get_free_heap_size();
    httpd_handle_t hd = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 8080;

    /* This check should be a part of http_server */
    config.max_open_sockets = (CONFIG_LWIP_MAX_SOCKETS - 3);

    if (httpd_start(&hd, &config) == ESP_OK) {
        ESP_LOGI(TAG, "Started HTTP server on port: '%d'", config.server_port);
        ESP_LOGI(TAG, "Max URI handlers: '%d'", config.max_uri_handlers);
        ESP_LOGI(TAG, "Max Open Sessions: '%d'", config.max_open_sockets);
        ESP_LOGI(TAG, "Max Header Length: '%d'", HTTPD_MAX_REQ_HDR_LEN);
        ESP_LOGI(TAG, "Max URI Length: '%d'", HTTPD_MAX_URI_LEN);
        ESP_LOGI(TAG, "Max Stack Size: '%d'", config.stack_size);
    }
	
    if (hd) {
        register_basic_handlers(hd);
    }
	
    return hd;
}

void stop_httpd(httpd_handle_t hd)
{
    ESP_LOGI(TAG, "Stopping httpd");
    httpd_stop(hd);
    post_stop_mem = esp_get_free_heap_size();
    ESP_LOGI(TAG, "HTTPD Stop: Current free memory: %d", post_stop_mem);
}



void wifi_event_got_ip(httpd_handle_t* server)
{
	gpio_set_level(GPIO_NUM_2, 1);
	
	
	/*xEventGroupClearBits(wifi_connection_groupevt, BIT(0));
    xTaskCreate(tcp_client_task, "tcp_client", 4096, NULL, 5, NULL);*/

    if (*server == NULL) {
        ESP_LOGI(TAG, "Starting webserver");
        *server = start_httpd();
    }

}

void wifi_event_disconnected(httpd_handle_t* server)
{
	gpio_set_level(GPIO_NUM_2, 0);
	
	/*xEventGroupSetBits(wifi_connection_groupevt, BIT(0));*/
	
    if (*server) {
        ESP_LOGI(TAG, "Stopping webserver");
        stop_httpd(*server);
        *server = NULL;
    }
}

void app_main()
{
	wifi_connection_groupevt = xEventGroupCreate();
	gpiomonevt = xEventGroupCreate();

	ESP_LOGI(TAG, "ESP8266 app_main Started");
	xTaskCreate(gpiowrtiteout_task, "gpiowrtiteout task", 1024, NULL, 10, NULL);
	xTaskCreate(gpioreadin_task, "gpioreadin_task", 1024, NULL, 10, NULL);
	
	gpio_set_direction(GPIO_NUM_5, GPIO_MODE_OUTPUT);
	gpio_set_intr_type(GPIO_NUM_5, GPIO_INTR_DISABLE);
	gpio_set_pull_mode(GPIO_NUM_5, GPIO_FLOATING);
	
	gpio_set_direction(GPIO_NUM_2, GPIO_MODE_OUTPUT);
	gpio_set_intr_type(GPIO_NUM_2, GPIO_INTR_DISABLE);
	gpio_set_pull_mode(GPIO_NUM_2, GPIO_FLOATING);
	gpio_set_level(GPIO_NUM_2, 0);

	gpio_config_t io_conf;
	io_conf.mode = GPIO_MODE_INPUT;
	io_conf.pin_bit_mask = BIT(GPIO_NUM_4);
	io_conf.pull_down_en = 1;
	gpio_config(&io_conf);

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
	example_set_connection_info("ipTIMEA2003NS", "hyeon__1123");
	ESP_ERROR_CHECK(example_connect());
	
}
