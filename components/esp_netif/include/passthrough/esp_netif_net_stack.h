/*
 * SPDX-FileCopyrightText: 2022-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "esp_netif.h"

#ifdef __cplusplus
extern "C" {
#endif

struct netif {
  /** This field can be set by the device driver and could point
   *  to state information for the device. */
  void *state;
  /** maximum transfer unit (in bytes) */
  uint16_t mtu;
};

#ifdef CONFIG_ESP_NETIF_RECEIVE_REPORT_ERRORS
typedef esp_err_t esp_netif_recv_ret_t;
#define ESP_NETIF_OPTIONAL_RETURN_CODE(expr) expr
#else
typedef void esp_netif_recv_ret_t;
#define ESP_NETIF_OPTIONAL_RETURN_CODE(expr)
#endif // CONFIG_ESP_NETIF_RECEIVE_REPORT_ERRORS

typedef esp_err_t (*init_fn_t)(struct netif*);
typedef esp_netif_recv_ret_t (*input_fn_t)(void *netif, void *buffer, size_t len, void *eb);

struct esp_netif_netstack_passthrough_config {
    init_fn_t init_fn;
    input_fn_t input_fn;
};

// passthrough netif specific network stack configuration
struct esp_netif_netstack_config {
    struct esp_netif_netstack_passthrough_config passthrough;
};

#ifdef __cplusplus
}
#endif
