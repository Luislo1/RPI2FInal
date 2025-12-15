/* MQTT (over TCP) Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"
#include <time.h>
#include "cJSON.h"
#include "nvs.h"

#include "esp_log.h"
#include "mqtt_client.h"

#define TOPIC "EDIFICIO_3/P_4/N/12"

static bool sensor_active = true;
TaskHandle_t sensor_handle = NULL;

static const char *TAG = "mqtt_example";

esp_mqtt_client_handle_t client; 


// Settings from ThingsBoard Device Profile (Step 1)
#define PROVISION_KEY    "mjcevunkll3fz3q6dhmb"
#define PROVISION_SECRET "coh9hpv4r8t0naj8un9g"
#define DEVICE_NAME      "ESP32_New_Device_001" // Or generate this dynamically from MAC address

// Global to hold the final token
char access_token[128] = {0};
bool is_provisioning_mode = false;
int intervalo_envio = 5000;

void save_token_to_nvs(const char *token) {
    nvs_handle_t my_handle;
    ESP_ERROR_CHECK(nvs_open("storage", NVS_READWRITE, &my_handle));
    ESP_ERROR_CHECK(nvs_set_str(my_handle, "tb_token", token));
    ESP_ERROR_CHECK(nvs_commit(my_handle));
    nvs_close(my_handle);
}

bool load_token_from_nvs() {
    nvs_handle_t my_handle;
    esp_err_t err = nvs_open("storage", NVS_READONLY, &my_handle);
    if (err != ESP_OK) return false;

    size_t required_size;
    err = nvs_get_str(my_handle, "tb_token", NULL, &required_size);
    if (err == ESP_OK && required_size < sizeof(access_token)) {
        nvs_get_str(my_handle, "tb_token", access_token, &required_size);
        nvs_close(my_handle);
        return true; 
    }
    nvs_close(my_handle);
    return false;
}

static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0) {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}

/*
 * @brief Event handler registered to receive MQTT events
 *
 *  This function is called by the MQTT client event loop.
 *
 * @param handler_args user data registered to the event.
 * @param base Event base for the handler(always MQTT Base in this example).
 * @param event_id The id for the received event.
 * @param event_data The data for the event, esp_mqtt_event_handle_t.
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32 "", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    client = event->client;
    int msg_id;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT Connected");
        
        if (is_provisioning_mode) {
            // 1. Subscribe to response
            esp_mqtt_client_subscribe(client, "/provision/response/+", 1);

            // 2. Prepare JSON Request
            char payload[256];
            snprintf(payload, sizeof(payload), 
                "{\"deviceName\": \"%s\", \"provisionDeviceKey\": \"%s\", \"provisionDeviceSecret\": \"%s\"}",
                DEVICE_NAME, PROVISION_KEY, PROVISION_SECRET);

            // 3. Send Request
            esp_mqtt_client_publish(client, "/provision/request", payload, 0, 0, 0);
            ESP_LOGI(TAG, "Sent Provisioning Request...");
        } else {
                // 1. Subscribe to Attribute Updates (Live changes)
            esp_mqtt_client_subscribe(client, "v1/devices/me/attributes", 1);

            // 2. Subscribe to Attribute Request Responses (Initial read)
            esp_mqtt_client_subscribe(client, "v1/devices/me/attributes/response/+", 1);

            // 3. Request the current value of 'intervalo_envio'
            // We use request ID '1'
            const char *req_payload = "{\"sharedKeys\":\"intervalo_envio\"}";
            esp_mqtt_client_publish(client, "v1/devices/me/attributes/request/1", req_payload, 0, 0, 0);
            
            ESP_LOGI(TAG, "Subscribed to attributes and requested current value");
        }
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        msg_id = esp_mqtt_client_publish(client, "/topic/qos0", "data", 0, 0, 0);
        ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        if (is_provisioning_mode) {
            printf("Provision Response: %.*s\r\n", event->data_len, event->data);
            
            // Parse JSON to get credentialsValue
            cJSON *root = cJSON_ParseWithLength(event->data, event->data_len);
            cJSON *status = cJSON_GetObjectItem(root, "status");
            
            if (status && strcmp(status->valuestring, "SUCCESS") == 0) {
                cJSON *tokenItem = cJSON_GetObjectItem(root, "credentialsValue");
                if (tokenItem) {
                    ESP_LOGI(TAG, "TOKEN RECEIVED: %s", tokenItem->valuestring);
                    
                    // Save to NVS
                    save_token_to_nvs(tokenItem->valuestring);
                    
                    // RESTART to load new token and run normally
                    ESP_LOGI(TAG, "Restarting in 2 seconds...");
                    vTaskDelay(2000 / portTICK_PERIOD_MS);
                    esp_restart();
                }
            }
            cJSON_Delete(root);
        }
        else {
        printf("Received Data: %.*s\r\n", event->data_len, event->data);
        
        // Parse JSON
        cJSON *root = cJSON_ParseWithLength(event->data, event->data_len);
        if (root == NULL) break;

        // Check if the payload contains our attribute directly
        // (This happens on live updates via 'v1/devices/me/attributes')
        cJSON *intervalItem = cJSON_GetObjectItem(root, "intervalo_envio");
        
        // If not found directly, it might be inside a "shared" object
        // (This happens on response to request 'v1/devices/me/attributes/response/+')
        if (!intervalItem) {
            cJSON *shared = cJSON_GetObjectItem(root, "shared");
            if (shared) {
                intervalItem = cJSON_GetObjectItem(shared, "intervalo_envio");
            }
        }

        // If we found the item and it is a number, update the variable
        if (intervalItem && cJSON_IsNumber(intervalItem)) {
            intervalo_envio = intervalItem->valueint;
            ESP_LOGI(TAG, "NEW INTERVAL SET: %d ms", intervalo_envio);
        }

        cJSON_Delete(root);
    }
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno",  event->error_handle->esp_transport_sock_errno);
            ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));

        }
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

static void mqtt_app_start(void)
{
    // Try to load token
    if (load_token_from_nvs()) {
        ESP_LOGI(TAG, "Token found in NVS: %s", access_token);
        is_provisioning_mode = false;
    } else {
        ESP_LOGW(TAG, "No token found. Starting Provisioning Mode.");
        is_provisioning_mode = true;
    }

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = "mqtt://demo.thingsboard.io",
        // If provisioning, username is "provision". If normal, it is the Token.
        .credentials.username = is_provisioning_mode ? "provision" : access_token, 
    };

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
}

float random_float(float min, float max) {
    return ((float)rand() / RAND_MAX) * (max - min) + min;
}

void generar_datos(float *temp, float *hum, float *lux, float *vibr) {
    *temp = random_float(15.0, 30.0);
    *hum  = random_float(30.0, 80.0);
    *lux  = random_float(100.0, 1000.0);
    *vibr = random_float(0.0, 10.0);
}

void publisher(void *pvParameters) {
    int msg_id;
    srand(time(NULL));
    vTaskDelay(500 / portTICK_PERIOD_MS);

    // ThingsBoard requires this specific topic for telemetry
    const char *tb_topic = "v1/devices/me/telemetry";

    while (1) {
        if (sensor_active) {
            float temp, hum, lux, vibr;
            generar_datos(&temp, &hum, &lux, &vibr);

            // Create a JSON string. 
            // Example output: {"temperature": 23.5, "humidity": 50.2, "lux": 500.0, "vibration": 1.2}
            char payload[128]; 
            snprintf(payload, sizeof(payload), 
                     "{\"temperature\": %.2f, \"humidity\": %.2f, \"lux\": %.2f, \"vibration\": %.2f}", 
                     temp, hum, lux, vibr);

            // Publish to ThingsBoard
            // QoS 0 or 1 is fine. Retain should usually be 0 for telemetry.
            msg_id = esp_mqtt_client_publish(client, tb_topic, payload, 0, 1, 0);
            
            ESP_LOGI(TAG, "Sent JSON to ThingsBoard: %s", payload);
        } else {
            ESP_LOGI(TAG, "Sensor disabled, skipping publish");
        }
        
        vTaskDelay(intervalo_envio / portTICK_PERIOD_MS);
    }
    vTaskDelete(NULL);
}


void app_main(void)
{
    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("mqtt_client", ESP_LOG_VERBOSE);
    esp_log_level_set("mqtt_example", ESP_LOG_VERBOSE);
    esp_log_level_set("transport_base", ESP_LOG_VERBOSE);
    esp_log_level_set("esp-tls", ESP_LOG_VERBOSE);
    esp_log_level_set("transport", ESP_LOG_VERBOSE);
    esp_log_level_set("outbox", ESP_LOG_VERBOSE);

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    ESP_ERROR_CHECK(example_connect());
    mqtt_app_start();
    xTaskCreate(&publisher, "Sensor", 4096, NULL, 0, &sensor_handle);
}
