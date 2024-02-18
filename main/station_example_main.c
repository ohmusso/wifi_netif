/* WiFi station Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"
#include "esp_netif_net_stack.h"

/* The examples use WiFi configuration that you can set via project configuration menu

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/
#define EXAMPLE_ESP_WIFI_SSID      CONFIG_ESP_WIFI_SSID
#define EXAMPLE_ESP_WIFI_PASS      CONFIG_ESP_WIFI_PASSWORD
#define EXAMPLE_ESP_MAXIMUM_RETRY  CONFIG_ESP_MAXIMUM_RETRY

#if CONFIG_ESP_WPA3_SAE_PWE_HUNT_AND_PECK
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HUNT_AND_PECK
#define EXAMPLE_H2E_IDENTIFIER ""
#elif CONFIG_ESP_WPA3_SAE_PWE_HASH_TO_ELEMENT
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HASH_TO_ELEMENT
#define EXAMPLE_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
#elif CONFIG_ESP_WPA3_SAE_PWE_BOTH
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_BOTH
#define EXAMPLE_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
#endif
#if CONFIG_ESP_WIFI_AUTH_OPEN
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_OPEN
#elif CONFIG_ESP_WIFI_AUTH_WEP
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WEP
#elif CONFIG_ESP_WIFI_AUTH_WPA_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WAPI_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WAPI_PSK
#endif

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static const char *TAG = "wifi station";

static int s_retry_num = 0;


static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        ESP_LOGI(TAG,"connect to the AP");
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static esp_netif_t* pxNetif;
void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    pxNetif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS,
            /* Authmode threshold resets to WPA2 as default if password matches WPA2 standards (pasword len => 8).
             * If you want to connect the device to deprecated WEP/WPA networks, Please set the threshold value
             * to WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK and set the password with length and format matching to
             * WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK standards.
             */
            .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
            .sae_pwe_h2e = ESP_WIFI_SAE_MODE,
            .sae_h2e_identifier = EXAMPLE_H2E_IDENTIFIER,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
}

struct EtherHeader {
    uint8_t pucDestMac[6];
    uint8_t pucSrcMac[6];
    uint16_t usType;
} __attribute__( ( packed ) );
typedef struct EtherHeader EtherHeader_t;

struct ArpMsg {
    EtherHeader_t xEtherHeader;
    uint8_t pucHwType[2];
    uint8_t pucProtocolType[2];
    uint8_t ucHwSize;
    uint8_t ucProtocolSize;
    uint8_t pucOpcode[2];
    uint8_t pucSenderMac[6];
    uint8_t pucSenderIp[4];
    uint8_t pucTargetMac[6];
    uint8_t pucTargetIp[4];
} __attribute__( ( packed ) );
typedef struct ArpMsg ArpMsg_t;

struct IpHeader {
    uint8_t ucVerHeaderLen;
    uint8_t ucTypeOfService;
    uint16_t usIpLen;
    uint16_t usId;
    uint16_t usFlags;
    uint8_t ucTTL;
    uint8_t ucProtcol;
    uint16_t usCheckSum;
    uint8_t pucSrcIpAddr[4];
    uint8_t pucDestIpAddr[4];
} __attribute__( ( packed ) );
typedef struct IpHeader IpHeader_t;

struct IcmpHeader {
    uint16_t usType;
    uint16_t usCheckSum;
    uint16_t usId;
    uint16_t usSequence;
} __attribute__( ( packed ) );
typedef struct IcmpHeader IcmpHeader_t;

struct IcmpEchorMsg {
    IpHeader_t xIpHeader;
    IcmpHeader_t xIcmpHeader;
    uint8_t pucData[2];
} __attribute__( ( packed ) );
typedef struct IcmpEchorMsg IcmpEchorMsg_t;

static EtherHeader_t xEhterHeaderTmp = {
    .pucDestMac = {0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU},
    .pucSrcMac = {0x9CU, 0x9CU, 0x1FU, 0xD0U, 0x03U, 0x84U},
    .usType = 0x0008U
};

static ArpMsg_t xArpMsgTmp = {
    .xEtherHeader = {
        .pucDestMac = {0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU},
        .pucSrcMac = {0x9CU, 0x9CU, 0x1FU, 0xD0U, 0x03U, 0x84U},
        .usType = 0x0608U
    },
    .pucHwType = {0x00U, 0x01U},
    .pucProtocolType = {0x08U, 0x00U},
    .ucHwSize = 0x06U,
    .ucProtocolSize = 0x04U,
    .pucOpcode = {0x00U, 0x01U},
    .pucSenderMac = {0x9CU, 0x9CU, 0x1FU, 0xD0U, 0x03U, 0x84U},
    .pucSenderIp = {192U, 168U, 137U, 5U},
    .pucTargetMac = {0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U},
    .pucTargetIp = {192U, 168U, 137U, 1U},
};

static IpHeader_t xIpHeaderTmp = {
    .ucVerHeaderLen = 0x45U,    /* version: 4, header length: 20byte */
    .ucTypeOfService = 0x00U,   /* type of service: none */
    .usIpLen = 0x0000,          /* ip length(include header and payload) */
    .usId = 0x0000U,            /* id */
    .usFlags = 0x0000U,         /* flags, flagment offset */
    .ucTTL = 0xFFU,             /* time to live */
    .ucProtcol = 0x01U,         /* protocol: icmp */
    .usCheckSum = 0x0000U,      /* ip header checksum */
    .pucSrcIpAddr = {0x00U, 0x00U, 0x00U, 0x00U},
    .pucDestIpAddr = {0xFFU, 0xFFU, 0xFFU, 0xFFU}
};

static IcmpHeader_t xIcmpHeaderTmp = {
    .usType = 0x0008U,          /* type: echo request */
    .usCheckSum = 0x0000U,      /* checksum */
    .usId = 0x0000U,            /* id */
    .usSequence = 0x0000U       /* sequence */
};

#define vSetU16ToPduSig(usPduSig, usData)           \
    do {                                            \
        (usPduSig) = (((usData) << 8) & 0xFF00);    \
        (usPduSig) |= (((usData) >> 8) & 0x00FF);    \
    } while(0);

#define xTxDataSizeMax (1024U)
#define xTxDataSize (14U + 20U + 10U)
typedef struct {
    uint8_t pucTxMsg[xTxDataSizeMax];
    size_t xTxMsgLen;
} TxMsgBuf_t;

TxMsgBuf_t xTxMsgBuf;

static uint16_t usCalcIpCheckSum(const uint8_t* const pucIpData, const size_t xLen){
    uint16_t usSum = 0U;

    for( size_t i = 0; i < xLen; i = i + 2 ){
        uint16_t usData = (((uint16_t)pucIpData[i] << 8) & 0xFF00U);

        if( (i + 1) < xLen ){
            usData |= ((uint16_t)pucIpData[i + 1] & 0x00FFU);
        }
        else{
            usData &= 0xFF00U;
        }

        usSum += usData;
        if( usSum < usData ){   /* check carry up */
            usSum++;
        }
    }

    return usSum = ~usSum;
}

static uint8_t* pucConstructPdu(uint8_t* pucPdu, const uint8_t* pucData, const size_t xLen){
    size_t i = xLen;

    while(i != 0){
        i--;
        pucPdu[i] = pucData[i];
    }

    return (pucPdu + xLen);
}

static void vCreateArpReq(void){
    uint8_t* pucIterator = xTxMsgBuf.pucTxMsg;

    /* set ip header */
    memcpy(pucIterator, &xArpMsgTmp, sizeof(ArpMsg_t)); 

    xTxMsgBuf.xTxMsgLen = sizeof(ArpMsg_t);
}

static void vCreateIcmpEchoReq(void){
    uint8_t* pucIterator = xTxMsgBuf.pucTxMsg;
    IcmpEchorMsg_t xEchoMsg;

    /* set ip header */
    memcpy(&xEchoMsg.xIpHeader, &xIpHeaderTmp, sizeof(IpHeader_t)); 
    vSetU16ToPduSig(xEchoMsg.xIpHeader.usIpLen, sizeof(IcmpEchorMsg_t));

    /* set icmp header */
    memcpy(&xEchoMsg.xIcmpHeader, &xIcmpHeaderTmp, sizeof(IcmpHeader_t)); 

    /* set icmp data */
    xEchoMsg.pucData[0] = 0x12U;
    xEchoMsg.pucData[1] = 0x34U;
    const uint16_t usIcpmCheckSum = usCalcIpCheckSum((uint8_t*)&xEchoMsg.xIcmpHeader, sizeof(IcmpEchorMsg_t) - sizeof(IpHeader_t)); 
    vSetU16ToPduSig(xEchoMsg.xIcmpHeader.usCheckSum, usIcpmCheckSum);

    pucIterator = pucConstructPdu(pucIterator, (uint8_t*)&xEhterHeaderTmp, sizeof(EtherHeader_t));

    pucIterator = pucConstructPdu(pucIterator, (uint8_t*)&xEchoMsg, sizeof(IcmpEchorMsg_t));

    xTxMsgBuf.xTxMsgLen = (pucIterator - xTxMsgBuf.pucTxMsg);
}

void app_main(void)
{
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    wifi_init_sta();

    // vCreateIcmpEchoReq();
    vCreateArpReq();

    while(pdTRUE){
        esp_netif_transmit(pxNetif, xTxMsgBuf.pucTxMsg, xTxMsgBuf.xTxMsgLen);
        vTaskDelay(5000);
    }
}
