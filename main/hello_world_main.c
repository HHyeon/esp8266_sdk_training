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

#include "esp_spiffs.h"


const char *TAG = "mainsession";



void tcp_server_task(void *arg)
{
    char rx_buffer[128];
    char addr_str[128];
	
	struct sockaddr_in destAddr;
	destAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	destAddr.sin_family = AF_INET;
	destAddr.sin_port = htons(1234);
	inet_ntoa_r(destAddr.sin_addr, addr_str, sizeof(addr_str) - 1);

	int listen_sock = socket(PF_INET, SOCK_STREAM, IPPROTO_IP);
	if (listen_sock < 0) {
		ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
		return;
	}
	ESP_LOGI(TAG, "Socket created");

	int err = bind(listen_sock, (struct sockaddr *)&destAddr, sizeof(destAddr));
	if (err != 0) {
		ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
		return;
	}
	ESP_LOGI(TAG, "Socket binded");
	
	err = listen(listen_sock, 3);
	if (err != 0) {
		ESP_LOGE(TAG, "Error occured during listen: errno %d", errno);
		return;
	}
	ESP_LOGI(TAG, "Socket listening");
	
	while(1)
	{
		struct sockaddr_in sourceAddr;
		uint addrLen = sizeof(sourceAddr);
		int sock = accept(listen_sock, (struct sockaddr *)&sourceAddr, &addrLen);
		if (sock < 0) {
			ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
			continue;
		}
		inet_ntoa_r(((struct sockaddr_in *)&sourceAddr)->sin_addr.s_addr, addr_str, sizeof(addr_str) - 1);
		ESP_LOGI(TAG, "Socket accepted from %s", addr_str);
		
		
		
			
		ESP_LOGI(TAG, "Initializing SPIFFS");

		esp_vfs_spiffs_conf_t conf = {
		  .base_path = "/spiffs",
		  .partition_label = NULL,
		  .max_files = 5,
		  .format_if_mount_failed = true
		};

		// Use settings defined above to initialize and mount SPIFFS filesystem.
		// Note: esp_vfs_spiffs_register is an all-in-one convenience function.
		esp_err_t ret = esp_vfs_spiffs_register(&conf);

		if (ret != ESP_OK) {
			if (ret == ESP_FAIL) {
				ESP_LOGE(TAG, "Failed to mount or format filesystem");
			} else if (ret == ESP_ERR_NOT_FOUND) {
				ESP_LOGE(TAG, "Failed to find SPIFFS partition");
			} else {
				ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
			}
			return;
		}
		
		size_t total = 0, used = 0;
		ret = esp_spiffs_info(NULL, &total, &used);
		if (ret != ESP_OK) {
			ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
		} else {
			ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
		}
		
		
		
		/*
		ESP_LOGI(TAG, "Opening file");
		FILE* file = fopen("/spiffs/img.jpg", "wb");
		if (file == NULL) {
			ESP_LOGE(TAG, "Failed to open file for writing");
			return;
		}
		int totalreceivedbytes = 0;
		int rlen = 1;
		while( rlen > 0)
		{
			rlen = recv(sock, rx_buffer, sizeof(rx_buffer), 0 );
			fwrite(rx_buffer, 1, rlen, file);
			totalreceivedbytes+=rlen;
			//ESP_LOGI(TAG, "%d", totalreceivedbytes);
			if(rlen!=sizeof(rx_buffer)) break;
		}
		fclose(file);		
		ESP_LOGI(TAG, "totalreceivedbytes : %d", totalreceivedbytes);
		*/
		
		FILE* file = fopen("/spiffs/img.jpg", "rb");
		if (file == NULL) {
			ESP_LOGE(TAG, "Failed to open file for reading");
			return;
		}
		int totalsentbytes = 0;
		int rlen = 1, wlen;
		while(rlen)
		{
			rlen = fread(rx_buffer, 1, sizeof(rx_buffer), file);
			wlen = send(sock, rx_buffer, rlen, 0);
			totalsentbytes += wlen;
		}
		fclose(file);
		ESP_LOGI(TAG, "sent : %d", totalsentbytes);

			
		esp_vfs_spiffs_unregister(NULL);
		
        if (sock != -1) {
            shutdown(sock, 0);
            close(sock);
        }
	}
	
	ESP_LOGI(TAG, "Socket server terminated");
	
    vTaskDelete(NULL);
}


void gpio_oneshot(void *arg)
{
	gpio_set_level(GPIO_NUM_5, 1);
	vTaskDelay(500 / portTICK_PERIOD_MS);
	gpio_set_level(GPIO_NUM_5, 0);

	vTaskDelete(NULL);
}

esp_err_t hello_type_get_handler(httpd_req_t *req)
{
	size_t qrylen = httpd_req_get_url_query_len(req);
	char *buf = malloc(qrylen+1);
	httpd_req_get_url_query_str(req, buf, qrylen+1);
	buf[qrylen] = 0;
	
	ESP_LOGI(TAG, "querystr - %d:%s", qrylen, buf);

	if(strcmp(buf, "img") == 0)
	{	
		ESP_LOGI(TAG, "Initializing SPIFFS");

		esp_vfs_spiffs_conf_t conf = {
		  .base_path = "/spiffs",
		  .partition_label = NULL,
		  .max_files = 5,
		  .format_if_mount_failed = true
		};

		// Use settings defined above to initialize and mount SPIFFS filesystem.
		// Note: esp_vfs_spiffs_register is an all-in-one convenience function.
		esp_err_t ret = esp_vfs_spiffs_register(&conf);

		if (ret != ESP_OK) {
			if (ret == ESP_FAIL) {
				ESP_LOGE(TAG, "Failed to mount or format filesystem");
			} else if (ret == ESP_ERR_NOT_FOUND) {
				ESP_LOGE(TAG, "Failed to find SPIFFS partition");
			} else {
				ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
			}

			const char * STR = "spiffs failed";
			httpd_resp_set_type(req, HTTPD_TYPE_TEXT);
			httpd_resp_send(req, STR, strlen(STR));
		}
		else
		{
			size_t total = 0, used = 0;
			ret = esp_spiffs_info(NULL, &total, &used);
			if (ret != ESP_OK) {
				ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
			} else {
				ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
			}
			
			FILE* file = fopen("/spiffs/img.jpg", "rb");
			
			if (file == NULL) {
				ESP_LOGE(TAG, "Failed to open file for reading");
			}
			else
			{
				fseek(file, 0, SEEK_END);
				int fsiz = ftell(file);
				fseek(file, 0, SEEK_SET);

				char *txbuf = malloc(512);

				httpd_resp_set_type(req, HTTPD_TYPE_IMAGE_JPEG);
				httpd_resp_send_hdr_only(req, fsiz);

				ESP_LOGI(TAG, "sending image %d bytes", fsiz);
				int rlen = 1;
				while(rlen)
				{
					rlen = fread(txbuf, 1, sizeof(txbuf), file);
					if(httpd_resp_send_buf(req, txbuf, rlen)!=ESP_OK)
						break;
				}

				free(txbuf);
				fclose(file);
				
				ESP_LOGI(TAG, "sending image completed");
			}
			
			esp_vfs_spiffs_unregister(NULL);

		}
	}
	else if(strcmp(buf, "test") == 0)
	{
		const char *STR = "hello!";
		httpd_resp_set_type(req, HTTPD_TYPE_TEXT_PLAIN);
		httpd_resp_send_hdr_only(req, 6);
		httpd_resp_send_buf(req, STR, strlen(STR));
	}
	else
	{
		const char * STR = "Hello World!";
		httpd_resp_set_type(req, HTTPD_TYPE_TEXT);
		httpd_resp_send(req, STR, strlen(STR));
	}
	
    free (buf);
	return ESP_OK;
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
	
    if (*server == NULL) {
        ESP_LOGI(TAG, "Starting webserver");
        *server = start_httpd();
    }
}

void wifi_event_disconnected(httpd_handle_t* server)
{
	gpio_set_level(GPIO_NUM_2, 0);
	
    if (*server) {
        ESP_LOGI(TAG, "Stopping webserver");
        stop_httpd(*server);
        *server = NULL;
    }
}

void app_main()
{
	ESP_LOGI(TAG, "ESP8266 app_main Started");
	
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
	
    xTaskCreate(tcp_server_task, "tcp_server", 2048, NULL, 5, NULL);
	
}
