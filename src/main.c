/* main.c - Application main entry point */

/*
 * Copyright (c) 2015-2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/types.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/drivers/gpio.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/services/bas.h>
#include <zephyr/bluetooth/services/hrs.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(demo);

int bt_long_vnd_notify(void);

/* The devicetree node identifier for the "led0" alias. */
#define LED_NODE DT_ALIAS(led0)

/*
 * A build error on this line means your board is unsupported.
 * See the sample documentation for information on how to fix this.
 */
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED_NODE, gpios);

static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA_BYTES(BT_DATA_UUID16_ALL,
              BT_UUID_16_ENCODE(BT_UUID_HRS_VAL),
              BT_UUID_16_ENCODE(BT_UUID_BAS_VAL),
              BT_UUID_16_ENCODE(BT_UUID_CTS_VAL),
              BT_UUID_16_ENCODE(BT_UUID_TPS_VAL)),
};

static uint16_t appearance = sys_cpu_to_le16(CONFIG_BT_DEVICE_APPEARANCE);

/* Set Scan Response data */
static const struct bt_data sd[] = {
    BT_DATA(BT_DATA_GAP_APPEARANCE, &appearance, sizeof(appearance)),
};

static void mtu_exchange_cb(struct bt_conn *conn, uint8_t err,
                struct bt_gatt_exchange_params *params)
{
    LOG_INF("MTU exchange %u %s (%u)", bt_conn_index(conn),
           err == 0U ? "successful" : "failed", bt_gatt_get_mtu(conn));
}

static void connected(struct bt_conn *conn, uint8_t err)
{
    char addr[BT_ADDR_LE_STR_LEN];
    int ret;
    static struct bt_gatt_exchange_params params = {
        .func = mtu_exchange_cb,
    };

    if (err) {
        LOG_ERR("Connection failed (err 0x%02x)", err);
        return;
    }

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    LOG_INF("Connected to %s", addr);

    ret = bt_gatt_exchange_mtu(conn, &params);
    if (ret) {
        LOG_ERR("MTU exchange failed (err %d)", ret);
        return;
    }
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    LOG_INF("Disconnected (reason 0x%02x)", reason);
}

static void remote_info_available(struct bt_conn *conn, struct bt_conn_remote_info *remote_info)
{
    uint8_t features[8];
    char features_str[2 * sizeof(features) +  1] = {0};

    LOG_INF("Remote LMP version 0x%02x subversion 0x%04x manufacturer 0x%04x",
                remote_info->version, remote_info->subversion, remote_info->manufacturer);

    sys_memcpy_swap(features, remote_info->le.features, sizeof(features));
    bin2hex(features, sizeof(features), features_str, sizeof(features_str));

    LOG_INF("LE Features: 0x%s ", features_str);
}

static void le_data_len_updated(struct bt_conn *conn, struct bt_conn_le_data_len_info *info)
{
    char addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    LOG_INF("Data length updated: %s max tx %u (%u us) max rx %u (%u us)\n",
           addr, info->tx_max_len, info->tx_max_time, info->rx_max_len,
           info->rx_max_time);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
    .remote_info_available = remote_info_available,
    .le_data_len_updated = le_data_len_updated,
};

static void auth_passkey_display(struct bt_conn *conn, unsigned int passkey)
{
    char addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    LOG_INF("Passkey for %s: %06u", addr, passkey);
}

static void auth_cancel(struct bt_conn *conn)
{
    char addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    LOG_INF("Pairing cancelled: %s", addr);
}

static struct bt_conn_auth_cb auth_cb_display = {
    .passkey_display = auth_passkey_display,
    .passkey_entry = NULL,
    .cancel = auth_cancel,
};

static void bas_notify(void)
{
    uint8_t battery_level = bt_bas_get_battery_level();

    battery_level--;

    if (!battery_level) {
        battery_level = 100U;
    }

    bt_bas_set_battery_level(battery_level);
}

static void hrs_notify(void)
{
    static uint8_t heartrate = 90U;

    /* Heartrate measurements simulation */
    heartrate++;
    if (heartrate == 160U) {
        heartrate = 90U;
    }

    bt_hrs_notify(heartrate);
}

void main(void)
{
    int err;

    if (!device_is_ready(led.port)) {
        LOG_ERR("LED is not ready");
        return;
    }

    err = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
    if (err < 0) {
        LOG_ERR("Configure LED pin failed (err %d)", err);
        return;
    }

    err = bt_enable(NULL);
    if (err) {
        LOG_ERR("Bluetooth init failed (err %d)", err);
        return;
    }

    LOG_INF("Bluetooth initialized");

    if (IS_ENABLED(CONFIG_SETTINGS)) {
        settings_load();
    }

    bt_conn_auth_cb_register(&auth_cb_display);

    err = bt_le_adv_start(BT_LE_ADV_CONN_NAME, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (err) {
        LOG_ERR("Advertising failed to start (err %d)", err);
        return;
    }

    LOG_INF("Advertising successfully started");

    /* Implement notification. At the moment there is no suitable way
     * of starting delayed work so we do it here
     */
    while (1) {
        k_sleep(K_SECONDS(1));

        /* Heartrate measurements simulation */
        hrs_notify();

        /* Battery level simulation */
        bas_notify();

        /* Vendor service simulation */
        bt_long_vnd_notify();

        /* Toggle LED pin */
        gpio_pin_toggle_dt(&led);
    }
}
