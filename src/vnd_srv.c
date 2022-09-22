#include <zephyr/types.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(vendor);

/* Custom Service Variables */
#define BT_UUID_CUSTOM_SERVICE_VAL         BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef0)
#define BT_UUID_CUSTOM_CHARACTERISTICS_VAL BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef1)

static struct bt_uuid_128 vnd_uuid = BT_UUID_INIT_128(BT_UUID_CUSTOM_SERVICE_VAL);
static const struct bt_uuid_128 vnd_long_uuid = BT_UUID_INIT_128(BT_UUID_CUSTOM_CHARACTERISTICS_VAL);
static uint8_t vnd_long_value[CONFIG_BT_L2CAP_TX_MTU - 3] = {0};

static ssize_t read_long_vnd(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                        void *buf, uint16_t len, uint16_t offset)
{
    const char *value = attr->user_data;

    return bt_gatt_attr_read(conn, attr, buf, len, offset, value, sizeof(vnd_long_value));
}

static ssize_t write_long_vnd(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                        const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
    uint8_t *value = attr->user_data;

    if (offset + len > sizeof(vnd_long_value)) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

    memcpy(value + offset, buf, len);
    return len;
}

static void long_vnd_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    ARG_UNUSED(attr);

    bool notif_enabled = (value == BT_GATT_CCC_NOTIFY);

    LOG_INF("Vendor service Notifications %s", notif_enabled ? "enabled" : "disabled");
}

/* Vendor Primary Service Declaration */
BT_GATT_SERVICE_DEFINE(vnd_svc,
    BT_GATT_PRIMARY_SERVICE(&vnd_uuid),
    BT_GATT_CHARACTERISTIC(&vnd_long_uuid.uuid,
                BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE | BT_GATT_CHRC_NOTIFY,
                BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT,
                read_long_vnd, write_long_vnd, vnd_long_value),
    BT_GATT_CCC(long_vnd_ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

int bt_long_vnd_notify(void)
{
    int ret;

    ret = bt_rand(vnd_long_value, sizeof(vnd_long_value));
    if (ret) {
        LOG_ERR("Generate random data failed (err %d)", ret);
        return ret;
    }

    ret = bt_gatt_notify(NULL, &vnd_svc.attrs[1], vnd_long_value, sizeof(vnd_long_value));

    return (ret == -ENOTCONN) ? 0 : ret;
}
