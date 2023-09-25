#include <zephyr/init.h>
#include <hal/nrf_gpio.h>

#define LIS2DH_PWR NRF_GPIO_PIN_MAP(0,8)

static int early_init(void)
{
    nrf_gpio_cfg_output(LIS2DH_PWR);

#ifdef CONFIG_LIS2DH
    nrf_gpio_pin_set(LIS2DH_PWR);
#else
    nrf_gpio_pin_clear(LIS2DH_PWR);
#endif

    return 0;
}

SYS_INIT(early_init, PRE_KERNEL_1, 0);