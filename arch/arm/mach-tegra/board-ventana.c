/*
 * arch/arm/mach-tegra/board-ventana.c
 *
 * Copyright (c) 2010 - 2011, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/ctype.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/serial_8250.h>
#include <linux/i2c.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/i2c-tegra.h>
#include <linux/gpio.h>
#include <linux/gpio_keys.h>
#include <linux/input.h>
#include <linux/tegra_usb.h>
#include <linux/usb/android_composite.h>
#include <linux/mfd/tps6586x.h>
#include <linux/memblock.h>

#ifdef CONFIG_TOUCHSCREEN_PANJIT_I2C
#include <linux/i2c/panjit_ts.h>
#endif

#include <mach/clk.h>
#include <mach/iomap.h>
#include <mach/irqs.h>
#include <mach/pinmux.h>
#include <mach/iomap.h>
#include <mach/io.h>
#include <mach/i2s.h>
#include <mach/spdif.h>
#include <mach/audio.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <mach/usb_phy.h>
#include <mach/tegra_das.h>

#include "board.h"
#include "clock.h"
#include "board-ventana.h"
#include "devices.h"
#include "gpio-names.h"
#include "fuse.h"
#include "wakeups-t2.h"

#ifdef CONFIG_ANDROID_TIMED_GPIO
#include <../../../drivers/staging/android/timed_output.h>
#include <../../../drivers/staging/android/timed_gpio.h>
#endif

extern void SysShutdown(void );

static struct usb_mass_storage_platform_data tegra_usb_fsg_platform = {
	.vendor = "NVIDIA",
	.product = "Tegra 2",
	.nluns = 1,
};

static struct platform_device tegra_usb_fsg_device = {
	.name = "usb_mass_storage",
	.id = -1,
	.dev = {
		.platform_data = &tegra_usb_fsg_platform,
	},
};

static struct plat_serial8250_port debug_uart_platform_data[] = {
	{
		.membase	= IO_ADDRESS(TEGRA_UARTD_BASE),
		.mapbase	= TEGRA_UARTD_BASE,
		.irq		= INT_UARTD,
		.flags		= UPF_BOOT_AUTOCONF | UPF_FIXED_TYPE,
		.type           = PORT_TEGRA,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
		.uartclk	= 216000000,
	}, {
		.flags		= 0,
	}
};

static struct platform_device debug_uart = {
	.name = "serial8250",
	.id = PLAT8250_DEV_PLATFORM,
	.dev = {
		.platform_data = debug_uart_platform_data,
	},
};

static struct tegra_audio_platform_data tegra_spdif_pdata = {
	.dma_on = true,  /* use dma by default */
	.i2s_clk_rate = 5644800,
	.mode = SPDIF_BIT_MODE_MODE16BIT,
	.fifo_fmt = 0,
};

static struct tegra_utmip_config utmi_phy_config[] = {
	[0] = {
			.hssync_start_delay = 0,
			.idle_wait_delay = 17,
			.elastic_limit = 16,
			.term_range_adj = 6,
			.xcvr_setup = 15,
			.xcvr_lsfslew = 2,
			.xcvr_lsrslew = 2,
	},
	[1] = {
			.hssync_start_delay = 0,
			.idle_wait_delay = 17,
			.elastic_limit = 16,
			.term_range_adj = 6,
			.xcvr_setup = 8,
			.xcvr_lsfslew = 2,
			.xcvr_lsrslew = 2,
	},
};

static struct tegra_ulpi_config ulpi_phy_config = {
	.reset_gpio = TEGRA_GPIO_PG2,
	.clk = "clk_dev2",
	.inf_type = TEGRA_USB_LINK_ULPI,
};

#ifdef CONFIG_BCM4329_RFKILL

static struct resource ventana_bcm4329_rfkill_resources[] = {
	{
		.name   = "bcm4329_nshutdown_gpio",
		.start  = TEGRA_GPIO_PU0,
		.end    = TEGRA_GPIO_PU0,
		.flags  = IORESOURCE_IO,
	},
};

static struct platform_device ventana_bcm4329_rfkill_device = {
	.name = "bcm4329_rfkill",
	.id             = -1,
	.num_resources  = ARRAY_SIZE(ventana_bcm4329_rfkill_resources),
	.resource       = ventana_bcm4329_rfkill_resources,
};

static noinline void __init ventana_bt_rfkill(void)
{
	/*Add Clock Resource*/
	clk_add_alias("bcm4329_32k_clk", ventana_bcm4329_rfkill_device.name, \
				"blink", NULL);

	platform_device_register(&ventana_bcm4329_rfkill_device);

	return;
}
#else
static inline void ventana_bt_rfkill(void) { }
#endif

#ifdef CONFIG_BT_BLUESLEEP
static noinline void __init tegra_setup_bluesleep(void)
{
	struct platform_device *pdev = NULL;
	struct resource *res;

	pdev = platform_device_alloc("bluesleep", 0);
	if (!pdev) {
		pr_err("unable to allocate platform device for bluesleep");
		return;
	}

	res = kzalloc(sizeof(struct resource) * 3, GFP_KERNEL);
	if (!res) {
		pr_err("unable to allocate resource for bluesleep\n");
		goto err_free_dev;
	}

	tegra_gpio_enable(TEGRA_GPIO_PU6);
	tegra_gpio_enable(TEGRA_GPIO_PU1);

	res[0].name   = "gpio_host_wake";
	res[0].start  = TEGRA_GPIO_PU6;
	res[0].end    = TEGRA_GPIO_PU6;
	res[0].flags  = IORESOURCE_IO;

	res[1].name   = "gpio_ext_wake";
	res[1].start  = TEGRA_GPIO_PU1;
	res[1].end    = TEGRA_GPIO_PU1;
	res[1].flags  = IORESOURCE_IO;

	res[2].name   = "host_wake";
	res[2].start  = gpio_to_irq(TEGRA_GPIO_PU6);
	res[2].end    = gpio_to_irq(TEGRA_GPIO_PU6);
	res[2].flags  = IORESOURCE_IRQ;

	if (platform_device_add_resources(pdev, res, 3)) {
		pr_err("unable to add resources to bluesleep device\n");
		goto err_free_res;
	}

	if (platform_device_add(pdev)) {
		pr_err("unable to add bluesleep device\n");
		goto err_free_res;
	}
	return;

err_free_res:
	kfree(res);
err_free_dev:
	platform_device_put(pdev);
	return;
}
#else
static inline void tegra_setup_bluesleep(void) { }
#endif

static __initdata struct tegra_clk_init_table ventana_clk_init_table[] = {
	/* name		parent		rate		enabled */
	{ "uartd",	"pll_p",	216000000,	true},
	{ "uartc",	"pll_m",	600000000,	false},
	{ "blink",	"clk_32k",	32768,		false},
	{ "pll_p_out4",	"pll_p",	24000000,	true },
	{ "pwm",	"clk_m",	12000000,	false},
	{ "pll_a",	NULL,		11289600,	true},
	{ "pll_a_out0",	NULL,		11289600,	true},
	{ "i2s1",	"pll_a_out0",	2822400,	true},
	{ "i2s2",	"pll_a_out0",	11289600,	true},
	{ "audio",	"pll_a_out0",	11289600,	true},
	{ "audio_2x",	"audio",	22579200,	true},
	{ "spdif_out",	"pll_a_out0",	5644800,	false},
	{ "kbc",	"clk_32k",	32768,		true},
	{ NULL,		NULL,		0,		0},
};

#define USB_MANUFACTURER_NAME		"ACER"
#define USB_PRODUCT_NAME		"ACER Iconia Tab A500"
#define USB_PRODUCT_ID_MTP_ADB		0x3325
#define USB_PRODUCT_ID_ADB		0x7101
#define USB_PRODUCT_ID_MTP		0x3341
#define USB_PRODUCT_ID_RNDIS		0x3343
#define USB_VENDOR_ID			0x0502

static char *usb_functions_mtp[] = { "mtp"};
static char *usb_functions_mtp_adb[] = { "mtp", "adb"};
#ifdef CONFIG_USB_ANDROID_RNDIS
static char *usb_functions_rndis[] = { "rndis" };
static char *usb_functions_rndis_adb[] = { "rndis", "adb" };
#endif
static char *usb_functions_all[] = {
#ifdef CONFIG_USB_ANDROID_RNDIS
	"rndis",
#endif
#ifdef CONFIG_USB_ANDROID_MTP
	"mtp",
#endif
	"adb",
};

static struct android_usb_product usb_products[] = {
	{
		.product_id     = USB_PRODUCT_ID_MTP,
		.num_functions  = ARRAY_SIZE(usb_functions_mtp),
		.functions      = usb_functions_mtp,
	},
	{
		.product_id     = USB_PRODUCT_ID_MTP_ADB,
		.num_functions  = ARRAY_SIZE(usb_functions_mtp_adb),
		.functions      = usb_functions_mtp_adb,
	},
#ifdef CONFIG_USB_ANDROID_RNDIS
	{
		.product_id     = USB_PRODUCT_ID_RNDIS,
		.num_functions  = ARRAY_SIZE(usb_functions_rndis),
		.functions      = usb_functions_rndis,
	},
	{
		.product_id     = USB_PRODUCT_ID_RNDIS,
		.num_functions  = ARRAY_SIZE(usb_functions_rndis_adb),
		.functions      = usb_functions_rndis_adb,
	},
#endif
};

/* standard android USB platform data */
static struct android_usb_platform_data andusb_plat = {
	.vendor_id              = USB_VENDOR_ID,
#ifdef CONFIG_USB_ANDROID_MTP
	.product_id             = USB_PRODUCT_ID_MTP_ADB,
#else
	.product_id             = USB_PRODUCT_ID_ADB,
#endif
	.manufacturer_name      = USB_MANUFACTURER_NAME,
	.product_name           = USB_PRODUCT_NAME,
	.serial_number          = NULL,
	.num_products = ARRAY_SIZE(usb_products),
	.products = usb_products,
	.num_functions = ARRAY_SIZE(usb_functions_all),
	.functions = usb_functions_all,
};

static struct platform_device androidusb_device = {
	.name   = "android_usb",
	.id     = -1,
	.dev    = {
		.platform_data  = &andusb_plat,
	},
};

#ifdef CONFIG_USB_ANDROID_RNDIS
static struct usb_ether_platform_data rndis_pdata = {
	.ethaddr = {0, 0, 0, 0, 0, 0},
	.vendorID = USB_VENDOR_ID,
	.vendorDescr = USB_MANUFACTURER_NAME,
};

static struct platform_device rndis_device = {
	.name   = "rndis",
	.id     = -1,
	.dev    = {
		.platform_data  = &rndis_pdata,
	},
};
#endif

static struct i2c_board_info __initdata ventana_i2c_bus1_board_info[] = {
	{
		I2C_BOARD_INFO("wm8903", 0x1a),
		.irq = TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_PX3),
	},
};

static struct tegra_ulpi_config ventana_ehci2_ulpi_phy_config = {
	.reset_gpio = TEGRA_GPIO_PV1,
	.clk = "clk_dev2",
};

static struct tegra_ehci_platform_data ventana_ehci2_ulpi_platform_data = {
	.operating_mode = TEGRA_USB_HOST,
	.power_down_on_bus_suspend = 0,
	.phy_config = &ventana_ehci2_ulpi_phy_config,
};

static struct tegra_i2c_platform_data ventana_i2c1_platform_data = {
	.adapter_nr	= 0,
	.bus_count	= 1,
	.bus_clk_rate	= { 400000, 0 },
};

static const struct tegra_pingroup_config i2c2_ddc = {
	.pingroup	= TEGRA_PINGROUP_DDC,
	.func		= TEGRA_MUX_I2C2,
};

static const struct tegra_pingroup_config i2c2_gen2 = {
	.pingroup	= TEGRA_PINGROUP_PTA,
	.func		= TEGRA_MUX_I2C2,
};

static struct tegra_i2c_platform_data ventana_i2c2_platform_data = {
	.adapter_nr	= 1,
	.bus_count	= 2,
	.bus_clk_rate	= { 50000, 100000 },
	.bus_mux	= { &i2c2_ddc, &i2c2_gen2 },
	.bus_mux_len	= { 1, 1 },
};

static struct tegra_i2c_platform_data ventana_i2c3_platform_data = {
	.adapter_nr	= 3,
	.bus_count	= 1,
	.bus_clk_rate	= { 400000, 0 },
};

static struct tegra_i2c_platform_data ventana_dvc_platform_data = {
	.adapter_nr	= 4,
	.bus_count	= 1,
	.bus_clk_rate	= { 400000, 0 },
	.is_dvc		= true,
};

static struct tegra_audio_platform_data tegra_audio_pdata[] = {
	/* For I2S1 */
	[0] = {
		.i2s_master	= true,
		.dma_on		= true,  /* use dma by default */
		.i2s_master_clk = 44100,
		.i2s_clk_rate	= 2822400,
		.dap_clk	= "clk_dev1",
		.audio_sync_clk = "audio_2x",
		.mode		= I2S_BIT_FORMAT_I2S,
		.fifo_fmt	= I2S_FIFO_PACKED,
		.bit_size	= I2S_BIT_SIZE_16,
		.i2s_bus_width = 32,
		.dsp_bus_width = 16,
	},
	/* For I2S2 */
	[1] = {
		.i2s_master	= true,
		.dma_on		= true,  /* use dma by default */
		.i2s_master_clk = 8000,
		.dsp_master_clk = 8000,
		.i2s_clk_rate	= 2000000,
		.dap_clk	= "clk_dev1",
		.audio_sync_clk = "audio_2x",
		.mode		= I2S_BIT_FORMAT_DSP,
		.fifo_fmt	= I2S_FIFO_16_LSB,
		.bit_size	= I2S_BIT_SIZE_16,
		.i2s_bus_width = 32,
		.dsp_bus_width = 16,
	}
};

static struct tegra_das_platform_data tegra_das_pdata = {
	.tegra_dap_port_info_table = {
		[0] = {
			.dac_port = tegra_das_port_none,
			.codec_type = tegra_audio_codec_type_none,
			.device_property = {
				.num_channels = 0,
				.bits_per_sample = 0,
				.rate = 0,
				.dac_dap_data_comm_format = 0,
			},
		},
		/* I2S1 <--> DAC1 <--> DAP1 <--> Hifi Codec */
		[1] = {
			.dac_port = tegra_das_port_i2s1,
			.codec_type = tegra_audio_codec_type_hifi,
			.device_property = {
				.num_channels = 2,
				.bits_per_sample = 16,
				.rate = 44100,
				.dac_dap_data_comm_format = dac_dap_data_format_i2s,
			},
		},
		[2] = {
			.dac_port = tegra_das_port_none,
			.codec_type = tegra_audio_codec_type_none,
			.device_property = {
				.num_channels = 0,
				.bits_per_sample = 0,
				.rate = 0,
				.dac_dap_data_comm_format = 0,
			},
		},
		[3] = {
			.dac_port = tegra_das_port_none,
			.codec_type = tegra_audio_codec_type_none,
			.device_property = {
				.num_channels = 0,
				.bits_per_sample = 0,
				.rate = 0,
				.dac_dap_data_comm_format = 0,
			},
		},
		[4] = {
			.dac_port = tegra_das_port_none,
			.codec_type = tegra_audio_codec_type_none,
			.device_property = {
				.num_channels = 0,
				.bits_per_sample = 0,
				.rate = 0,
				.dac_dap_data_comm_format = 0,
			},
		},
	},

	.tegra_das_con_table = {
		[0] = {
			.con_id = tegra_das_port_con_id_hifi,
			.num_entries = 4,
			.con_line = {
				[0] = {tegra_das_port_i2s1, tegra_das_port_dap1, true},
				[1] = {tegra_das_port_dap1, tegra_das_port_i2s1, false},
				[2] = {tegra_das_port_i2s2, tegra_das_port_dap4, true},
				[3] = {tegra_das_port_dap4, tegra_das_port_i2s2, false},
			},
		},
	}
};

static void ventana_i2c_init(void)
{
	tegra_i2c_device1.dev.platform_data = &ventana_i2c1_platform_data;
	tegra_i2c_device2.dev.platform_data = &ventana_i2c2_platform_data;
	tegra_i2c_device3.dev.platform_data = &ventana_i2c3_platform_data;
	tegra_i2c_device4.dev.platform_data = &ventana_dvc_platform_data;

	i2c_register_board_info(0, ventana_i2c_bus1_board_info, 1);

	platform_device_register(&tegra_i2c_device1);
	platform_device_register(&tegra_i2c_device2);
	platform_device_register(&tegra_i2c_device3);
	platform_device_register(&tegra_i2c_device4);
}


#ifdef CONFIG_KEYBOARD_GPIO
//ddebug #define GPIO_KEY(_id, _gpio, _iswake)		\
//ddebug 	{					\
//ddebug 		.code = _id,			\
//ddebug 		.gpio = TEGRA_GPIO_##_gpio,	\
//ddebug 		.active_low = 1,		\
//ddebug 		.desc = #_id,			\
//ddebug 		.type = EV_KEY,			\
//ddebug 		.wakeup = _iswake,		\
//ddebug 		.debounce_interval = 10,	\
//ddebug 	}
//ddebug - start
#define GPIO_KEY(_id, _gpio,_isactivelow, _iswake)		\
	{					\
		.code = _id,			\
		.gpio = TEGRA_GPIO_##_gpio,	\
		.active_low = _isactivelow,		\
		.desc = #_id,			\
		.type = EV_KEY,			\
		.wakeup = _iswake,		\
		.debounce_interval = 10,	\
	}

static struct gpio_keys_button ventana_keys[] = {
//ddebug 	[0] = GPIO_KEY(KEY_FIND, PQ3, 0),
//ddebug 	[1] = GPIO_KEY(KEY_HOME, PQ1, 0),
//ddebug 	[2] = GPIO_KEY(KEY_BACK, PQ2, 0),
//ddebug 	[3] = GPIO_KEY(KEY_VOLUMEUP, PQ5, 0),
//ddebug 	[4] = GPIO_KEY(KEY_VOLUMEDOWN, PQ4, 0),
//ddebug 	[5] = GPIO_KEY(KEY_POWER, PV2, 1),
//ddebug 	[6] = GPIO_KEY(KEY_MENU, PC7, 0),
//ddebug - start
        [0] = GPIO_KEY(KEY_VOLUMEUP, PQ4, 1,  0),
        [1] = GPIO_KEY(KEY_VOLUMEDOWN, PQ5, 1, 0),
        [2] = GPIO_KEY(KEY_POWER, PC7, 0, 1),
        [3] = GPIO_KEY(KEY_POWER, PI3, 0, 0),
//ddebug - end
};

#define PMC_WAKE_STATUS 0x14

static int ventana_wakeup_key(void)
{
	unsigned long status =
		readl(IO_ADDRESS(TEGRA_PMC_BASE) + PMC_WAKE_STATUS);

//ddebug 	return status & TEGRA_WAKE_GPIO_PV2 ? KEY_POWER : KEY_RESERVED;
	return status & TEGRA_WAKE_GPIO_PC7 ? KEY_POWER : KEY_RESERVED; //ddebug
}

static struct gpio_keys_platform_data ventana_keys_platform_data = {
	.buttons	= ventana_keys,
	.nbuttons	= ARRAY_SIZE(ventana_keys),
	.wakeup_key	= ventana_wakeup_key,
};

static struct platform_device ventana_keys_device = {
	.name	= "gpio-keys",
	.id	= 0,
	.dev	= {
		.platform_data	= &ventana_keys_platform_data,
	},
};

static void ventana_keys_init(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ventana_keys); i++)
		tegra_gpio_enable(ventana_keys[i].gpio);
}
#endif

#ifdef CONFIG_DOCK
static struct gpio_switch_platform_data dock_switch_platform_data = {
        .gpio = TEGRA_GPIO_PR0,
};

static struct platform_device dock_switch = {
        .name   = "acer-dock",
        .id     = -1,
        .dev    = {
                .platform_data  = &dock_switch_platform_data,
        },
};
#endif

#ifdef CONFIG_ROTATELOCK
static struct gpio_switch_platform_data rotationlock_switch_platform_data = {
        .gpio = TEGRA_GPIO_PQ2,
};

static struct platform_device rotationlock_switch = {
        .name   = "rotationlock",
        .id     = -1,
        .dev    = {
                .platform_data  = &rotationlock_switch_platform_data,
        },
};
#endif

#ifdef CONFIG_ANDROID_TIMED_GPIO
static struct timed_gpio picasso_timed_gpios[] = {
        {
                .name = "vibrator",
                .gpio = TEGRA_GPIO_PV5,
                .max_timeout = 10000,
                .active_low = 0,
        },
};

static struct timed_gpio_platform_data picasso_timed_gpio_platform_data = {
        .num_gpios      = ARRAY_SIZE(picasso_timed_gpios),
        .gpios          = picasso_timed_gpios,
};

static struct platform_device picasso_timed_gpio_device = {
        .name   = TIMED_GPIO_NAME,
        .id     = 0,
        .dev    = {
                .platform_data  = &picasso_timed_gpio_platform_data,
        },
};
#endif

static struct platform_device tegra_camera = {
	.name = "tegra_camera",
	.id = -1,
};

#ifdef CONFIG_ANDROID_RAM_CONSOLE
struct resource tegra_ram_console_resources[] = {
	[0] = {
		.start	= 0,
		.end	= 0,
		.flags	= IORESOURCE_MEM,
	}
};

struct platform_device tegra_ram_console_device = {
	.name			= "ram_console",
	.id 			= -1,
	.num_resources	= ARRAY_SIZE(tegra_ram_console_resources),
	.resource		= tegra_ram_console_resources,
};
#endif

static struct platform_device *ventana_devices[] __initdata = {
	&tegra_usb_fsg_device,
	&androidusb_device,
#ifdef CONFIG_DOCK
	&dock_switch,
#else
	&debug_uart,
#endif
	&tegra_uartb_device,
	&tegra_uartc_device,
	&pmu_device,
	&tegra_udc_device,
	&tegra_ehci2_device,
	&tegra_gart_device,
	&tegra_aes_device,
#ifdef CONFIG_KEYBOARD_GPIO
	&ventana_keys_device,
#endif
#ifdef CONFIG_ANDROID_TIMED_GPIO
        &picasso_timed_gpio_device,
#endif
	&tegra_wdt_device,
	&tegra_i2s_device1,
	&tegra_i2s_device2,
#ifdef CONFIG_ROTATELOCK
	&rotationlock_switch,
#endif
	&tegra_spdif_device,
	&tegra_avp_device,
	&tegra_camera,
	&tegra_das_device,
#ifdef CONFIG_ANDROID_RAM_CONSOLE
	&tegra_ram_console_device,
#endif
};


#ifdef CONFIG_TOUCHSCREEN_PANJIT_I2C
static struct panjit_i2c_ts_platform_data panjit_data = {
	.gpio_reset = TEGRA_GPIO_PQ7,
};

static const struct i2c_board_info ventana_i2c_bus1_touch_info[] = {
	{
	 I2C_BOARD_INFO("panjit_touch", 0x3),
	 .irq = TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_PV6),
	 .platform_data = &panjit_data,
	 },
};

static int __init ventana_touch_init_panjit(void)
{
	tegra_gpio_enable(TEGRA_GPIO_PV6);

	tegra_gpio_enable(TEGRA_GPIO_PQ7);
	i2c_register_board_info(0, ventana_i2c_bus1_touch_info, 1);

	return 0;
}
#endif

#ifdef CONFIG_TOUCHSCREEN_ATMEL_MT_T9
/* Atmel MaxTouch touchscreen              Driver data */

static struct i2c_board_info __initdata i2c_info[] = {
	{
	 I2C_BOARD_INFO("maXTouch", 0X4C),
	 .irq = TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_PV6),
	 },
};

static int __init ventana_touch_init_atmel(void)
{
        int ret;

	tegra_gpio_enable(TEGRA_GPIO_PV6);
	tegra_gpio_enable(TEGRA_GPIO_PQ7);

        ret = gpio_request(TEGRA_GPIO_PV6, "atmel_maXTouch1386_irq_gpio");
        if (ret < 0)
                printk("atmel_maXTouch1386: gpio_request TEGRA_GPIO_PQ6 fail\n");

	ret = gpio_request(TEGRA_GPIO_PQ7, "atmel_maXTouch1386");
	if (ret < 0)
  		printk("atmel_maXTouch1386: gpio_request fail\n");

	ret = gpio_direction_output(TEGRA_GPIO_PQ7, 0);
	if (ret < 0)
  		printk("atmel_maXTouch1386: gpio_direction_output fail\n");
	gpio_set_value(TEGRA_GPIO_PQ7, 0);
	msleep(1);
	gpio_set_value(TEGRA_GPIO_PQ7, 1);
	msleep(100);

	i2c_register_board_info(0, i2c_info, 1);

	return 0;
}
#endif

static struct tegra_ehci_platform_data tegra_ehci_pdata[] = {
	[0] = {
			.phy_config = &utmi_phy_config[0],
			.operating_mode = TEGRA_USB_HOST,
			.power_down_on_bus_suspend = 0,
	},
	[1] = {
			.phy_config = &ulpi_phy_config,
			.operating_mode = TEGRA_USB_HOST,
			.power_down_on_bus_suspend = 0,
	},
	[2] = {
			.phy_config = &utmi_phy_config[1],
			.operating_mode = TEGRA_USB_HOST,
			.power_down_on_bus_suspend = 0,
	},
};

static struct platform_device *tegra_usb_otg_host_register(void)
{
	struct platform_device *pdev;
	void *platform_data;
	int val;

	pdev = platform_device_alloc(tegra_ehci1_device.name, tegra_ehci1_device.id);
	if (!pdev)
		return NULL;

	val = platform_device_add_resources(pdev, tegra_ehci1_device.resource,
		tegra_ehci1_device.num_resources);
	if (val)
		goto error;

	pdev->dev.dma_mask =  tegra_ehci1_device.dev.dma_mask;
	pdev->dev.coherent_dma_mask = tegra_ehci1_device.dev.coherent_dma_mask;

	platform_data = kmalloc(sizeof(struct tegra_ehci_platform_data), GFP_KERNEL);
	if (!platform_data)
		goto error;

	memcpy(platform_data, &tegra_ehci_pdata[0],
				sizeof(struct tegra_ehci_platform_data));
	pdev->dev.platform_data = platform_data;

	val = platform_device_add(pdev);
	if (val)
		goto error_add;

	return pdev;

error_add:
	kfree(platform_data);
error:
	pr_err("%s: failed to add the host contoller device\n", __func__);
	platform_device_put(pdev);
	return NULL;
}

static void tegra_usb_otg_host_unregister(struct platform_device *pdev)
{
	kfree(pdev->dev.platform_data);
	platform_device_unregister(pdev);
}

static struct tegra_otg_platform_data tegra_otg_pdata = {
	.host_register = &tegra_usb_otg_host_register,
	.host_unregister = &tegra_usb_otg_host_unregister,
};

static int __init ventana_gps_init(void)
{
	struct clk *clk32 = clk_get_sys(NULL, "blink");
	if (!IS_ERR(clk32)) {
		clk_set_rate(clk32,clk32->parent->rate);
		clk_enable(clk32);
	}

	tegra_gpio_enable(TEGRA_GPIO_PZ3);
	return 0;
}

static void ventana_power_off(void)
{
	int ret;

	ret = tps6586x_power_off();
	if (ret)
		pr_err("ventana: failed to power off\n");

	while(1);
}

static void __init ventana_power_off_init(void)
{
	pm_power_off = SysShutdown;
}

#define SERIAL_NUMBER_LENGTH 20
static char usb_serial_num[SERIAL_NUMBER_LENGTH];
static void ventana_usb_init(void)
{
	char *src = NULL;
	int i;

	tegra_otg_device.dev.platform_data = &tegra_otg_pdata;
	platform_device_register(&tegra_otg_device);

	tegra_ehci3_device.dev.platform_data=&tegra_ehci_pdata[2];
	platform_device_register(&tegra_ehci3_device);

#ifdef CONFIG_USB_ANDROID_RNDIS
	src = usb_serial_num;

	/* create a fake MAC address from our serial number.
	 * first byte is 0x02 to signify locally administered.
	 */
	rndis_pdata.ethaddr[0] = 0x02;
	for (i = 0; *src; i++) {
		/* XOR the USB serial across the remaining bytes */
		rndis_pdata.ethaddr[i % (ETH_ALEN - 1) + 1] ^= *src++;
	}
	platform_device_register(&rndis_device);
#endif
}

#ifdef CONFIG_DOCK
static void dockin_init(void)
{
	platform_device_register(&tegra_uartd_device);

	tegra_gpio_enable(TEGRA_GPIO_PR0);
	tegra_gpio_enable(TEGRA_GPIO_PR1);
}
#endif

static void __init tegra_ventana_init(void)
{
#if defined(CONFIG_TOUCHSCREEN_PANJIT_I2C) || \
	defined(CONFIG_TOUCHSCREEN_ATMEL_MT_T9)
	struct board_info BoardInfo;
#endif

	tegra_common_init();
	tegra_clk_init_from_table(ventana_clk_init_table);
	ventana_pinmux_init();
	ventana_i2c_init();
	snprintf(usb_serial_num, sizeof(usb_serial_num), "%llx", tegra_chip_uid());
	andusb_plat.serial_number = kstrdup(usb_serial_num, GFP_KERNEL);
	tegra_i2s_device1.dev.platform_data = &tegra_audio_pdata[0];
	tegra_i2s_device2.dev.platform_data = &tegra_audio_pdata[1];
	tegra_spdif_device.dev.platform_data = &tegra_spdif_pdata;
	tegra_das_device.dev.platform_data = &tegra_das_pdata;
//ddebug 	tegra_ehci2_device.dev.platform_data
//ddebug 		= &ventana_ehci2_ulpi_platform_data;
	platform_add_devices(ventana_devices, ARRAY_SIZE(ventana_devices));

#ifdef CONFIG_DOCK
	dockin_init();
#endif

	ventana_sdhci_init();
	ventana_charge_init();
	ventana_regulator_init();

#if defined(CONFIG_TOUCHSCREEN_PANJIT_I2C) || \
	defined(CONFIG_TOUCHSCREEN_ATMEL_MT_T9)

	tegra_get_board_info(&BoardInfo);

	/* boards with sku > 0 have atmel touch panels */
//ddebug 	if (BoardInfo.sku) {
		pr_info("Initializing Atmel touch driver\n");
		ventana_touch_init_atmel();
//ddebug 	} else {
//ddebug 		pr_info("Initializing Panjit touch driver\n");
//ddebug 		ventana_touch_init_panjit();
//ddebug 	}
#endif

#ifdef CONFIG_KEYBOARD_GPIO
	ventana_keys_init();
#endif
#ifdef CONFIG_KEYBOARD_TEGRA
	ventana_kbc_init();
#endif

	ventana_wired_jack_init();
	ventana_usb_init();
	ventana_gps_init();
	ventana_panel_init();
	ventana_sensors_init();
	ventana_bt_rfkill();
	ventana_power_off_init();
	ventana_emc_init();
#ifdef CONFIG_BT_BLUESLEEP
	tegra_setup_bluesleep();
#endif
#ifdef CONFIG_ANDROID_TIMED_GPIO
        tegra_gpio_enable(TEGRA_GPIO_PV5);
#endif
//ddebug - start
        // [Peter] enable gpio for headphone detection
        tegra_gpio_enable(TEGRA_GPIO_PW2);
#ifdef CONFIG_ROTATELOCK
        // [Peter] enable gpio for rotation lock
        tegra_gpio_enable(TEGRA_GPIO_PQ2);
#endif
#ifdef CONFIG_DOCK && CONFIG_ACER_DOCK_HS
        tegra_gpio_enable(TEGRA_GPIO_PX6);
#endif
	    // [Peter] enable gpio for simcard detection
        tegra_gpio_enable(TEGRA_GPIO_PI7);
        // [Peter] enable gpio for p-sensor
        tegra_gpio_enable(TEGRA_GPIO_PC1);
//ddebug - end
}

int __init tegra_ventana_protected_aperture_init(void)
{
	tegra_protected_aperture_init(tegra_grhost_aperture);
	return 0;
}
late_initcall(tegra_ventana_protected_aperture_init);

void __init tegra_ventana_reserve(void)
{
	if (memblock_reserve(0x0, 4096) < 0)
		pr_warn("Cannot reserve first 4K of memory for safety\n");

	tegra_reserve(SZ_256M, SZ_8M, SZ_16M);
}

MACHINE_START(VENTANA, "ventana")
	.boot_params    = 0x00000100,
	.phys_io        = IO_APB_PHYS,
	.io_pg_offst    = ((IO_APB_VIRT) >> 18) & 0xfffc,
	.init_irq       = tegra_init_irq,
	.init_machine   = tegra_ventana_init,
	.map_io         = tegra_map_common_io,
	.reserve        = tegra_ventana_reserve,
	.timer          = &tegra_timer,
MACHINE_END
