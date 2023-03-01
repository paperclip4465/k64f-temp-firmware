#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(k64_temp, LOG_LEVEL_DBG);

#include <zephyr/zephyr.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/net/net_conn_mgr.h>
#include <zephyr/drivers/sensor.h>

#include <mgmt/mcumgr/smp_udp.h>
#include "os_mgmt/os_mgmt.h"
#include "img_mgmt/img_mgmt.h"

#define APP_BMEM
#define APP_DMEM

#define APP_MQTT_BUFFER_SIZE 1024
#define APP_CONNECT_TRIES 3
#define APP_CONNECT_TIMEOUT_MS 5000
#define APP_SLEEP_MSECS 500
#define SERIAL_NUMBER "001"
#define SERVER_ADDR "192.168.1.217"
#define SERVER_PORT 1883

/* Buffers for MQTT client. */
static APP_BMEM uint8_t rx_buffer[APP_MQTT_BUFFER_SIZE];
static APP_BMEM uint8_t tx_buffer[APP_MQTT_BUFFER_SIZE];

/* The mqtt client struct */
static APP_BMEM struct mqtt_client client_ctx;

/* MQTT Broker details. */
static APP_BMEM struct sockaddr_storage broker;

static APP_BMEM struct zsock_pollfd fds[1];
static APP_BMEM int nfds;

static APP_BMEM bool connected;

static const struct device *dev = NULL;
static uint32_t msg_id = 0;
static char clientid[32];

static void prepare_fds(struct mqtt_client *client)
{
	if (client->transport.type == MQTT_TRANSPORT_NON_SECURE) {
		fds[0].fd = client->transport.tcp.sock;
	}

	fds[0].events = ZSOCK_POLLIN;
	nfds = 1;
}

static void clear_fds(void)
{
	nfds = 0;
}

static int wait(int timeout)
{
	int ret = 0;

	if (nfds > 0) {
		ret = zsock_poll(fds, nfds, timeout);
		if (ret < 0) {
			LOG_ERR("poll error: %d", errno);
		}
	}

	return ret;
}

void mqtt_evt_handler(struct mqtt_client *const client,
		      const struct mqtt_evt *evt)
{
	int err;

	switch (evt->type) {
	case MQTT_EVT_CONNACK:
		if (evt->result != 0) {
			LOG_ERR("MQTT connect failed %d", evt->result);
			break;
		}

		connected = true;
		LOG_INF("MQTT client connected!");

		break;

	case MQTT_EVT_DISCONNECT:
		LOG_INF("MQTT client disconnected %d", evt->result);

		connected = false;
		clear_fds();

		break;

	case MQTT_EVT_PUBACK:
		if (evt->result != 0) {
			LOG_ERR("MQTT PUBACK error %d", evt->result);
			break;
		}

		LOG_INF("PUBACK packet id: %u", evt->param.puback.message_id);

		break;

	case MQTT_EVT_PUBREC:
		if (evt->result != 0) {
			LOG_ERR("MQTT PUBREC error %d", evt->result);
			break;
		}

		LOG_INF("PUBREC packet id: %u", evt->param.pubrec.message_id);

		const struct mqtt_pubrel_param rel_param = {
			.message_id = evt->param.pubrec.message_id
		};

		err = mqtt_publish_qos2_release(client, &rel_param);
		if (err != 0) {
			LOG_ERR("Failed to send MQTT PUBREL: %d", err);
		}

		break;

	case MQTT_EVT_PUBCOMP:
		if (evt->result != 0) {
			LOG_ERR("MQTT PUBCOMP error %d", evt->result);
			break;
		}

		LOG_INF("PUBCOMP packet id: %u",
			evt->param.pubcomp.message_id);

		break;

	case MQTT_EVT_PINGRESP:
		LOG_INF("PINGRESP packet");
		break;

	default:
		break;
	}
}

static char *get_mqtt_topic(const char *topic)
{
	static APP_BMEM char buf[256];
	snprintf(buf, sizeof(buf), "%s/%s", SERIAL_NUMBER, topic);
	return buf;
}

static char *get_mqtt_payload(enum mqtt_qos qos)
{
	static APP_BMEM char payload[256];
	struct sensor_value val;

	int ret = sensor_sample_fetch(dev);
	if (ret < 0) {
		LOG_ERR("Failed to read temperature.");
		return "-999";
	}
	ret = sensor_channel_get(dev, SENSOR_CHAN_DIE_TEMP, &val);

	snprintf(payload, sizeof(payload), "%10.6f",
		 ret ? 0 : sensor_value_to_double(&val));

	return payload;
}

static int publish(struct mqtt_client *client, enum mqtt_qos qos)
{
	struct mqtt_publish_param param;

	param.message.topic.qos = qos;
	param.message.topic.topic.utf8 = (uint8_t *)get_mqtt_topic("temp");
	param.message.topic.topic.size = strlen(param.message.topic.topic.utf8);
	param.message.payload.data = get_mqtt_payload(qos);
	param.message.payload.len = strlen(param.message.payload.data);
	param.message_id = msg_id++;
	param.dup_flag = 0U;
	param.retain_flag = 0U;

	return mqtt_publish(client, &param);
}

#define RC_STR(rc) ((rc) == 0 ? "OK" : "ERROR")

#define PRINT_RESULT(func, rc) \
	LOG_INF("%s: %d <%s>", (func), rc, RC_STR(rc))

static void broker_init(void)
{
	struct sockaddr_in *broker4 = (struct sockaddr_in *)&broker;

	broker4->sin_family = AF_INET;
	broker4->sin_port = htons(SERVER_PORT);
	zsock_inet_pton(AF_INET, SERVER_ADDR, &broker4->sin_addr);
}

static void client_init(struct mqtt_client *client)
{
	mqtt_client_init(client);

	broker_init();

	snprintf(clientid, sizeof(clientid), "TEMP-%s", SERIAL_NUMBER);

	/* MQTT client configuration */
	client->broker = &broker;
	client->evt_cb = mqtt_evt_handler;
	client->client_id.utf8 = (uint8_t *)clientid;
	client->client_id.size = strlen(clientid);
	client->password = NULL;
	client->user_name = NULL;
	client->protocol_version = MQTT_VERSION_3_1_1;

	/* MQTT buffers configuration */
	client->rx_buf = rx_buffer;
	client->rx_buf_size = sizeof(rx_buffer);
	client->tx_buf = tx_buffer;
	client->tx_buf_size = sizeof(tx_buffer);

	/* MQTT transport configuration */
	client->transport.type = MQTT_TRANSPORT_NON_SECURE;
}

/* In this routine we block until the connected variable is 1 */
static int try_to_connect(struct mqtt_client *client)
{
	int rc, i = 0;

	while (i++ < APP_CONNECT_TRIES && !connected) {
		client_init(client);

		rc = mqtt_connect(client);
		if (rc != 0) {
			PRINT_RESULT("mqtt_connect", rc);
			k_sleep(K_MSEC(1000));
			continue;
		}

		prepare_fds(client);

		if (wait(APP_CONNECT_TIMEOUT_MS)) {
			mqtt_input(client);
		}

		if (!connected) {
			mqtt_abort(client);
		}
	}

	if (connected) {
		return 0;
	}

	return -EINVAL;
}

static int process_mqtt_and_sleep(struct mqtt_client *client, int timeout)
{
	int64_t remaining = timeout;
	int64_t start_time = k_uptime_get();
	int rc;

	while (remaining > 0 && connected) {
		if (wait(remaining)) {
			rc = mqtt_input(client);
			if (rc != 0) {
				PRINT_RESULT("mqtt_input", rc);
				return rc;
			}
		}

		rc = mqtt_live(client);
		if (rc != 0 && rc != -EAGAIN) {
			PRINT_RESULT("mqtt_live", rc);
			return rc;
		} else if (rc == 0) {
			rc = mqtt_input(client);
			if (rc != 0) {
				PRINT_RESULT("mqtt_input", rc);
				return rc;
			}
		}

		remaining = timeout + start_time - k_uptime_get();
	}

	return 0;
}

#define EVENT_MASK (NET_EVENT_L4_CONNECTED | NET_EVENT_L4_DISCONNECTED)

static struct net_mgmt_event_callback mgmt_cb;

static void event_handler(struct net_mgmt_event_callback *cb,
			  uint32_t mgmt_event, struct net_if *iface)
{
	if ((mgmt_event & EVENT_MASK) != mgmt_event) {
		return;
	}

	if (mgmt_event == NET_EVENT_L4_CONNECTED) {
		LOG_INF("Network connected");

		if (smp_udp_open() < 0) {
			LOG_ERR("could not open smp udp");
		}

		return;
	}

	if (mgmt_event == NET_EVENT_L4_DISCONNECTED) {
		LOG_INF("Network disconnected");
		smp_udp_close();
		return;
	}
}

static void start_smp_udp(void)
{
	net_mgmt_init_event_callback(&mgmt_cb, event_handler, EVENT_MASK);
	net_mgmt_add_event_callback(&mgmt_cb);
	net_conn_mgr_resend_status();
}

static void start_smp()
{
	img_mgmt_register_group();
	os_mgmt_register_group();
	start_smp_udp();
}

#define SUCCESS_OR_EXIT(rc) { if (rc != 0) { return; } }
#define SUCCESS_OR_BREAK(rc) { if (rc != 0) { break; } }

void main(void)
{
	int rc;
	dev = device_get_binding("TEMP1");

	if(!dev){
		printk("Failed to find temperature device!\n");
		return;
	}

	start_smp();

	LOG_INF("attempting to connect: ");
	rc = try_to_connect(&client_ctx);
	PRINT_RESULT("try_to_connect", rc);
	SUCCESS_OR_EXIT(rc);

	while (1) {
		rc = mqtt_ping(&client_ctx);
		SUCCESS_OR_BREAK(rc);

		rc = process_mqtt_and_sleep(&client_ctx, 100);
		SUCCESS_OR_BREAK(rc);

		rc = publish(&client_ctx, MQTT_QOS_0_AT_MOST_ONCE);
		SUCCESS_OR_BREAK(rc);

		rc = process_mqtt_and_sleep(&client_ctx, 100);
		SUCCESS_OR_BREAK(rc);

		k_sleep(K_SECONDS(60));
	}

	return;
}
