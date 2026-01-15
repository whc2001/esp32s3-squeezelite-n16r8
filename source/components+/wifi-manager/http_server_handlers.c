/*
Copyright (c) 2017-2021 Sebastien L
*/

#include "http_server_handlers.h"
#include "esp_http_server.h"
#include "cmd_system.h"
#include <inttypes.h>
#include "squeezelite-ota.h"
#include "nvs_utilities.h"
#include <stdio.h>
#include <stdlib.h>
#include "cJSON.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "platform_config.h"
#include "sys/param.h"
#include "esp_vfs.h"
#include "messaging.h"
#include "platform_esp32.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include "platform_console.h"
#include "accessors.h"
#include "webapp/webpack.h"
#include "network_wifi.h"
#include "network_status.h"
#include "tools.h"
#include "esp_netif.h"
// tcpip_adapter compatibility for IDF 5.x
#define TCPIP_ADAPTER_IF_AP ESP_IF_WIFI_AP
#define TCPIP_ADAPTER_IF_STA ESP_IF_WIFI_STA

extern esp_netif_t* wifi_netif;
extern esp_netif_t* wifi_ap_netif;
static inline bool tcpip_adapter_is_netif_up(int interface) {
    esp_netif_t* netif = (interface == ESP_IF_WIFI_AP) ? wifi_ap_netif : wifi_netif;
    return netif ? esp_netif_is_netif_up(netif) : false;
}
static inline esp_err_t tcpip_adapter_get_ip_info(int interface, tcpip_adapter_ip_info_t *ip_info) {
    esp_netif_t* netif = (interface == ESP_IF_WIFI_AP) ? wifi_ap_netif : wifi_netif;
    return netif ? esp_netif_get_ip_info(netif, ip_info) : ESP_FAIL;
}
static inline esp_err_t tcpip_adapter_get_hostname(int interface, const char **hostname) {
    esp_netif_t* netif = (interface == ESP_IF_WIFI_AP) ? wifi_ap_netif : wifi_netif;
    return netif ? esp_netif_get_hostname(netif, hostname) : ESP_FAIL;
}

extern cJSON * get_gpio_list(bool refresh);

/* @brief tag used for ESP serial console messages */
static const char TAG[] = "httpd_handlers";

// Pool of number buffers to avoid static buffer overwrite issue
#define NUM_BUF_COUNT 16
#define NUM_BUF_SIZE 32
static char num_buf_pool[NUM_BUF_COUNT][NUM_BUF_SIZE];
static int num_buf_index = 0;

// Helper to get string value from cJSON config item
static char* get_config_str(cJSON* config, const char* key) {
    cJSON* item = cJSON_GetObjectItemCaseSensitive(config, key);
    if (!item) return NULL;
    cJSON* val = cJSON_GetObjectItemCaseSensitive(item, "value");
    if (!val) return NULL;
    if (cJSON_IsString(val)) return val->valuestring;
    if (cJSON_IsNumber(val)) {
        // Use rotating buffer pool to avoid overwriting previous values
        char* buf = num_buf_pool[num_buf_index];
        num_buf_index = (num_buf_index + 1) % NUM_BUF_COUNT;
        snprintf(buf, NUM_BUF_SIZE, "%d", val->valueint);
        return buf;
    }
    return NULL;
}

// Helper to check if key exists in config
static bool has_config_key(cJSON* config, const char* key) {
    return cJSON_GetObjectItemCaseSensitive(config, key) != NULL;
}

// Helper to check if cmdname matches a pattern
static bool cmdname_is(const char* cmdname, const char* pattern) {
    if (!cmdname) return false;
    return strstr(cmdname, pattern) != NULL;
}

// Helper to mark a config key as processed (will be skipped in main loop)
static void mark_as_processed(cJSON* config, const char* key) {
    cJSON* item = cJSON_GetObjectItemCaseSensitive(config, key);
    if (item && item->string) {
        // Rename key to __skip__ prefix so main loop skips it
        char* new_name = malloc(strlen(key) + 10);
        if (new_name) {
            sprintf(new_name, "__skip__%s", key);
            free(item->string);
            item->string = new_name;
        }
    }
}

// Helper to check if key should be skipped
static bool should_skip_key(const char* key) {
    return key && strncmp(key, "__skip__", 8) == 0;
}

// Process complex configurations that need multiple fields combined into one NVS string
// Returns number of fields processed (removed from further processing)
static int process_complex_configs(cJSON* config) {
    int processed = 0;
    char buf[512];
    
    // Get command name for context
    char* cmdname = get_config_str(config, "__cmdname__");
    if (cmdname) {
        ESP_LOGI(TAG, "Processing config for command: %s", cmdname);
    }
    mark_as_processed(config, "__cmdname__");
    
    // ===== I2C CONFIG (i2c_config) =====
    // Command: cfg-hw-i2c, Fields: scl, sda, speed, port -> "scl=X,sda=Y,speed=Z,port=W"
    if (cmdname_is(cmdname, "i2c")) {
        char* scl = get_config_str(config, "scl");
        char* sda = get_config_str(config, "sda");
        char* speed = get_config_str(config, "speed");
        char* port = get_config_str(config, "port");
        
        if (scl && sda) {
            snprintf(buf, sizeof(buf), "scl=%s,sda=%s", scl, sda);
            if (speed && strlen(speed) > 0) {
                char tmp[64];
                snprintf(tmp, sizeof(tmp), ",speed=%s", speed);
                strcat(buf, tmp);
            }
            if (port && strlen(port) > 0) {
                char tmp[64];
                snprintf(tmp, sizeof(tmp), ",port=%s", port);
                strcat(buf, tmp);
            }
            ESP_LOGI(TAG, "Built i2c_config: %s", buf);
            config_set_value_forced(NVS_TYPE_STR, "i2c_config", buf);
            processed += 4;
        }
        mark_as_processed(config, "scl");
        mark_as_processed(config, "sda");
        mark_as_processed(config, "speed");
        mark_as_processed(config, "port");
        return processed;
    }
    
    // ===== SPI CONFIG (spi_config) =====
    // Command: cfg-hw-spi, Fields: data, clk, dc, host -> "data=X,clk=Y,dc=Z,host=W"
    if (cmdname_is(cmdname, "spi")) {
        ESP_LOGI(TAG, "SPI handler matched cmdname=%s", cmdname ? cmdname : "null");
        char* data = get_config_str(config, "data");
        char* clk = get_config_str(config, "clk");
        char* dc = get_config_str(config, "dc");
        char* host = get_config_str(config, "host");
        ESP_LOGI(TAG, "SPI fields: data=%s, clk=%s, dc=%s, host=%s", 
                 data ? data : "null", clk ? clk : "null", dc ? dc : "null", host ? host : "null");
        
        if (data && clk) {
            snprintf(buf, sizeof(buf), "data=%s,clk=%s", data, clk);
            if (dc && strlen(dc) > 0) {
                char tmp[64];
                snprintf(tmp, sizeof(tmp), ",dc=%s", dc);
                strcat(buf, tmp);
            }
            if (host && strlen(host) > 0) {
                char tmp[64];
                snprintf(tmp, sizeof(tmp), ",host=%s", host);
                strcat(buf, tmp);
            }
            ESP_LOGI(TAG, "Built spi_config: %s", buf);
            config_set_value_forced(NVS_TYPE_STR, "spi_config", buf);
            processed += 4;
        }
        mark_as_processed(config, "data");
        mark_as_processed(config, "clk");
        mark_as_processed(config, "dc");
        mark_as_processed(config, "host");
        return processed;
    }
    
    // ===== DISPLAY CONFIG (display_config) =====
    // Command: cfg-hw-display, Fields: type, width, height, cs, reset, back, speed, mode, driver, rotate, hf, vf, invert, address, depth
    if (cmdname_is(cmdname, "display")) {
        char* type = get_config_str(config, "type");
        char* width = get_config_str(config, "width");
        char* height = get_config_str(config, "height");
        char* cs = get_config_str(config, "cs");
        char* reset = get_config_str(config, "reset");
        char* back = get_config_str(config, "back");
        char* speed = get_config_str(config, "speed");
        char* mode = get_config_str(config, "mode");
        char* driver = get_config_str(config, "driver");
        char* rotate = get_config_str(config, "rotate");
        char* hf = get_config_str(config, "hf");
        char* vf = get_config_str(config, "vf");
        char* invert = get_config_str(config, "invert");
        char* address = get_config_str(config, "address");
        char* depth = get_config_str(config, "depth");
        
        if (type && width && height) {
            // Format: TYPE,width=X,height=Y[,cs=Z][,reset=R][,back=B][,speed=S][,mode=M],driver=D[,HFlip][,VFlip][,rotate][,invert]
            snprintf(buf, sizeof(buf), "%s,width=%s,height=%s", type, width, height);
            
            if (strcasecmp(type, "I2C") == 0 && address && strlen(address) > 0) {
                char tmp[64];
                snprintf(tmp, sizeof(tmp), ",address=%s", address);
                strcat(buf, tmp);
            }
            if (strcasecmp(type, "SPI") == 0 && cs && strlen(cs) > 0 && strcmp(cs, "-1") != 0) {
                char tmp[64];
                snprintf(tmp, sizeof(tmp), ",cs=%s", cs);
                strcat(buf, tmp);
            }
            if (reset && strlen(reset) > 0 && strcmp(reset, "-1") != 0) {
                char tmp[64];
                snprintf(tmp, sizeof(tmp), ",reset=%s", reset);
                strcat(buf, tmp);
            }
            if (back && strlen(back) > 0 && strcmp(back, "-1") != 0) {
                char tmp[64];
                snprintf(tmp, sizeof(tmp), ",back=%s", back);
                strcat(buf, tmp);
            }
            if (strcasecmp(type, "SPI") == 0 && speed && strlen(speed) > 0) {
                char tmp[64];
                snprintf(tmp, sizeof(tmp), ",speed=%s", speed);
                strcat(buf, tmp);
            }
            if (strcasecmp(type, "SPI") == 0 && mode && strlen(mode) > 0) {
                char tmp[64];
                snprintf(tmp, sizeof(tmp), ",mode=%s", mode);
                strcat(buf, tmp);
            }
            if (driver && strlen(driver) > 0 && strcmp(driver, "--") != 0) {
                char tmp[64];
                snprintf(tmp, sizeof(tmp), ",driver=%s", driver);
                strcat(buf, tmp);
                if (depth && strlen(depth) > 0 && strcmp(depth, "-1") != 0) {
                    snprintf(tmp, sizeof(tmp), ":%s", depth);
                    strcat(buf, tmp);
                }
            }
            if (hf && (strcmp(hf, "1") == 0 || strcasecmp(hf, "true") == 0 || strcasecmp(hf, "y") == 0)) {
                strcat(buf, ",HFlip");
            }
            if (vf && (strcmp(vf, "1") == 0 || strcasecmp(vf, "true") == 0 || strcasecmp(vf, "y") == 0)) {
                strcat(buf, ",VFlip");
            }
            if (rotate && (strcmp(rotate, "1") == 0 || strcasecmp(rotate, "true") == 0 || strcasecmp(rotate, "y") == 0)) {
                strcat(buf, ",rotate");
            }
            if (invert && (strcmp(invert, "1") == 0 || strcasecmp(invert, "true") == 0 || strcasecmp(invert, "y") == 0)) {
                strcat(buf, ",invert");
            }
            
            ESP_LOGI(TAG, "Built display_config: %s", buf);
            config_set_value_forced(NVS_TYPE_STR, "display_config", buf);
            processed += 15;
        }
        // Delete all display fields
        mark_as_processed(config, "type");
        mark_as_processed(config, "width");
        mark_as_processed(config, "height");
        mark_as_processed(config, "cs");
        mark_as_processed(config, "reset");
        mark_as_processed(config, "back");
        mark_as_processed(config, "speed");
        mark_as_processed(config, "mode");
        mark_as_processed(config, "driver");
        mark_as_processed(config, "rotate");
        mark_as_processed(config, "hf");
        mark_as_processed(config, "vf");
        mark_as_processed(config, "invert");
        mark_as_processed(config, "address");
        mark_as_processed(config, "depth");
        return processed;
    }
    
    // ===== SPDIF CONFIG (spdif_config) =====
    // Command: cfg-hw-spdif, Fields: clock, wordselect, data -> "bck=X,ws=Y,do=Z"
    if (cmdname_is(cmdname, "spdif")) {
        ESP_LOGI(TAG, "SPDIF handler matched cmdname=%s", cmdname ? cmdname : "null");
        char* clock = get_config_str(config, "clock");
        char* ws = get_config_str(config, "wordselect");
        char* data = get_config_str(config, "data");
        ESP_LOGI(TAG, "SPDIF fields: clock=%s, ws=%s, data=%s", clock ? clock : "null", ws ? ws : "null", data ? data : "null");
        
        if (clock && ws && data) {
            snprintf(buf, sizeof(buf), "bck=%s,ws=%s,do=%s", clock, ws, data);
            ESP_LOGI(TAG, "Built spdif_config: %s", buf);
            config_set_value_forced(NVS_TYPE_STR, "spdif_config", buf);
            processed += 3;
        }
        mark_as_processed(config, "clock");
        mark_as_processed(config, "wordselect");
        mark_as_processed(config, "data");
        return processed;
    }
    
    // ===== DAC CONFIG (dac_config) =====
    // Command: cfg-hw-dac, Fields: clock(bck), wordselect(ws), data(do), model_name(model), mute_gpio(mute), mute_level, dac_sda(sda), dac_scl(scl), dac_i2c(i2c)
    // NOTE: Skip if cmdname contains "spdif" to avoid conflict with SPDIF handler
    if (!cmdname_is(cmdname, "spdif") && 
        (cmdname_is(cmdname, "dac") || cmdname_is(cmdname, "i2s") ||
        (has_config_key(config, "clock") && has_config_key(config, "wordselect") && has_config_key(config, "model_name")))) {
        char* clock = get_config_str(config, "clock");
        char* ws = get_config_str(config, "wordselect");
        char* data = get_config_str(config, "data");
        char* model = get_config_str(config, "model_name");
        char* mute = get_config_str(config, "mute_gpio");
        char* mute_lvl = get_config_str(config, "mute_level");
        char* sda = get_config_str(config, "dac_sda");
        char* scl = get_config_str(config, "dac_scl");
        char* i2c = get_config_str(config, "dac_i2c");
        
        if (clock && ws && data) {
            snprintf(buf, sizeof(buf), "bck=%s,ws=%s,do=%s", clock, ws, data);
            if (mute && strlen(mute) > 0 && strcmp(mute, "-1") != 0) {
                char tmp[64];
                snprintf(tmp, sizeof(tmp), ",mute=%s", mute);
                strcat(buf, tmp);
                if (mute_lvl && (strcmp(mute_lvl, "1") == 0 || strcasecmp(mute_lvl, "true") == 0)) {
                    strcat(buf, ":1");
                } else {
                    strcat(buf, ":0");
                }
            }
            if (model && strlen(model) > 0 && strcmp(model, "--") != 0) {
                char tmp[64];
                snprintf(tmp, sizeof(tmp), ",model=%s", model);
                strcat(buf, tmp);
            }
            if (sda && scl && strlen(sda) > 0 && strlen(scl) > 0 && strcmp(sda, "-1") != 0 && strcmp(scl, "-1") != 0) {
                char tmp[64];
                snprintf(tmp, sizeof(tmp), ",sda=%s,scl=%s", sda, scl);
                strcat(buf, tmp);
                if (i2c && strlen(i2c) > 0) {
                    snprintf(tmp, sizeof(tmp), ",i2c=%s", i2c);
                    strcat(buf, tmp);
                }
            }
            ESP_LOGI(TAG, "Built dac_config: %s", buf);
            config_set_value_forced(NVS_TYPE_STR, "dac_config", buf);
            processed += 9;
        }
        mark_as_processed(config, "clock");
        mark_as_processed(config, "wordselect");
        mark_as_processed(config, "data");
        mark_as_processed(config, "model_name");
        mark_as_processed(config, "mute_gpio");
        mark_as_processed(config, "mute_level");
        mark_as_processed(config, "dac_sda");
        mark_as_processed(config, "dac_scl");
        mark_as_processed(config, "dac_i2c");
        return processed;
    }
    
    // ===== LED VU CONFIG (led_vu_config) =====
    // Command: cfg-hw-ledvu, Fields: gpio, length, type, scale -> "type=WS2812,gpio=X,length=Y,scale=Z"
    if (cmdname_is(cmdname, "ledvu") || cmdname_is(cmdname, "led")) {
        char* led_gpio = get_config_str(config, "gpio");
        char* led_length = get_config_str(config, "length");
        char* led_type = get_config_str(config, "type");
        char* led_scale = get_config_str(config, "scale");
        
        buf[0] = '\0';
        if (led_type && strlen(led_type) > 0 && strcmp(led_type, "--") != 0) {
            snprintf(buf, sizeof(buf), "type=%s", led_type);
        }
        if (led_gpio && strlen(led_gpio) > 0 && strcmp(led_gpio, "-1") != 0) {
            if (buf[0]) strcat(buf, ",");
            char tmp[32];
            snprintf(tmp, sizeof(tmp), "gpio=%s", led_gpio);
            strcat(buf, tmp);
        }
        if (led_length && strlen(led_length) > 0 && strcmp(led_length, "0") != 0) {
            if (buf[0]) strcat(buf, ",");
            char tmp[32];
            snprintf(tmp, sizeof(tmp), "length=%s", led_length);
            strcat(buf, tmp);
        }
        if (led_scale && strlen(led_scale) > 0) {
            if (buf[0]) strcat(buf, ",");
            char tmp[32];
            snprintf(tmp, sizeof(tmp), "scale=%s", led_scale);
            strcat(buf, tmp);
        }
        if (buf[0]) {
            ESP_LOGI(TAG, "Built led_vu_config: %s", buf);
            config_set_value_forced(NVS_TYPE_STR, "led_vu_config", buf);
            processed += 4;
        }
        mark_as_processed(config, "gpio");
        mark_as_processed(config, "length");
        mark_as_processed(config, "type");
        mark_as_processed(config, "scale");
        return processed;
    }
    
    // ===== ROTARY CONFIG (rotary_config) =====
    // Command: cfg-hw-rotary, Fields: A, B, SW, volume_lock, longpress, knobonly, timer
    if (cmdname_is(cmdname, "rotary") || (has_config_key(config, "A") && has_config_key(config, "B"))) {
        char* A = get_config_str(config, "A");
        char* B = get_config_str(config, "B");
        char* SW = get_config_str(config, "SW");
        char* volume_lock = get_config_str(config, "volume_lock");
        char* longpress = get_config_str(config, "longpress");
        char* knobonly = get_config_str(config, "knobonly");
        char* timer = get_config_str(config, "timer");
        char* raw_mode = get_config_str(config, "raw_mode");
        
        buf[0] = '\0';
        if (A && B && strlen(A) > 0 && strlen(B) > 0) {
            snprintf(buf, sizeof(buf), "A=%s,B=%s", A, B);
            if (SW && strlen(SW) > 0 && strcmp(SW, "-1") != 0) {
                char tmp[32];
                snprintf(tmp, sizeof(tmp), ",SW=%s", SW);
                strcat(buf, tmp);
            }
            if (volume_lock && (strcmp(volume_lock, "1") == 0 || strcasecmp(volume_lock, "true") == 0 || strcasecmp(volume_lock, "y") == 0)) {
                strcat(buf, ",volume_lock");
            }
            if (longpress && (strcmp(longpress, "1") == 0 || strcasecmp(longpress, "true") == 0 || strcasecmp(longpress, "y") == 0)) {
                strcat(buf, ",longpress");
            }
            if (knobonly && (strcmp(knobonly, "1") == 0 || strcasecmp(knobonly, "true") == 0 || strcasecmp(knobonly, "y") == 0)) {
                strcat(buf, ",knobonly");
                if (timer && strlen(timer) > 0 && strcmp(timer, "0") != 0) {
                    char tmp[32];
                    snprintf(tmp, sizeof(tmp), "=%s", timer);
                    strcat(buf, tmp);
                }
            }
            ESP_LOGI(TAG, "Built rotary_config: %s", buf);
            config_set_value_forced(NVS_TYPE_STR, "rotary_config", buf);
            processed += 7;
        }
        // raw_mode -> lms_ctrls_raw
        if (raw_mode) {
            const char* nvs_val = (strcmp(raw_mode, "1") == 0 || strcasecmp(raw_mode, "true") == 0 || strcasecmp(raw_mode, "y") == 0) ? "Y" : "N";
            config_set_value_forced(NVS_TYPE_STR, "lms_ctrls_raw", nvs_val);
            processed++;
        }
        mark_as_processed(config, "A");
        mark_as_processed(config, "B");
        mark_as_processed(config, "SW");
        mark_as_processed(config, "volume_lock");
        mark_as_processed(config, "longpress");
        mark_as_processed(config, "knobonly");
        mark_as_processed(config, "timer");
        mark_as_processed(config, "raw_mode");
        return processed;
    }
    
    // ===== CSPOT CONFIG (cspot_config) =====
    // Command: cfg-system-cspot, Fields: deviceName, bitrate, zeroConf -> JSON
    if (cmdname_is(cmdname, "cspot") || has_config_key(config, "deviceName") || has_config_key(config, "bitrate")) {
        char* devName = get_config_str(config, "deviceName");
        char* bitrate = get_config_str(config, "bitrate");
        char* zeroConf = get_config_str(config, "zeroConf");
        
        if (devName || bitrate || zeroConf) {
            cJSON* cspot_json = cJSON_CreateObject();
            if (devName) cJSON_AddStringToObject(cspot_json, "deviceName", devName);
            if (bitrate) cJSON_AddNumberToObject(cspot_json, "bitrate", atoi(bitrate));
            if (zeroConf) cJSON_AddNumberToObject(cspot_json, "zeroConf", atoi(zeroConf));
            
            char* json_str = cJSON_PrintUnformatted(cspot_json);
            if (json_str) {
                ESP_LOGI(TAG, "Built cspot_config: %s", json_str);
                config_set_value_forced(NVS_TYPE_STR, "cspot_config", json_str);
                free(json_str);
                processed += 3;
            }
            cJSON_Delete(cspot_json);
        }
        mark_as_processed(config, "deviceName");
        mark_as_processed(config, "bitrate");
        mark_as_processed(config, "zeroConf");
        return processed;
    }
    
    // ===== BT SOURCE CONFIG =====
    // Command: cfg-audio-bt_source, Fields: sink_name, pin_code
    if (cmdname_is(cmdname, "bt_source") || cmdname_is(cmdname, "bt-source")) {
        char* sink_name = get_config_str(config, "sink_name");
        char* pin_code = get_config_str(config, "pin_code");
        
        if (sink_name) {
            ESP_LOGI(TAG, "Setting a2dp_sink_name=%s", sink_name);
            config_set_value_forced(NVS_TYPE_STR, "a2dp_sink_name", sink_name);
            processed++;
        }
        if (pin_code) {
            ESP_LOGI(TAG, "Setting a2dp_spin=%s", pin_code);
            config_set_value_forced(NVS_TYPE_STR, "a2dp_spin", pin_code);
            processed++;
        }
        mark_as_processed(config, "sink_name");
        mark_as_processed(config, "pin_code");
        return processed;
    }
    
    // ===== AUDIO GENERAL CONFIG =====
    // Command: cfg-audio-general, Fields: jack_behavior, loudness
    if (cmdname_is(cmdname, "cfg-audio-general")) {
        char* jack = get_config_str(config, "jack_behavior");
        char* loudness = get_config_str(config, "loudness");
        
        if (jack && strlen(jack) > 0) {
            const char* nvs_val = (strcasecmp(jack, "Headphones") == 0) ? "y" : "n";
            ESP_LOGI(TAG, "Setting jack_mutes_amp=%s (from jack_behavior=%s)", nvs_val, jack);
            config_set_value_forced(NVS_TYPE_STR, "jack_mutes_amp", nvs_val);
            processed++;
        }
        if (loudness && strlen(loudness) > 0) {
            ESP_LOGI(TAG, "Setting loudness=%s", loudness);
            config_set_value_forced(NVS_TYPE_STR, "loudness", loudness);
            processed++;
        }
        mark_as_processed(config, "jack_behavior");
        mark_as_processed(config, "loudness");
        return processed;
    }
    
    // ===== DEVICE NAME (name -> multiple keys) =====
    if (has_config_key(config, "name")) {
        char* name = get_config_str(config, "name");
        if (name && strlen(name) > 0) {
            ESP_LOGI(TAG, "Setting device name '%s' to multiple keys", name);
            config_set_value_forced(NVS_TYPE_STR, "host_name", name);
            config_set_value_forced(NVS_TYPE_STR, "name", name);
            
            // Build derived names
            char derived[64];
            snprintf(derived, sizeof(derived), "ESP32-AirPlay-%s", name);
            config_set_value_forced(NVS_TYPE_STR, "airplay_name", derived);
            
            snprintf(derived, sizeof(derived), "BT-%s", name);
            config_set_value_forced(NVS_TYPE_STR, "bt_name", derived);
            
            snprintf(derived, sizeof(derived), "squeezelite-%s", name);
            config_set_value_forced(NVS_TYPE_STR, "ap_ssid", derived);
            
            processed++;
        }
        mark_as_processed(config, "name");
    }
    
    // ===== REMAINING SIMPLE KEY TRANSLATIONS (fallback) =====
    // These handle cases when cmdname wasn't sent or for backward compatibility
    
    // sink_name -> a2dp_sink_name
    if (has_config_key(config, "sink_name")) {
        char* val = get_config_str(config, "sink_name");
        if (val) {
            ESP_LOGI(TAG, "Translated sink_name -> a2dp_sink_name=%s", val);
            config_set_value_forced(NVS_TYPE_STR, "a2dp_sink_name", val);
            processed++;
        }
        mark_as_processed(config, "sink_name");
    }
    
    // pin_code -> a2dp_spin
    if (has_config_key(config, "pin_code")) {
        char* val = get_config_str(config, "pin_code");
        if (val) {
            ESP_LOGI(TAG, "Translated pin_code -> a2dp_spin=%s", val);
            config_set_value_forced(NVS_TYPE_STR, "a2dp_spin", val);
            processed++;
        }
        mark_as_processed(config, "pin_code");
    }
    
    // ===== SERVICES CONFIG (cfg-syst-services) =====
    // Fields: cspot, BT_Speaker, AirPlay, telnet (select), stats (checkboxes -> y/n)
    // NOTE: JavaScript sends {key: {value: "true/false", type: 33}} format for checkboxes
    if (cmdname_is(cmdname, "services") || cmdname_is(cmdname, "cfg-syst-services")) {
        ESP_LOGI(TAG, "Processing Services config");
        
        // cspot -> enable_cspot (checkbox) - use get_config_str which extracts .value
        {
            char* str_val = get_config_str(config, "cspot");
            const char* val = "n";  // default if not present
            if (str_val) {
                val = (strcasecmp(str_val, "true") == 0 || 
                       strcasecmp(str_val, "y") == 0 || 
                       strcmp(str_val, "1") == 0) ? "y" : "n";
            }
            ESP_LOGI(TAG, "Setting enable_cspot=%s (str_val=%s)", val, str_val ? str_val : "null");
            config_set_value_forced(NVS_TYPE_STR, "enable_cspot", val);
            mark_as_processed(config, "cspot");
            processed++;
        }
        
        // BT_Speaker -> enable_bt_sink (checkbox)
        {
            char* str_val = get_config_str(config, "BT_Speaker");
            const char* val = "n";
            if (str_val) {
                val = (strcasecmp(str_val, "true") == 0 || 
                       strcasecmp(str_val, "y") == 0 || 
                       strcmp(str_val, "1") == 0) ? "y" : "n";
            }
            ESP_LOGI(TAG, "Setting enable_bt_sink=%s (str_val=%s)", val, str_val ? str_val : "null");
            config_set_value_forced(NVS_TYPE_STR, "enable_bt_sink", val);
            mark_as_processed(config, "BT_Speaker");
            processed++;
        }
        
        // AirPlay -> enable_airplay (checkbox)
        {
            char* str_val = get_config_str(config, "AirPlay");
            const char* val = "n";
            if (str_val) {
                val = (strcasecmp(str_val, "true") == 0 || 
                       strcasecmp(str_val, "y") == 0 || 
                       strcmp(str_val, "1") == 0) ? "y" : "n";
            }
            ESP_LOGI(TAG, "Setting enable_airplay=%s (str_val=%s)", val, str_val ? str_val : "null");
            config_set_value_forced(NVS_TYPE_STR, "enable_airplay", val);
            mark_as_processed(config, "AirPlay");
            processed++;
        }
        
        // stats -> stats (checkbox)
        {
            char* str_val = get_config_str(config, "stats");
            const char* val = "n";
            if (str_val) {
                val = (strcasecmp(str_val, "true") == 0 || 
                       strcasecmp(str_val, "y") == 0 || 
                       strcmp(str_val, "1") == 0) ? "y" : "n";
            }
            ESP_LOGI(TAG, "Setting stats=%s (str_val=%s)", val, str_val ? str_val : "null");
            config_set_value_forced(NVS_TYPE_STR, "stats", val);
            mark_as_processed(config, "stats");
            processed++;
        }
        
        // telnet -> telnet_enable (select: Disabled|Telnet Only|Telnet and Serial -> N|Y|D)
        if (has_config_key(config, "telnet")) {
            char* telnet = get_config_str(config, "telnet");
            const char* val = "N"; // default disabled
            if (telnet && strlen(telnet) > 0) {
                if (strcasecmp(telnet, "Disabled") == 0) {
                    val = "N";
                } else if (strcasecmp(telnet, "Telnet Only") == 0) {
                    val = "Y";
                } else if (strcasecmp(telnet, "Telnet and Serial") == 0) {
                    val = "D";
                }
            }
            ESP_LOGI(TAG, "Setting telnet_enable=%s (from telnet=%s)", val, telnet ? telnet : "null");
            config_set_value_forced(NVS_TYPE_STR, "telnet_enable", val);
            processed++;
            mark_as_processed(config, "telnet");
        }
        
        return processed;
    }
    
    return processed;
}

// Simple key translation for remaining items (called during item processing)
static bool translate_config_key_value(const char* web_key, const char* web_value, 
                                        char** nvs_key, char** nvs_value) {
    // Most translations now handled by process_complex_configs
    // This function handles any remaining simple translations
    return false;
}

#define HTTP_STACK_SIZE	(5*1024)
const char str_na[]="N/A";
#define STR_OR_NA(s) s?s:str_na
/* @brief task handle for the http server */

SemaphoreHandle_t http_server_config_mutex = NULL;
extern RingbufHandle_t messaging;
#define AUTH_TOKEN_SIZE 50
typedef struct session_context {
    char * auth_token;
    bool authenticated;
    char * sess_ip_address;
    u16_t port;
} session_context_t;


union sockaddr_aligned {
	struct sockaddr     sa;
    struct sockaddr_storage st;
    struct sockaddr_in  sin;
    struct sockaddr_in6 sin6;
} aligned_sockaddr_t;
esp_err_t post_handler_buff_receive(httpd_req_t * req);
static const char redirect_payload1[]="<html><head><title>Redirecting to Captive Portal</title><meta http-equiv='refresh' content='0; url=";
static const char redirect_payload2[]="'></head><body><p>Please wait, refreshing.  If page does not refresh, click <a href='";
static const char redirect_payload3[]="'>here</a> to login.</p></body></html>";

/**
 * @brief embedded binary data.
 * @see file "component.mk"
 * @see https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html#embedding-binary-data
 */

esp_err_t redirect_processor(httpd_req_t *req, httpd_err_code_t error);


char * alloc_get_http_header(httpd_req_t * req, const char * key){
    char*  buf = NULL;
    size_t buf_len;

    /* Get header value string length and allocate memory for length + 1,
     * extra byte for null termination */
    buf_len = httpd_req_get_hdr_value_len(req, key) + 1;
    if (buf_len > 1) {
        buf = malloc_init_external(buf_len);
        /* Copy null terminated value string into buffer */
        if (httpd_req_get_hdr_value_str(req, "Host", buf, buf_len) == ESP_OK) {
            ESP_LOGD_LOC(TAG, "Found header => %s: %s",key, buf);
        }
    }
    return buf;
}


char * http_alloc_get_socket_address(httpd_req_t *req, u8_t local, in_port_t * portl) {

	socklen_t len;
	union sockaddr_aligned addr;
	len = sizeof(addr);
	ip_addr_t * ip_addr=NULL;
	char * ipstr = malloc_init_external(INET6_ADDRSTRLEN);
	typedef int (*getaddrname_fn_t)(int s, struct sockaddr *name, socklen_t *namelen);
	getaddrname_fn_t get_addr = NULL;

	int s = httpd_req_to_sockfd(req);
	if(s == -1) {
		free(ipstr);
		return strdup_psram("httpd_req_to_sockfd error");
	}
	ESP_LOGV_LOC(TAG,"httpd socket descriptor: %u", s);

	get_addr = local?&lwip_getsockname:&lwip_getpeername;
	if(get_addr(s, (struct sockaddr *)&addr, &len) <0){
		ESP_LOGE_LOC(TAG,"Failed to retrieve socket address");
		sprintf(ipstr,"N/A (0.0.0.%u)",local);
	}
	else {
		if (addr.sin.sin_family!= AF_INET) {
			ip_addr = (ip_addr_t *)&(addr.sin6.sin6_addr);
			inet_ntop(addr.sa.sa_family, ip_addr, ipstr, INET6_ADDRSTRLEN);
			ESP_LOGV_LOC(TAG,"Processing an IPV6 address : %s", ipstr);
			*portl =  addr.sin6.sin6_port;
			// IDF 5.3: unmap_ipv4_mapped_ipv6(ip_2_ip4(ip_addr), ip_2_ip6(ip_addr));
		}
		else {
			ip_addr = (ip_addr_t *)&(addr.sin.sin_addr);
			inet_ntop(addr.sa.sa_family, ip_addr, ipstr, INET6_ADDRSTRLEN);
			ESP_LOGV_LOC(TAG,"Processing an IPV6 address : %s", ipstr);
			*portl =  addr.sin.sin_port;
		}
		inet_ntop(AF_INET, ip_addr, ipstr, INET6_ADDRSTRLEN);
		ESP_LOGV_LOC(TAG,"Retrieved ip address:port = %s:%u",ipstr, *portl);
	}
	return ipstr;
}
bool is_captive_portal_host_name(httpd_req_t *req){
	const char * host_name=NULL;
	const char * ap_host_name=NULL;
	char * ap_ip_address=NULL;
	bool request_contains_hostname = false;
	esp_err_t hn_err =ESP_OK, err=ESP_OK;
	ESP_LOGD_LOC(TAG,  "Getting adapter host name");
	if((err  = tcpip_adapter_get_hostname(TCPIP_ADAPTER_IF_STA, &host_name )) !=ESP_OK) {
		ESP_LOGE_LOC(TAG,  "Unable to get host name. Error: %s",esp_err_to_name(err));
	}
	else {
		ESP_LOGD_LOC(TAG,  "Host name is %s",host_name);
	}

   ESP_LOGD_LOC(TAG,  "Getting host name from request");
	char *req_host = alloc_get_http_header(req, "Host");

	if(tcpip_adapter_is_netif_up(TCPIP_ADAPTER_IF_AP)){
		ESP_LOGD_LOC(TAG,  "Soft AP is enabled. getting ip info");
		// Access point is up and running. Get the current IP address
		tcpip_adapter_ip_info_t ip_info;
		esp_err_t ap_ip_err = tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_AP, &ip_info);
		if(ap_ip_err != ESP_OK){
			ESP_LOGE_LOC(TAG,  "Unable to get local AP ip address. Error: %s",esp_err_to_name(ap_ip_err));
		}
		else {
			ESP_LOGD_LOC(TAG,  "getting host name for TCPIP_ADAPTER_IF_AP");
			if((hn_err  = tcpip_adapter_get_hostname(TCPIP_ADAPTER_IF_AP, &ap_host_name )) !=ESP_OK) {
				ESP_LOGE_LOC(TAG,  "Unable to get host name. Error: %s",esp_err_to_name(hn_err));
				err=err==ESP_OK?hn_err:err;
			}
			else {
				ESP_LOGD_LOC(TAG,  "Soft AP Host name is %s",ap_host_name);
			}

			ap_ip_address =  malloc_init_external(IP4ADDR_STRLEN_MAX);
			memset(ap_ip_address, 0x00, IP4ADDR_STRLEN_MAX);
			if(ap_ip_address){
				ESP_LOGD_LOC(TAG,  "Converting soft ip address to string");
				esp_ip4addr_ntoa(&ip_info.ip, ap_ip_address, IP4ADDR_STRLEN_MAX);
				ESP_LOGD_LOC(TAG,"TCPIP_ADAPTER_IF_AP is up and has ip address %s ", ap_ip_address);
			}
		}

	}


    if((request_contains_hostname 		= (host_name!=NULL) && (req_host!=NULL) && strcasestr(req_host,host_name)) == true){
    	ESP_LOGD_LOC(TAG,"http request host = system host name %s", req_host);
    }
    else if((request_contains_hostname 		= (ap_host_name!=NULL) && (req_host!=NULL) && strcasestr(req_host,ap_host_name)) == true){
    	ESP_LOGD_LOC(TAG,"http request host = AP system host name %s", req_host);
    }

    FREE_AND_NULL(ap_ip_address);
    FREE_AND_NULL(req_host);

    return request_contains_hostname;
}

/* Custom function to free context */
void free_ctx_func(void *ctx)
{
	session_context_t * context = (session_context_t *)ctx;
    if(context){
    	ESP_LOGD(TAG, "Freeing up socket context");
    	FREE_AND_NULL(context->auth_token);
    	FREE_AND_NULL(context->sess_ip_address);
    	free(context);
    }
}

session_context_t* get_session_context(httpd_req_t *req){
	bool newConnection=false;
	if (! req->sess_ctx) {
		ESP_LOGD(TAG,"New connection context. Allocating session buffer");
		req->sess_ctx = malloc_init_external(sizeof(session_context_t));
		req->free_ctx = free_ctx_func;
		newConnection = true;
		// get the remote IP address only once per session
	}
	session_context_t *ctx_data = (session_context_t*)req->sess_ctx;
	FREE_AND_NULL(ctx_data->sess_ip_address);
	ctx_data->sess_ip_address = http_alloc_get_socket_address(req, 0, &ctx_data->port);
	if(newConnection){
		ESP_LOGI(TAG, "serving %s to peer %s port %u", req->uri, ctx_data->sess_ip_address , ctx_data->port);
	}
	return (session_context_t *)req->sess_ctx;
}

bool is_user_authenticated(httpd_req_t *req){
	session_context_t *ctx_data = get_session_context(req);

	if(ctx_data->authenticated){
		ESP_LOGD_LOC(TAG,"User is authenticated.");
		return true;
	}

	ESP_LOGD(TAG, "Heap internal:%zu (min:%zu) external:%zu (min:%zu) dma:%zu (min:%zu)",
			heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
			heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
			heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
			heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM),
			heap_caps_get_free_size(MALLOC_CAP_DMA),
			heap_caps_get_minimum_free_size(MALLOC_CAP_DMA));

	// todo:  ask for user to authenticate
	return false;
}



/* Copies the full path into destination buffer and returns
 * pointer to requested file name */
static const char* get_path_from_uri(char *dest, const char *uri, size_t destsize)
{
    size_t pathlen = strlen(uri);
    memset(dest,0x0,destsize);

    const char *quest = strchr(uri, '?');
    if (quest) {
        pathlen = MIN(pathlen, quest - uri);
    }
    const char *hash = strchr(uri, '#');
    if (hash) {
        pathlen = MIN(pathlen, hash - uri);
    }

    if ( pathlen + 1 > destsize) {
        /* Full path string won't fit into destination buffer */
        return NULL;
    }

    strlcpy(dest , uri, pathlen + 1);

    // strip trailing blanks
    char * sr = dest+pathlen;
    while(*sr== ' ') *sr-- = '\0';

    char * last_fs = strchr(dest,'/');
    if(!last_fs) ESP_LOGD_LOC(TAG,"no / found in %s", dest);
    char * p=last_fs;
    while(p && *(++p)!='\0'){
    	if(*p == '/') {
    		last_fs=p;
    	}
    }
    /* Return pointer to path, skipping the base */
    return last_fs? ++last_fs: dest;
}

#define IS_FILE_EXT(filename, ext) \
    (strcasecmp(&filename[strlen(filename) - sizeof(ext) + 1], ext) == 0)

/* Set HTTP response content type according to file extension */
static esp_err_t set_content_type_from_file(httpd_req_t *req, const char *filename)
{
    if(strlen(filename) ==0){
    	// for root page, etc.
    	return httpd_resp_set_type(req, HTTPD_TYPE_TEXT);
    } else if (IS_FILE_EXT(filename, ".pdf")) {
        return httpd_resp_set_type(req, "application/pdf");
    } else if (IS_FILE_EXT(filename, ".html")) {
        return httpd_resp_set_type(req, HTTPD_TYPE_TEXT);
    } else if (IS_FILE_EXT(filename, ".jpeg")) {
        return httpd_resp_set_type(req, "image/jpeg");
    } else if (IS_FILE_EXT(filename, ".png")) {
        return httpd_resp_set_type(req, "image/png");
    } else if (IS_FILE_EXT(filename, ".ico")) {
        return httpd_resp_set_type(req, "image/x-icon");
    } else if (IS_FILE_EXT(filename, ".css")) {
        return httpd_resp_set_type(req, "text/css");
    } else if (IS_FILE_EXT(filename, ".js")) {
        return httpd_resp_set_type(req, "text/javascript");
    } else if (IS_FILE_EXT(filename, ".json")) {
        return httpd_resp_set_type(req, HTTPD_TYPE_JSON);
    } else if (IS_FILE_EXT(filename, ".map")) {
        return httpd_resp_set_type(req, "map");
    }


    /* This is a limited set only */
    /* For any other type always set as plain text */
    return httpd_resp_set_type(req, "text/plain");
}
static esp_err_t set_content_type_from_req(httpd_req_t *req)
{
	char filepath[FILE_PATH_MAX];
	const char *filename = get_path_from_uri(filepath, req->uri, sizeof(filepath));
   if (!filename) {
	   ESP_LOGE_LOC(TAG, "Filename is too long");
	   /* Respond with 500 Internal Server Error */
	   httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Filename too long");
	   return ESP_FAIL;
   }

   /* If name has trailing '/', respond with directory contents */
   if (filename[strlen(filename) - 1] == '/' && strlen(filename)>1) {
	   httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Browsing files forbidden.");
	   return ESP_FAIL;
   }
   set_content_type_from_file(req, filename);
   return ESP_OK;
}

int resource_get_index(const char * fileName){
	for(int i=0;resource_lookups[i][0]!='\0';i++){
		if(strstr(resource_lookups[i], fileName)){
			return i;
		}
	}
	return -1;
}
esp_err_t root_get_handler(httpd_req_t *req){
	esp_err_t err = ESP_OK;
    ESP_LOGD_LOC(TAG, "serving [%s]", req->uri);
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Accept-Encoding", "identity");

    if(!is_user_authenticated(req)){
    	// todo:  send password entry page and return
    }
	int idx=-1;
	if((idx=resource_get_index("index.html"))>=0){
		const size_t file_size = (resource_map_end[idx] - resource_map_start[idx]);
		httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
		err = set_content_type_from_req(req);
		if(err == ESP_OK){
			httpd_resp_send(req, (const char *)resource_map_start[idx], file_size);
		} 
	}
    else{
		httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "index.html not found");
	   return ESP_FAIL;
	}
	ESP_LOGD_LOC(TAG, "done serving [%s]", req->uri);
    return err;
}


esp_err_t resource_filehandler(httpd_req_t *req){
    char filepath[FILE_PATH_MAX];
   ESP_LOGD_LOC(TAG, "serving [%s]", req->uri);

   const char *filename = get_path_from_uri(filepath, req->uri, sizeof(filepath));
   if (!filename) {
	   ESP_LOGE_LOC(TAG, "Filename is too long");
	   /* Respond with 500 Internal Server Error */
	   httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Filename too long");
	   return ESP_FAIL;
   }

   /* If name has trailing '/', respond with directory contents */
   if (filename[strlen(filename) - 1] == '/') {
	   httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Browsing files forbidden.");
	   return ESP_FAIL;
   }

	if(strlen(filename) !=0 && IS_FILE_EXT(filename, ".map")){
		return httpd_resp_sendstr(req, "");
	}
	int idx=-1;
	if((idx=resource_get_index(filename))>=0){
	    set_content_type_from_file(req, filename);
		if(strstr(resource_lookups[idx], ".gz")) {
			httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
		}
	    const size_t file_size = (resource_map_end[idx] - resource_map_start[idx]);
	    httpd_resp_send(req, (const char *)resource_map_start[idx], file_size);
	}
	else {
	   ESP_LOGE_LOC(TAG, "Unknown resource [%s] from path [%s] ", filename,filepath);
	   /* Respond with 404 Not Found */
	   httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File does not exist");
	   return ESP_FAIL;
   }
   ESP_LOGD_LOC(TAG, "Resource sending complete");
   return ESP_OK;

}
esp_err_t ap_scan_handler(httpd_req_t *req){
    const char empty[] = "{}";
	ESP_LOGD_LOC(TAG, "serving [%s]", req->uri);
    if(!is_user_authenticated(req)){
    	// todo:  redirect to login page
    	// return ESP_OK;
    }
	network_async_scan();
	esp_err_t err = set_content_type_from_req(req);
	if(err == ESP_OK){
		httpd_resp_send(req, (const char *)empty, HTTPD_RESP_USE_STRLEN);
	}
	return err;
}

esp_err_t console_cmd_get_handler(httpd_req_t *req){
    ESP_LOGD_LOC(TAG, "serving [%s]", req->uri);
    if(!is_user_authenticated(req)){
    	// todo:  redirect to login page
    	// return ESP_OK;
    }
    /* if we can get the mutex, write the last version of the AP list */
	esp_err_t err = set_content_type_from_req(req);
	cJSON * cmdlist = get_cmd_list();
	char * json_buffer = cJSON_Print(cmdlist);
	if(json_buffer){
		httpd_resp_send(req, (const char *)json_buffer, HTTPD_RESP_USE_STRLEN);
		free(json_buffer);
	}
	else{
		ESP_LOGD_LOC(TAG,  "Error retrieving command json string. ");
		httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Unable to format command");
	}
	cJSON_Delete(cmdlist);
	ESP_LOGD_LOC(TAG, "done serving [%s]", req->uri);
	return err;
}
esp_err_t console_cmd_post_handler(httpd_req_t *req){
	char success[]="{\"Result\" : \"Success\" }";
	ESP_LOGD_LOC(TAG, "serving [%s]", req->uri);
	//bool bOTA=false;
	//char * otaURL=NULL;
	esp_err_t err = post_handler_buff_receive(req);
	if(err!=ESP_OK){
		return err;
	}
	if(!is_user_authenticated(req)){
		// todo:  redirect to login page
		// return ESP_OK;
	}
	err = set_content_type_from_req(req);
	if(err != ESP_OK){
		return err;
	}

	char *command= ((rest_server_context_t *)(req->user_ctx))->scratch;

	cJSON *root = cJSON_Parse(command);
	if(root == NULL){
		ESP_LOGE_LOC(TAG, "Parsing command. Received content was: %s",command);
		httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Malformed command json.  Unable to parse content.");
		return ESP_FAIL;
	}
	char * root_str = cJSON_Print(root);
	if(root_str!=NULL){
		ESP_LOGD(TAG, "Processing command item: \n%s", root_str);
		free(root_str);
	}
	cJSON *item=cJSON_GetObjectItemCaseSensitive(root, "command");
	if(!item){
		ESP_LOGE_LOC(TAG, "Command not found. Received content was: %s",command);
		httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Malformed command json.  Unable to parse content.");
		err = ESP_FAIL;

	}
	else{
		// navigate to the first child of the config structure
		char *cmd = cJSON_GetStringValue(item);
		ESP_LOGI(TAG, "console_cmd_post_handler: pushing command [%s]", cmd);
		if(!console_push(cmd, strlen(cmd) + 1)){
			ESP_LOGE(TAG, "console_push failed for command [%s]", cmd);
			httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Unable to push command for execution");
		}
		else {
			ESP_LOGI(TAG, "console_push success for command [%s]", cmd);
			httpd_resp_send(req, (const char *)success, strlen(success));
		}
	}

	ESP_LOGD_LOC(TAG, "done serving [%s]", req->uri);
	return err;
}
esp_err_t ap_get_handler(httpd_req_t *req){
    ESP_LOGD_LOC(TAG, "serving [%s]", req->uri);
    if(!is_user_authenticated(req)){
    	// todo:  redirect to login page
    	// return ESP_OK;
    }
    /* if we can get the mutex, write the last version of the AP list */
	esp_err_t err = set_content_type_from_req(req);
	if( err == ESP_OK && network_status_lock_json_buffer(( TickType_t ) 200/portTICK_PERIOD_MS)){
		char *buff = network_status_alloc_get_ap_list_json();
		network_status_unlock_json_buffer();
		if(buff!=NULL){
			httpd_resp_send(req, (const char *)buff, HTTPD_RESP_USE_STRLEN);
			free(buff);
		}
		else {
			ESP_LOGD_LOC(TAG,  "Error retrieving ap list json string. ");
			httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Unable to retrieve AP list");
		}
	}
	else {
		httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "AP list unavailable");
		ESP_LOGE_LOC(TAG,   "GET /ap.json failed to obtain mutex");
	}
	ESP_LOGD_LOC(TAG, "done serving [%s]", req->uri);
	return err;
}

esp_err_t config_get_handler(httpd_req_t *req){
    ESP_LOGD_LOC(TAG, "serving [%s]", req->uri);
    if(!is_user_authenticated(req)){
    	// todo:  redirect to login page
    	// return ESP_OK;
    }
	esp_err_t err = set_content_type_from_req(req);
	if(err == ESP_OK){
		char * json = config_alloc_get_json(false);
		if(json==NULL){
			ESP_LOGD_LOC(TAG,  "Error retrieving config json string. ");
			httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Error retrieving configuration object");
			err=ESP_FAIL;
		}
		else {
			ESP_LOGD_LOC(TAG,  "config json : %s",json );
			cJSON * gplist=get_gpio_list(false);
			char * gpliststr=cJSON_PrintUnformatted(gplist);
			httpd_resp_sendstr_chunk(req,"{ \"gpio\":");
			httpd_resp_sendstr_chunk(req,gpliststr);
			httpd_resp_sendstr_chunk(req,", \"config\":");
			httpd_resp_sendstr_chunk(req, (const char *)json);
			httpd_resp_sendstr_chunk(req,"}");
			httpd_resp_sendstr_chunk(req,NULL);
			free(gpliststr);
			free(json);
		}
	}
	return err;
}
esp_err_t post_handler_buff_receive(httpd_req_t * req){
    esp_err_t err = ESP_OK;

    int total_len = req->content_len;
    int cur_len = 0;
    char *buf = ((rest_server_context_t *)(req->user_ctx))->scratch;
    int received = 0;
    if (total_len >= SCRATCH_BUFSIZE) {
        /* Respond with 500 Internal Server Error */
    	ESP_LOGE_LOC(TAG,"Received content was too long. ");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR , "Content too long");
        err = ESP_FAIL;
    }
    while (err == ESP_OK && cur_len < total_len) {
        received = httpd_req_recv(req, buf + cur_len, total_len);
        if (received <= 0) {
            /* Respond with 500 Internal Server Error */
        	ESP_LOGE_LOC(TAG,"Not all data was received. ");
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR , "Not all data was received");
            err = ESP_FAIL;
        }
        else {
        	cur_len += received;
        }
    }

    if(err == ESP_OK) {
    	buf[total_len] = '\0';
    }
    return err;
}

esp_err_t config_post_handler(httpd_req_t *req){
    ESP_LOGI(TAG, "=== CONFIG POST HANDLER serving [%s] ===", req->uri);
	bool bOTA=false;
	char * otaURL=NULL;
    esp_err_t err = post_handler_buff_receive(req);
    if(err!=ESP_OK){
        return err;
    }
    if(!is_user_authenticated(req)){
    	// todo:  redirect to login page
    	// return ESP_OK;
    }
	err = set_content_type_from_req(req);
	if(err != ESP_OK){
		return err;
	}

    char *buf = ((rest_server_context_t *)(req->user_ctx))->scratch;
    cJSON *root = cJSON_Parse(buf);
    if(root == NULL){
    	ESP_LOGE_LOC(TAG, "Parsing config json failed. Received content was: %s",buf);
    	httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Malformed config json.  Unable to parse content.");
    	return ESP_FAIL;
    }

    char * root_str = cJSON_Print(root);
	if(root_str!=NULL){
		ESP_LOGD(TAG, "Processing config item: \n%s", root_str);
		free(root_str);
	}

    cJSON *item=cJSON_GetObjectItemCaseSensitive(root, "config");
    if(!item){
    	ESP_LOGE_LOC(TAG, "Parsing config json failed. Received content was: %s",buf);
    	httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Malformed config json.  Unable to parse content.");
    	err = ESP_FAIL;
    }
    else{
    	// First process complex configurations that need multiple fields combined
    	ESP_LOGI(TAG, "Processing complex configurations...");
    	int complex_processed = process_complex_configs(item);
    	ESP_LOGI(TAG, "Processed %d complex config fields", complex_processed);
    	
    	// navigate to the first child of the config structure for remaining items
    	if(item->child) item=item->child;
    }

	while (item && err == ESP_OK)
	{
		cJSON *prev_item = item;
		item=item->next;
		
		// Skip items already processed by process_complex_configs
		if (prev_item->string && should_skip_key(prev_item->string)) {
			ESP_LOGD(TAG, "Skipping already processed key: %s", prev_item->string);
			continue;
		}
		
		char * entry_str = cJSON_Print(prev_item);
		if(entry_str!=NULL){
			ESP_LOGD_LOC(TAG, "Processing config item: \n%s", entry_str);
			free(entry_str);
		}

		if(prev_item->string==NULL) {
			ESP_LOGD_LOC(TAG,"Config value does not have a name");
			httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Malformed config json.  Value does not have a name.");
			err = ESP_FAIL;
		}
		if(err == ESP_OK){
			ESP_LOGD_LOC(TAG,"Found config value name [%s]", prev_item->string);
			nvs_type_t item_type=  config_get_item_type(prev_item);
			if(item_type!=0){
				void * val = config_safe_alloc_get_entry_value(item_type, prev_item);
				if(val!=NULL){
					if(strcmp(prev_item->string, "fwurl")==0) {
						if(item_type!=NVS_TYPE_STR){
							ESP_LOGE_LOC(TAG,"Firmware url should be type %d. Found type %d instead.",NVS_TYPE_STR,item_type );
							httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Malformed config json.  Wrong type for firmware URL.");
							err = ESP_FAIL;
						}
						else {
							// we're getting a request to do an OTA from that URL
							ESP_LOGW_LOC(TAG,   "Found OTA request!");
							otaURL=strdup_psram(val);
							bOTA=true;
						}
					}
					else {
						// Try to translate web UI key/value to NVS key/value
						char* nvs_key = NULL;
						char* nvs_value = NULL;
						const char* actual_key = prev_item->string;
						const char* actual_value = (const char*)val;
						
						if (item_type == NVS_TYPE_STR && 
						    translate_config_key_value(prev_item->string, (const char*)val, &nvs_key, &nvs_value)) {
							actual_key = nvs_key;
							actual_value = nvs_value;
						}
						
						ESP_LOGI(TAG, "Setting config value [%s] = [%s]", actual_key, 
						         item_type == NVS_TYPE_STR ? actual_value : "(non-string)");
						
						if(config_set_value_forced(item_type, actual_key, 
						                           item_type == NVS_TYPE_STR ? (void*)actual_value : val) != ESP_OK){
							ESP_LOGE_LOC(TAG,"Unable to store value for [%s]", actual_key);
							httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR , "Unable to store config value");
							err = ESP_FAIL;
						}
						else {
							ESP_LOGI(TAG,"Successfully set value for [%s]", actual_key);
						}
						
						// Free translated strings if allocated
						if (nvs_key) free(nvs_key);
						if (nvs_value) free(nvs_value);
					}
					free(val);
				}
				else {
					char messageBuffer[101]={};
					ESP_LOGE_LOC(TAG,"Value not found for [%s]", prev_item->string);
					snprintf(messageBuffer,sizeof(messageBuffer),"Malformed config json.  Missing value for entry %s.",prev_item->string);
					httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, messageBuffer);
					err = ESP_FAIL;
				}
			}
			else {
				ESP_LOGE_LOC(TAG,"Unable to determine the type of config value [%s]", prev_item->string);
				httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Malformed config json.  Missing value for entry.");
				err = ESP_FAIL;
			}
		}
	}


	if(err==ESP_OK){
		// Wait for config timer to commit changes to NVS
		ESP_LOGI(TAG, "Waiting for config commit to NVS...");
		if(wait_for_commit()){
			ESP_LOGI(TAG, "Config committed successfully");
		} else {
			ESP_LOGW(TAG, "Timeout waiting for config commit");
		}
		httpd_resp_sendstr(req, "{ \"result\" : \"OK\" }");
		messaging_post_message(MESSAGING_INFO,MESSAGING_CLASS_SYSTEM,"Save Success");
	}
    cJSON_Delete(root);
	if(bOTA) {

		if(is_recovery_running){
			ESP_LOGW_LOC(TAG,   "Starting process OTA for url %s",otaURL);
		}
		else {
			ESP_LOGW_LOC(TAG,   "Restarting system to process OTA for url %s",otaURL);
		}

		network_reboot_ota(otaURL);
		free(otaURL);
	}
    return err;

}
esp_err_t connect_post_handler(httpd_req_t *req){
    ESP_LOGD_LOC(TAG, "serving [%s]", req->uri);
    char success[]="{}";
    char * ssid=NULL;
    char * password=NULL;
    char * host_name=NULL;

	esp_err_t err = post_handler_buff_receive(req);
	if(err!=ESP_OK){
		return err;
	}
	err = set_content_type_from_req(req);
	if(err != ESP_OK){
		return err;
	}

	char *buf = ((rest_server_context_t *)(req->user_ctx))->scratch;
    if(!is_user_authenticated(req)){
    	// todo:  redirect to login page
    	// return ESP_OK;
    }
	cJSON *root = cJSON_Parse(buf);

	if(root==NULL){
		httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR , "JSON parsing error.");
		return ESP_FAIL;
	}

	cJSON * ssid_object = cJSON_GetObjectItem(root, "ssid");
	if(ssid_object !=NULL){
		ssid = strdup_psram(ssid_object->valuestring);
	}
	cJSON * password_object = cJSON_GetObjectItem(root, "pwd");
	if(password_object !=NULL){
		password = strdup_psram(password_object->valuestring);
	}
	cJSON * host_name_object = cJSON_GetObjectItem(root, "host_name");
	if(host_name_object !=NULL){
		host_name = strdup_psram(host_name_object->valuestring);
	}
	cJSON_Delete(root);

	if(host_name!=NULL){
		if(config_set_value(NVS_TYPE_STR, "host_name", host_name) != ESP_OK){
			ESP_LOGW_LOC(TAG,  "Unable to save host name configuration");
		}
	}

	if(ssid !=NULL && strlen(ssid) <= MAX_SSID_SIZE && strlen(password) <= MAX_PASSWORD_SIZE  ){
		network_async_connect(ssid, password);
		httpd_resp_send(req, (const char *)success, strlen(success));
	}
	else {
		httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Malformed json. Missing or invalid ssid/password.");
		err = ESP_FAIL;
	}
	FREE_AND_NULL(ssid);
	FREE_AND_NULL(password);
	FREE_AND_NULL(host_name);
	return err;
}
esp_err_t connect_delete_handler(httpd_req_t *req){
	char success[]="{}";
    ESP_LOGD_LOC(TAG, "serving [%s]", req->uri);
    if(!is_user_authenticated(req)){
    	// todo:  redirect to login page
    	// return ESP_OK;
    }
	esp_err_t err = set_content_type_from_req(req);
	if(err != ESP_OK){
		return err;
	}
	httpd_resp_send(req, (const char *)success, strlen(success));
	network_async_delete();

    return ESP_OK;
}
esp_err_t reboot_ota_post_handler(httpd_req_t *req){
	char success[]="{}";
	ESP_LOGD_LOC(TAG, "serving [%s]", req->uri);
    if(!is_user_authenticated(req)){
    	// todo:  redirect to login page
    	// return ESP_OK;
    }
    esp_err_t err = set_content_type_from_req(req);
	if(err != ESP_OK){
		return err;
	}

	httpd_resp_send(req, (const char *)success, strlen(success));
	network_async_reboot(OTA);
    return ESP_OK;
}
esp_err_t reboot_post_handler(httpd_req_t *req){
    ESP_LOGD_LOC(TAG, "serving [%s]", req->uri);
    char success[]="{}";
    if(!is_user_authenticated(req)){
    	// todo:  redirect to login page
    	// return ESP_OK;
    }
    esp_err_t err = set_content_type_from_req(req);
	if(err != ESP_OK){
		return err;
	}
	httpd_resp_send(req, (const char *)success, strlen(success));
	network_async_reboot(RESTART);
	return ESP_OK;
}
esp_err_t recovery_post_handler(httpd_req_t *req){
    ESP_LOGD_LOC(TAG, "serving [%s]", req->uri);
    char success[]="{}";
    if(!is_user_authenticated(req)){
    	// todo:  redirect to login page
    	// return ESP_OK;
    }
    esp_err_t err = set_content_type_from_req(req);
	if(err != ESP_OK){
		return err;
	}
	httpd_resp_send(req, (const char *)success, strlen(success));
	network_async_reboot(RECOVERY);
	return ESP_OK;
}


esp_err_t flash_post_handler(httpd_req_t *req){
	esp_err_t err =ESP_OK;
	if(is_recovery_running){
		ESP_LOGD_LOC(TAG, "serving [%s]", req->uri);
		char success[]="File uploaded. Flashing started.";
		if(!is_user_authenticated(req)){
			// todo:  redirect to login page
			// return ESP_OK;
		}
		err = httpd_resp_set_type(req, HTTPD_TYPE_TEXT);
		if(err != ESP_OK){
			return err;
		}
		char * binary_buffer = malloc_init_external(req->content_len);
		if(binary_buffer == NULL){
			ESP_LOGE(TAG, "File too large : %d bytes", req->content_len);
			/* Respond with 400 Bad Request */
			httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
								"Binary file too large. Unable to allocate memory!");
			return ESP_FAIL;
		}
		ESP_LOGI(TAG, "Receiving ota binary file");
		/* Retrieve the pointer to scratch buffer for temporary storage */
		char *buf = ((rest_server_context_t *)(req->user_ctx))->scratch;

		char *head=binary_buffer;
		int received;

		/* Content length of the request gives
		 * the size of the file being uploaded */
		int remaining = req->content_len;

		while (remaining > 0) {

			ESP_LOGI(TAG, "Remaining size : %d", remaining);
			/* Receive the file part by part into a buffer */
			if ((received = httpd_req_recv(req, buf, MIN(remaining, SCRATCH_BUFSIZE))) <= 0) {
				if (received == HTTPD_SOCK_ERR_TIMEOUT) {
					/* Retry if timeout occurred */
					continue;
				}
				FREE_RESET(binary_buffer);
				ESP_LOGE(TAG, "File reception failed!");
				/* Respond with 500 Internal Server Error */
				httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive file");
				err = ESP_FAIL;
				goto bail_out;
			}

			/* Write buffer content to file on storage */
			if (received ) {
				memcpy(head,buf,received );
				head+=received;
			}

			/* Keep track of remaining size of
			 * the file left to be uploaded */
			remaining -= received;
		}

		/* Close file upon upload completion */
		ESP_LOGI(TAG, "File reception complete. Invoking OTA process.");
		err = start_ota(NULL, binary_buffer, req->content_len);
		if(err!=ESP_OK){
			httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA processing failed");
			goto bail_out;
		}

		//todo:  handle this in ajax.  For now, just send the root page
		httpd_resp_send(req, (const char *)success, strlen(success));
	}
bail_out:

	return err;
}

char * get_ap_ip_address(){
	static char ap_ip_address[IP4ADDR_STRLEN_MAX]={};

	tcpip_adapter_ip_info_t ip_info;
	esp_err_t err=ESP_OK;
	memset(ap_ip_address, 0x00, sizeof(ap_ip_address));

	ESP_LOGD_LOC(TAG,  "checking if soft AP is enabled");
	if(tcpip_adapter_is_netif_up(TCPIP_ADAPTER_IF_AP)){
		ESP_LOGD_LOC(TAG,  "Soft AP is enabled. getting ip info");
		// Access point is up and running. Get the current IP address
		err = tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_AP, &ip_info);
		if(err != ESP_OK){
			ESP_LOGE_LOC(TAG,  "Unable to get local AP ip address. Error: %s",esp_err_to_name(err));
		}
		else {
			ESP_LOGV_LOC(TAG,  "Converting soft ip address to string");
			esp_ip4addr_ntoa(&ip_info.ip, ap_ip_address, IP4ADDR_STRLEN_MAX);
			ESP_LOGD_LOC(TAG,"TCPIP_ADAPTER_IF_AP is up and has ip address %s ", ap_ip_address);
		}
	}
	else{
		ESP_LOGD_LOC(TAG,"AP Is not enabled. Returning blank string");
	}
	return ap_ip_address;
}
esp_err_t process_redirect(httpd_req_t *req, const char * status){
	const char location_prefix[] = "http://";
	char * ap_ip_address=get_ap_ip_address();
	char * remote_ip=NULL;
	in_port_t port=0;
	char *redirect_url = NULL;

	ESP_LOGD_LOC(TAG,  "Getting remote socket address");
	remote_ip = http_alloc_get_socket_address(req,0, &port);

	size_t buf_size = strlen(redirect_payload1) +strlen(redirect_payload2) + strlen(redirect_payload3) +2*(strlen(location_prefix)+strlen(ap_ip_address))+1;
	char * redirect=malloc_init_external(buf_size);

	if(strcasestr(status,"302")){
		size_t url_buf_size = strlen(location_prefix) + strlen(ap_ip_address)+1;
		redirect_url = malloc_init_external(url_buf_size);
		memset(redirect_url,0x00,url_buf_size);
		snprintf(redirect_url, buf_size,"%s%s/",location_prefix, ap_ip_address);
		ESP_LOGW_LOC(TAG,  "Redirecting host [%s] to %s (from uri %s)",remote_ip, redirect_url,req->uri);
		httpd_resp_set_hdr(req,"Location",redirect_url);
		snprintf(redirect, buf_size,"OK");
	}
	else {

		snprintf(redirect, buf_size,"%s%s%s%s%s%s%s",redirect_payload1, location_prefix, ap_ip_address,redirect_payload2, location_prefix, ap_ip_address,redirect_payload3);
		ESP_LOGW_LOC(TAG,  "Responding to host [%s] (from uri %s) with redirect html page %s",remote_ip, req->uri,redirect);
	}

	httpd_resp_set_type(req, HTTPD_TYPE_TEXT);
	httpd_resp_set_hdr(req,"Cache-Control","no-cache");
	httpd_resp_set_status(req, status);
	httpd_resp_send(req, redirect, HTTPD_RESP_USE_STRLEN);
	FREE_AND_NULL(redirect);
	FREE_AND_NULL(redirect_url);
	FREE_AND_NULL(remote_ip);

	return ESP_OK;
}
esp_err_t redirect_200_ev_handler(httpd_req_t *req){
	ESP_LOGD_LOC(TAG,"Processing known redirect url %s",req->uri);
	process_redirect(req,"200 OK");
	return ESP_OK;
}
esp_err_t redirect_processor(httpd_req_t *req, httpd_err_code_t error){
	esp_err_t err=ESP_OK;
	const char * host_name=NULL;
	const char * ap_host_name=NULL;
	char * user_agent=NULL;
	char * remote_ip=NULL;
	char * sta_ip_address=NULL;
	char * ap_ip_address=get_ap_ip_address();
	char * socket_local_address=NULL;
	bool request_contains_hostname = false;
	bool request_contains_ap_ip_address 	= false;
	bool request_is_sta_ip_address 	= false;
	bool connected_to_ap_ip_interface 	= false;
	bool connected_to_sta_ip_interface = false;
	bool useragentiscaptivenetwork = false;

    in_port_t port=0;
    ESP_LOGV_LOC(TAG,  "Getting remote socket address");
    remote_ip = http_alloc_get_socket_address(req,0, &port);

	ESP_LOGW_LOC(TAG, "%s requested invalid URL: [%s]",remote_ip, req->uri);
    if(network_status_lock_sta_ip_string(portMAX_DELAY)){
		sta_ip_address = strdup_psram(network_status_get_sta_ip_string());
		network_status_unlock_sta_ip_string();
	}
	else {
    	ESP_LOGE(TAG,"Unable to obtain local IP address from WiFi Manager.");
    	httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR , NULL);
	}


    ESP_LOGV_LOC(TAG,  "Getting host name from request");
    char *req_host = alloc_get_http_header(req, "Host");

    user_agent = alloc_get_http_header(req,"User-Agent");
    if((useragentiscaptivenetwork = (user_agent!=NULL  && strcasestr(user_agent,"CaptiveNetworkSupport"))==true)){
    	ESP_LOGW_LOC(TAG,"Found user agent that supports captive networks! [%s]",user_agent);
    }

	esp_err_t hn_err = ESP_OK;
	ESP_LOGV_LOC(TAG,  "Getting adapter host name");
	if((hn_err  = tcpip_adapter_get_hostname(TCPIP_ADAPTER_IF_STA, &host_name )) !=ESP_OK) {
		ESP_LOGE_LOC(TAG,  "Unable to get host name. Error: %s",esp_err_to_name(hn_err));
		err=err==ESP_OK?hn_err:err;
	}
	else {
		ESP_LOGV_LOC(TAG,  "Host name is %s",host_name);
	}


	in_port_t loc_port=0;
	ESP_LOGV_LOC(TAG,  "Getting local socket address");
	socket_local_address= http_alloc_get_socket_address(req,1, &loc_port);



    ESP_LOGD_LOC(TAG,  "Peer IP: %s [port %u], System AP IP address: %s, System host: %s. Requested Host: [%s], uri [%s]",STR_OR_NA(remote_ip), port, STR_OR_NA(ap_ip_address), STR_OR_NA(host_name), STR_OR_NA(req_host), req->uri);
    /* captive portal functionality: redirect to access point IP for HOST that are not the access point IP OR the STA IP */
	/* determine if Host is from the STA IP address */

    if((request_contains_hostname 		= (host_name!=NULL) && (req_host!=NULL) && strcasestr(req_host,host_name)) == true){
    	ESP_LOGD_LOC(TAG,"http request host = system host name %s", req_host);
    }
    else if((request_contains_hostname 		= (ap_host_name!=NULL) && (req_host!=NULL) && strcasestr(req_host,ap_host_name)) == true){
    	ESP_LOGD_LOC(TAG,"http request host = AP system host name %s", req_host);
    }
    if((request_contains_ap_ip_address 	= (ap_ip_address!=NULL) && (req_host!=NULL) && strcasestr(req_host,ap_ip_address))== true){
    	ESP_LOGD_LOC(TAG,"http request host is access point ip address %s", req_host);
    }
    if((connected_to_ap_ip_interface 	= (ap_ip_address!=NULL) && (socket_local_address!=NULL) && strcasestr(socket_local_address,ap_ip_address))==true){
    	ESP_LOGD_LOC(TAG,"http request is connected to access point interface IP %s", ap_ip_address);
    }
    if((request_is_sta_ip_address 	= (sta_ip_address!=NULL) && (req_host!=NULL) && strcasestr(req_host,sta_ip_address))==true){
    	ESP_LOGD_LOC(TAG,"http request host is WiFi client ip address %s", req_host);
    }
    if((connected_to_sta_ip_interface = (sta_ip_address!=NULL) && (socket_local_address!=NULL) && strcasestr(sta_ip_address,socket_local_address))==true){
    	ESP_LOGD_LOC(TAG,"http request is connected to WiFi client ip address %s", sta_ip_address);
    }

   if((error == 0) || (error == HTTPD_404_NOT_FOUND && connected_to_ap_ip_interface && !(request_contains_ap_ip_address || request_contains_hostname ))) {
		process_redirect(req,"302 Found");

	}
	else {
		ESP_LOGD_LOC(TAG,"URL not found, and not processing captive portal so throw regular 404 error");
		httpd_resp_send_err(req, error, NULL);
	}

	FREE_AND_NULL(socket_local_address);

	FREE_AND_NULL(req_host);
	FREE_AND_NULL(user_agent);

    FREE_AND_NULL(sta_ip_address);
	FREE_AND_NULL(remote_ip);
	return err;

}
esp_err_t redirect_ev_handler(httpd_req_t *req){
	return redirect_processor(req,0);
}

esp_err_t messages_get_handler(httpd_req_t *req){
    ESP_LOGD_LOC(TAG, "serving [%s]", req->uri);
    if(!is_user_authenticated(req)){
    	// todo:  redirect to login page
    	// return ESP_OK;
    }
    esp_err_t err = set_content_type_from_req(req);
	if(err != ESP_OK){
		return err;
	}
	cJSON * json_messages=  messaging_retrieve_messages(messaging);
	if(json_messages!=NULL){
		char * json_text= cJSON_Print(json_messages);
		httpd_resp_send(req, (const char *)json_text, strlen(json_text));
		free(json_text);
		cJSON_Delete(json_messages);
	}
	else {
		httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR , "Unable to retrieve messages");
	}
	return ESP_OK;
}

esp_err_t status_get_handler(httpd_req_t *req){
    ESP_LOGD_LOC(TAG, "serving [%s]", req->uri);
    if(!is_user_authenticated(req)){
    	// todo:  redirect to login page
    	// return ESP_OK;
    }
    esp_err_t err = set_content_type_from_req(req);
	if(err != ESP_OK){
		return err;
	}

	if(network_status_lock_json_buffer(( TickType_t ) 200/portTICK_PERIOD_MS)) {
		char *buff = network_status_alloc_get_ip_info_json();
		network_status_unlock_json_buffer();
		if(buff) {
			httpd_resp_send(req, (const char *)buff, strlen(buff));
			free(buff);
		}
		else {
			httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR , "Empty status object");
		}
	}
	else {
		httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR , "Error retrieving status object");
	}
	// update status for next status call
	network_async_update_status();

	return ESP_OK;
}


esp_err_t err_handler(httpd_req_t *req, httpd_err_code_t error){
	esp_err_t err = ESP_OK;

    if(error != HTTPD_404_NOT_FOUND){
    	err = httpd_resp_send_err(req, error, NULL);
    }
    else {
    	err = redirect_processor(req,error);
    }

	return err;
}
