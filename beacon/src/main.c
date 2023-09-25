/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/printk.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <string.h>

#include "digital_sign.h"

#define UPDATE_ADV_INTERVAL 30 //Seconds
#define ADV_INTERVAL 5000 //Milliseconds

#define ADV_INTERVAL_HEX ((ADV_INTERVAL * 16UL) / 10UL) //Do not change
#define ADV_INTERVAL_MIN (ADV_INTERVAL_HEX) //Do not change
#define ADV_INTERVAL_MAX (ADV_INTERVAL_HEX + (ADV_INTERVAL_HEX/10)) //Do not change

#define MANUFACTURER_SIZE 2 //Do not change
#define IDENTIFIER_SIZE BT_ADDR_SIZE //Do not change
#define LENGTH_SIZE 1 //Do not change
#define PAYLOAD_SIZE 6 //Changeable

#define HEADER_SIZE (IDENTIFIER_SIZE + LENGTH_SIZE) //Do not change
#define DATA_SIZE (HEADER_SIZE + PAYLOAD_SIZE) //Do not change
#define ADV_SIZE (MANUFACTURER_SIZE + DATA_SIZE + SIGNATURE_SIZE) //Do not change

BUILD_ASSERT(sizeof(CONFIG_WLEN_PRIVATE_KEY) == NUM_ECC_BYTES*2 + 1, "Invalid private key size");

static const struct bt_le_adv_param *param =
	BT_LE_ADV_PARAM(BT_LE_ADV_OPT_EXT_ADV  | BT_LE_ADV_OPT_USE_IDENTITY,
			ADV_INTERVAL_MIN, ADV_INTERVAL_MAX, NULL);

static uint8_t advertising[ADV_SIZE] = {};
static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(0x1408)),
	BT_DATA(BT_DATA_MANUFACTURER_DATA, advertising, ADV_SIZE),
};

static uint8_t identifier[IDENTIFIER_SIZE] = {};
static uint8_t priv_key[KEY_SIZE] = {};

static struct EcdsaData_t ecdsa_data = {};
static volatile uint8_t counter = 0;
static uint32_t sequence = 0;

static volatile bool is_moving = false;

#if CONFIG_WLEN_USE_BUTTON
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static struct gpio_callback button_cb_data;
#endif

#if CONFIG_WLEN_USE_LED
static struct gpio_dt_spec led_0 = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static struct gpio_dt_spec led_1 = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);
static struct gpio_dt_spec led_2 = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios);			
#endif				

static struct bt_le_ext_adv *adv;
static struct bt_le_oob oob;

#if CONFIG_WLEN_USE_ACCEL
static struct sensor_trigger trig;
#endif

#if CONFIG_WLEN_USE_LED
void led_0_handler(struct k_timer *dummy)
{
	gpio_pin_set_dt(&led_0, 0);
}

K_TIMER_DEFINE(led_0_timer, led_0_handler, NULL);

void led_1_handler(struct k_timer *dummy);
K_TIMER_DEFINE(led_1_timer, led_1_handler, NULL);

static volatile bool led_1_state = false;
void led_1_handler(struct k_timer *dummy)
{
	if (led_1_state) {
		gpio_pin_set_dt(&led_1, 0);
		led_1_state = false;
		k_timer_start(&led_1_timer, K_MSEC(ADV_INTERVAL - 100), K_NO_WAIT);
	} else {
		gpio_pin_set_dt(&led_1, 1);
		led_1_state = true;
		k_timer_start(&led_1_timer, K_MSEC(100), K_NO_WAIT);
	}
}

void led_2_handler(struct k_timer *dummy)
{
	gpio_pin_set_dt(&led_2, 0);
}

K_TIMER_DEFINE(led_2_timer, led_2_handler, NULL);
#endif

#if CONFIG_WLEN_USE_BUTTON
static volatile uint64_t last_pressed = 0;
void button_pressed(const struct device *dev, struct gpio_callback *cb,
		    uint32_t pins)
{
	uint64_t now = k_uptime_get();
	if (now - last_pressed < 1000) {
		last_pressed = now;
		return;
	}
	
	last_pressed = now;
	printk("Button pressed at %" PRIu64 "\n", k_uptime_get());
	counter++;

#if CONFIG_WLEN_USE_LED
	gpio_pin_set_dt(&led_0, 1);
	k_timer_start(&led_0_timer, K_MSEC(200), K_NO_WAIT);
#endif
}
#endif

int update_advertising() {
	memcpy(&advertising[MANUFACTURER_SIZE + HEADER_SIZE], (uint8_t *) &sequence, sizeof(sequence));
	sequence++;

	advertising[MANUFACTURER_SIZE + HEADER_SIZE + sizeof(sequence)] = counter;
	advertising[MANUFACTURER_SIZE + HEADER_SIZE + sizeof(sequence) + sizeof(counter)] = is_moving;

	ecdsa_data.message = &advertising[2];
	ecdsa_data.length = DATA_SIZE;
	ecdsa_data.signature = &advertising[MANUFACTURER_SIZE + DATA_SIZE];
	int res = sign_ecdsa(&ecdsa_data);
	if (res)
		return 1;
	
	return 0;
}

void init_advertising() {
	memset(advertising, 0xFF, MANUFACTURER_SIZE);
	memcpy(&advertising[MANUFACTURER_SIZE], identifier, IDENTIFIER_SIZE);
	advertising[MANUFACTURER_SIZE + IDENTIFIER_SIZE] = PAYLOAD_SIZE;

	update_advertising();
}

#if CONFIG_WLEN_USE_ACCEL
static void fetch_and_display(const struct device *sensor)
{
	static int32_t prev_accel[3];
	static bool initialized = false;
	static int8_t acc = 0;
	struct sensor_value accel[3];
	int rc = sensor_sample_fetch(sensor);

	if (rc == 0) {
		rc = sensor_channel_get(sensor,
					SENSOR_CHAN_ACCEL_XYZ,
					accel);
	}
	if (rc < 0) {
		printk("ERROR: Getting accel failed: %d\n", rc);
	} else {
		printk("New acceleration values!\n");
		if (!initialized) {
			for (int i = 0; i < 3; i++) {
				prev_accel[i] = accel[i].val1 * 1000 + (accel[i].val2/1000);
			}
			initialized = true;
			return;
		}

		bool is_moving_now = false;
		for (int i = 0; i < 3; i++) {
			int32_t value = accel[i].val1 * 1000 + (accel[i].val2/1000);
			int32_t diff = value - prev_accel[i];
			prev_accel[i] = value;
			if (diff < -500 || diff > 500) {
				is_moving_now = true;
			}
		}
		if (is_moving_now) {
			acc++;
		} else {
			acc--;
		}
		printk("Acc accel: %d\n", acc);

		if (acc >= 5) {
			is_moving = true;
			printk("Is moving: %d\n", is_moving);
			acc = 0;
		} else if (acc <= -5) {
			is_moving = false;
			printk("Is moving: %d\n", is_moving);
			acc = 0;
		}
	}
}

static void trigger_handler(const struct device *dev,
			    const struct sensor_trigger *trig)
{
	fetch_and_display(dev);
}

static struct sensor_trigger trig;
#endif

void main(void)
{
	int err;

	printk("Starting Beacon\n");

#if CONFIG_WLEN_USE_ACCEL
	const struct device *sensor = DEVICE_DT_GET_ANY(st_lis2dh);

	if (sensor == NULL) {
		printk("No device found\n");
		return;
	}
	if (!device_is_ready(sensor)) {
		printk("Device %s is not ready\n", sensor->name);
		return;
	}

	trig.type = SENSOR_TRIG_DATA_READY;
	trig.chan = SENSOR_CHAN_ACCEL_XYZ;

	if (IS_ENABLED(CONFIG_LIS2DH_ODR_RUNTIME)) {
		struct sensor_value odr = {
			.val1 = 1,
		};

		err = sensor_attr_set(sensor, trig.chan,
						SENSOR_ATTR_SAMPLING_FREQUENCY,
						&odr);
		if (err != 0) {
			printk("Failed to set odr: %d\n", err);
			return;
		}
		printk("Sampling at %u Hz\n", odr.val1);
	}

	err = sensor_trigger_set(sensor, &trig, trigger_handler);
	if (err != 0) {
		printk("Failed to set trigger: %d\n", err);
		return;
	}
#endif

#if CONFIG_WLEN_USE_BUTTON
	if (!device_is_ready(button.port)) {
		printk("Error: button device %s is not ready\n",
		       button.port->name);
		return;
	}

	err = gpio_pin_configure_dt(&button, GPIO_INPUT);
	if (err != 0) {
		printk("Error %d: failed to configure %s pin %d\n",
		       err, button.port->name, button.pin);
		return;
	}

	err = gpio_pin_interrupt_configure_dt(&button,
					      GPIO_INT_EDGE_TO_ACTIVE);
	if (err != 0) {
		printk("Error %d: failed to configure interrupt on %s pin %d\n",
			err, button.port->name, button.pin);
		return;
	}

	gpio_init_callback(&button_cb_data, button_pressed, BIT(button.pin));
	gpio_add_callback(button.port, &button_cb_data);
	printk("Set up button at %s pin %d\n", button.port->name, button.pin);
#endif

#if CONFIG_WLEN_USE_LED
	if (!device_is_ready(led_0.port)) {
		printk("Error: LED device %s is not ready\n",
				led_0.port->name);
		return;
	}
	err = gpio_pin_configure_dt(&led_0, GPIO_OUTPUT_INACTIVE);
	if (err < 0) {
		printk("Error: Couldn't configure LED device %s\n",
				led_0.port->name);
		return;
	}
	printk("Led 0 at %s pin %d\n", led_0.port->name, led_0.pin);

	if (!device_is_ready(led_1.port)) {
		printk("Error: LED device %s is not ready\n",
				led_1.port->name);
		return;
	}
	err = gpio_pin_configure_dt(&led_1, GPIO_OUTPUT_INACTIVE);
	if (err < 0) {
		printk("Error: Couldn't configure LED device %s\n",
				led_1.port->name);
		return;
	}
	printk("Led 1 at %s pin %d\n", led_1.port->name, led_1.pin);
	
	if (!device_is_ready(led_2.port)) {
		printk("Error: LED device %s is not ready\n",
				led_2.port->name);
		return;
	}
	err = gpio_pin_configure_dt(&led_2, GPIO_OUTPUT_INACTIVE);
	if (err < 0) {
		printk("Error: Couldn't configure LED device %s\n",
				led_2.port->name);
		return;
	}
	printk("Led 2 at %s pin %d\n", led_2.port->name, led_2.pin);
#endif

	size_t key_bytes = hex2bin(CONFIG_WLEN_PRIVATE_KEY, strlen(CONFIG_WLEN_PRIVATE_KEY), priv_key, sizeof(priv_key));
	if (key_bytes != NUM_ECC_BYTES) {
		printk("Invalid private key\n");
		return;
	}

	err = init_ecdsa();
	if (err) {
		printk("Error initializing ECDSA\n");
		return;
	}
	set_key_ecdsa(priv_key);

	/* Initialize the Bluetooth Subsystem */
	err = bt_enable(NULL);
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return;
	}

	// Set beacon identifier
	bt_le_oob_get_local(BT_ID_DEFAULT, &oob);
	for (int i = 0; i < BT_ADDR_SIZE; i++) {
		identifier[i] = oob.addr.a.val[(BT_ADDR_SIZE - 1) - i];
	}

	/* Create a non-connectable non-scannable advertising set */
	err = bt_le_ext_adv_create(param, NULL, &adv);
	if (err) {
		printk("Failed to create advertising set (err %d)\n", err);
		return;
	}

	init_advertising();

	err = bt_le_ext_adv_set_data(adv, ad, ARRAY_SIZE(ad), NULL, 0);
	if (err) {
		printk("Failed to set advertising data (err %d)\n", err);
		return;
	}

	/* Enable Advertising */
	printk("Start Extended Advertising...");
	err = bt_le_ext_adv_start(adv, BT_LE_EXT_ADV_START_DEFAULT);
	if (err) {
		printk("Failed to start extended advertising "
				"(err %d)\n", err);
		return;
	}

	printk("done.\n");

#if CONFIG_WLEN_USE_LED
	k_timer_start(&led_1_timer, K_NO_WAIT, K_NO_WAIT);
#endif

	while (true) {
		k_sleep(K_SECONDS(UPDATE_ADV_INTERVAL));

		update_advertising();

#if CONFIG_WLEN_USE_LED
		gpio_pin_set_dt(&led_2, 1);
		k_timer_start(&led_2_timer, K_MSEC(100), K_NO_WAIT);
#endif

		printk("Set Advertising Data...");
		err = bt_le_ext_adv_set_data(adv, ad, ARRAY_SIZE(ad), NULL, 0);
		if (err) {
			printk("Failed (err %d)\n", err);
			return;
		}
		printk("done.\n");
	}
}
