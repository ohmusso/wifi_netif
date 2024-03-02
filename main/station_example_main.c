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

#include "driver/uart.h"
#include "driver/gpio.h"

#include "passthrough/esp_netif_net_stack.h"
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
static uint8_t ucPassthroughState;

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

/* net stack */
esp_err_t passthrough_init_sta(struct netif *netif) {
    ESP_LOGI(TAG, "init_sta");
    return ESP_OK;
}

/* UART Data Format */
/* tag, data length, data */
/* tag: 2byte, data length: 2byte, data: data length byte */
/* tags */
/*  - 0x01: esp ok: no data length and data */
/*  - 0x02: data : follow data length and data */
#define usLengthTag ((uint16_t)2)
#define usLengthData ((uint16_t)2)
#define usTagInitOk ((uint16_t)1)
#define usTagData  ((uint16_t)2)
#define usTagAck  ((uint16_t)0xA5A5)

static void vUartSendAck(void){
    /* uart data format */
    /* tad: 2byte, length: 2byte, data: length */
    const uint16_t tag = usTagAck ;
    const uint16_t len = usLengthTag;

    uart_write_bytes(UART_NUM_1, (const char*)&tag, len);
}

esp_netif_recv_ret_t passthrough_input(void *h, void *buffer, size_t len, void* l2_buff)
{
    const uint16_t tag = (uint16_t)usTagData;
    const uint16_t rcvLen = (uint16_t)len;

    ESP_LOGI(TAG, "passthrough_input");
    if( ucPassthroughState == 0U ){
        /* drop */
        return;
    }

    /* uart data format */
    /* tad: 2byte, length: 2byte, data: length */
    uint16_t usSendLen = usLengthTag + usLengthData + (uint16_t)len;
    uint8_t* ucpData = malloc((size_t)usSendLen);
    uint8_t* ucpIterator = ucpData;
    memcpy(ucpIterator, &tag, usLengthTag);
    ucpIterator = ucpIterator + usLengthTag;

    memcpy(ucpIterator, &rcvLen, usLengthData);
    ucpIterator = ucpIterator + usLengthData;

    memcpy(ucpIterator, buffer, len);

    /* send */
    uart_write_bytes(UART_NUM_1, (const void*)ucpData, (size_t)usSendLen);
 
    free(ucpData);

    vTaskDelay(pdMS_TO_TICKS(100));

 #ifdef CONFIG_ESP_NETIF_RECEIVE_REPORT_ERRORS
    return ESP_NETIF_OPTIONAL_RETURN_CODE(ESP_OK);
#endif
}

static const struct esp_netif_netstack_config s_wifi_netif_config_sta = {
    {
        passthrough_init_sta,
        passthrough_input
    }
};
static esp_netif_t* pxNetif;

const esp_netif_netstack_config_t *_g_esp_netif_netstack_default_wifi_sta = &s_wifi_netif_config_sta;
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

#define TEST_WIFI_TRANSMIT 0
#if TEST_WIFI_TRANSMIT

struct EtherHeader {
    uint8_t pucDestMac[6];
    uint8_t pucSrcMac[6];
    uint16_t usType;
} __attribute__( ( packed ) );
typedef struct EtherHeader EtherHeader_t;

static EtherHeader_t xEhterHeaderTmp = {
    .pucDestMac = {0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU},
    .pucSrcMac = {0x9CU, 0x9CU, 0x1FU, 0xD0U, 0x03U, 0x84U},
    .usType = 0x0608U
};

static uint8_t* pucConstructPdu(uint8_t* pucPdu, const uint8_t* pucData, const size_t xLen){
    size_t i = xLen;

    while(i != 0){
        i--;
        pucPdu[i] = pucData[i];
    }

    return (pucPdu + xLen);
}

static void vCreateEtherFrame(uint8_t* buffer, EtherHeader_t* header, uint8_t* data, size_t dataLen){
    uint8_t* pucIterator = buffer;

    pucIterator = pucConstructPdu(pucIterator, (uint8_t*)header, sizeof(EtherHeader_t));
    (void)pucConstructPdu(pucIterator, (uint8_t*)data, dataLen);
}

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
#endif /* TEST_WIFI_TRANSMIT */

#define UART_BAUD_RATE_9600 9600U
#define UART_BUF_RX_SIZE (1500)
#define UART_BUF_TX_SIZE (1500)
#define UART_BUF_SIZE (UART_BUF_RX_SIZE + UART_BUF_TX_SIZE)
static uart_config_t xUartConfig = {
    .baud_rate = UART_BAUD_RATE_9600,
    .data_bits = UART_DATA_8_BITS,
    .parity    = UART_PARITY_DISABLE,
    .stop_bits = UART_STOP_BITS_1,
    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    .source_clk = UART_SCLK_DEFAULT,
};
static QueueHandle_t xUartQueue;
static void vUartInit(void){
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_1, &xUartConfig));   /* UART1 */
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM_1, 17, 16, 14, 15));      /* Set UART pins(TX: IO17, RX: IO16, RTS: IO14, CTS: IO15) */
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_1, UART_BUF_SIZE, UART_BUF_SIZE, 10, &xUartQueue, 0));
}

static void vWaitSTMInit(void){
    uint16_t uartRcvTag = 0U;
    while(pdTRUE){
        ESP_LOGI(TAG, "wait stm initialization");
        uart_read_bytes(UART_NUM_1, (void*)&uartRcvTag, (uint32_t)usLengthTag, pdMS_TO_TICKS(1000));
        ESP_LOGI(TAG, "init %d", uartRcvTag);
        if( uartRcvTag == usTagAck ){
            break;
        }
    }

    vUartSendAck();
}

// static void vTaskUartTx(void* pvParameters){
//     while(pdTRUE){
//         ESP_LOGI(TAG, "uart tx");
//         vTaskDelay(pdMS_TO_TICKS(1000));
//     }
// }

void app_main(void)
{
    ucPassthroughState = 0U;

    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* create task */
    // (void)xTaskCreate(vTaskUartTx, "UartTx", configMINIMAL_STACK_SIZE,
    //             (void *)NULL, tskIDLE_PRIORITY + 2, (TaskHandle_t *)NULL);

    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    wifi_init_sta();

    vUartInit();

    ESP_LOGI(TAG, "UART_START");

    vWaitSTMInit();
    ucPassthroughState = 1U;

    ESP_LOGI(TAG, "Passthrough start");



    uart_event_t xUartEvent;
    uint8_t* ucpUartRxData = (uint8_t*) malloc(UART_BUF_RX_SIZE);

#if TEST_WIFI_TRANSMIT
        uint8_t* ucpWifiTxData = (uint8_t*) malloc(UART_BUF_RX_SIZE);
#endif /* TEST_WIFI_TRANSMIT */

    while(pdTRUE){
        // const uint8_t testArp[] = {
        //     /* ethernet header */
        //     0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU,   /* DestMac */
        //     0x9CU, 0x9CU, 0x1FU, 0xD0U, 0x03U, 0x84U,   /* SrcMac */
        //     0x08U, 0x06U,                               /* Type */
        //     /* ip header */
        //     0x00U, 0x01U,                               /* HwType */
        //     0x08U, 0x00U,                               /* ProtocolType */
        //     0x06U,                                      /* HwSize */
        //     0x04U,                                      /* ProtocolSize */
        //     0x00U, 0x01U,                               /* Opcode */
        //     0x9CU, 0x9CU, 0x1FU, 0xD0U, 0x03U, 0x84U,   /* SenderMac */
        //     192U, 168U, 137U, 5U,                       /* SenderIp */
        //     0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,   /* TargetMac */
        //     192U, 168U, 137U, 1U                        /* TargetIp */
        // };
        // const uint16_t testTag = 2U;
        // const uint16_t testLen = (uint16_t)sizeof(testArp);
        // uint8_t testAck;

        // vTaskDelay(pdMS_TO_TICKS(1000));
        // ESP_LOGI(TAG, "test uart output");
        // uart_write_bytes(UART_NUM_1, (const void*)&testTag, 2);
        // uart_write_bytes(UART_NUM_1, (const void*)&testLen, 2);
        // uart_write_bytes(UART_NUM_1, (const void*)testArp, testLen);
        // uart_read_bytes(UART_NUM_1, (void*)&testAck, 2, pdMS_TO_TICKS(1000));
        // if( testAck == 0xA5){
        //     ESP_LOGI(TAG, "test uart recive ack");
        // }

#if TEST_WIFI_TRANSMIT
        // uint8_t* ucpWifiTxData = (uint8_t*) malloc(UART_BUF_RX_SIZE);
        vCreateEtherFrame(ucpWifiTxData, &xEhterHeaderTmp, ucpUartRxData, uartDataLen);
        esp_netif_transmit(pxNetif, (void*)ucpWifiTxData, (sizeof(EtherHeader_t) + (size_t)uartDataLen));
        vTaskDelay(pdMS_TO_TICKS(1000));
#endif /* TEST_WIFI_TRANSMIT */

        xQueueReceive(xUartQueue, (void *)&xUartEvent, (TickType_t)portMAX_DELAY);
        switch ( xUartEvent.type ) {
            case UART_DATA:
                /* read length */
                /* uart data format */
                /* tag: 2byte, data length: 2byte, data: data length */
                uint16_t uartTag = 0U;
                uint16_t uartDataLen = 0U;
                uart_read_bytes(UART_NUM_1, (void*)&uartTag, usLengthTag, portMAX_DELAY);
                if( uartTag != usTagData ){
                    uart_flush(UART_NUM_1);
                    continue;
                }
                uart_read_bytes(UART_NUM_1, (void*)&uartDataLen, usLengthData, portMAX_DELAY);     /* read data length */
                uart_read_bytes(UART_NUM_1, ucpUartRxData, uartDataLen, portMAX_DELAY); /* read data */

                ESP_LOGI(TAG, "transmit");
                esp_netif_transmit(pxNetif, (void*)ucpUartRxData, uartDataLen);
                break;
            default:
                /* nop */
                break;
        }
    }
}
