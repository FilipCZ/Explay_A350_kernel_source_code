/* Copyright (c) 2011, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/leds.h>
#include <linux/spinlock.h>
#include <linux/ctype.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/err.h>
#include <asm/mach-types.h>
#include <mach/pmic.h>
#include <mach/gpio.h>
#include <mach/oem_rapi_client.h>

#if defined (CONFIG_LCT_AE550) || defined (CONFIG_LCT_AE770)
#ifndef CONFIG_KBDLED_SUPPORT
#define CONFIG_KBDLED_SUPPORT //LED_SUPPORT_KEYPAD
#endif
#endif

#if defined(CONFIG_KBDLED_SUPPORT) && defined(CONFIG_LCT_AW550)
#define LED_GPIO (12)  // must define it in board-msm7x27a.c too
#endif

#define DEBUG_TRICOLOR_LED 0

#define LED_FLASH 96

enum tri_color_led_color {
	LED_COLOR_RED,
	LED_COLOR_GREEN,
	LED_COLOR_BLUE,
	LED_COLOR_MAX
};

enum tri_led_status{
	ALL_OFF,
	ALL_ON,
	BLUE_ON,
	BLUE_OFF,
	RED_ON,
	RED_OFF,
	GREEN_ON,
	GREEN_OFF,
	BLUE_BLINK,
	RED_BLINK,
	GREEN_BLINK,
	BLUE_BLINK_OFF,
	RED_BLINK_OFF,
	GREEN_BLINK_OFF,
	LED_MAX
};

struct tricolor_led_data {
	struct msm_rpc_client *rpc_client;
	spinlock_t led_lock;
	int led_data[4];
	#ifdef CONFIG_KBDLED_SUPPORT
	struct led_classdev leds[5];	/* blue, green, red, flashlight keypad backlight */
	#else
	struct led_classdev leds[4];	/* blue, green, red, flashlight */
	#endif
};

static void call_oem_rapi_client_streaming_function(struct msm_rpc_client *client,
						    char *input)
{
	struct oem_rapi_client_streaming_func_arg client_arg = {
		OEM_RAPI_CLIENT_EVENT_TRI_COLOR_LED_WORK,
		NULL,
		(void *)NULL,
		sizeof(input),
		input,
		0,
		0,
		0
	};
	struct oem_rapi_client_streaming_func_ret client_ret = {
		(uint32_t *)NULL,
		(char *)NULL
	};

	int ret = oem_rapi_client_streaming_function(client, &client_arg, &client_ret);
	if (ret)
		printk(KERN_ERR
			"oem_rapi_client_streaming_function() error=%d\n", ret);
}


static ssize_t led_blink_solid_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	ssize_t ret = 0;
	enum tri_color_led_color color = LED_COLOR_MAX;
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct tricolor_led_data *tricolor_led = NULL;
	if (!strcmp(led_cdev->name, "red")) {
		color = LED_COLOR_RED;
	} else if (!strcmp(led_cdev->name, "green")) {
		color = LED_COLOR_GREEN;
	} else {
		color = LED_COLOR_BLUE;
	}
	tricolor_led = container_of(led_cdev, struct tricolor_led_data, leds[color]);
	if(!tricolor_led)
		printk(KERN_ERR "%s tricolor_led is NULL ",__func__);
	ret = sprintf(buf, "%u\n", tricolor_led->led_data[color]);
	return ret;
}

static ssize_t led_blink_solid_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t size)
{
	int blink = 0;
	unsigned long flags = 0;
	enum tri_led_status input = LED_MAX;
	enum tri_color_led_color color = LED_COLOR_MAX;
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct tricolor_led_data *tricolor_led = NULL;
	if (!strcmp(led_cdev->name, "red")) {
		color = LED_COLOR_RED;
	} else if (!strcmp(led_cdev->name, "green")) {
		color = LED_COLOR_GREEN;
	} else {
		color = LED_COLOR_BLUE;
	}
	tricolor_led = container_of(led_cdev, struct tricolor_led_data, leds[color]);
	if(!tricolor_led)
		printk(KERN_ERR "%s tricolor_led is NULL ",__func__);
	sscanf(buf, "%d", &blink);
#if DEBUG_TRICOLOR_LED
	printk("tricolor %s is %d\n",led_cdev->name, blink);
#endif
	spin_lock_irqsave(&tricolor_led->led_lock, flags);
	if(blink){
		if(color == LED_COLOR_RED)
			input = RED_BLINK;
		if(color == LED_COLOR_GREEN)
			input = GREEN_BLINK;
		if(color == LED_COLOR_BLUE)
			input = BLUE_BLINK;
	} else {
		if(color == LED_COLOR_RED)
			input = RED_BLINK_OFF;
		if(color == LED_COLOR_GREEN)
			input = GREEN_BLINK_OFF;
		if(color == LED_COLOR_BLUE)
			input = BLUE_BLINK_OFF;
	}
	tricolor_led->led_data[color] = blink;
	spin_unlock_irqrestore(&tricolor_led->led_lock, flags);
	call_oem_rapi_client_streaming_function(tricolor_led->rpc_client, (char*)&input);
	return size;
}

static DEVICE_ATTR(blink, 0644, led_blink_solid_show, led_blink_solid_store);

static void led_brightness_set_tricolor(struct led_classdev *led_cdev,
			       enum led_brightness brightness)
{
	struct tricolor_led_data *tricolor_led = NULL;
	enum tri_color_led_color color = LED_COLOR_MAX;
	enum tri_led_status input = LED_MAX;
	unsigned long flags = 0;

	if (!strcmp(led_cdev->name, "red")) {
		color = LED_COLOR_RED;
	} else if (!strcmp(led_cdev->name, "green")) {
		color = LED_COLOR_GREEN;
	} else {
		color = LED_COLOR_BLUE;
	}
	tricolor_led = container_of(led_cdev, struct tricolor_led_data, leds[color]);
	if(!tricolor_led)
		printk(KERN_ERR "%s tricolor_led is NULL ",__func__);

	spin_lock_irqsave(&tricolor_led->led_lock, flags);
	if(brightness){
		if(color == LED_COLOR_RED)
			input = RED_ON;
		if(color == LED_COLOR_GREEN)
			input = GREEN_ON;
		if(color == LED_COLOR_BLUE)
			input = BLUE_ON;
	} else {
		if(color == LED_COLOR_RED)
			input = RED_OFF;
		if(color == LED_COLOR_GREEN)
			input = GREEN_OFF;
		if(color == LED_COLOR_BLUE)
			input = BLUE_OFF;
	}
	spin_unlock_irqrestore(&tricolor_led->led_lock, flags);
	call_oem_rapi_client_streaming_function(tricolor_led->rpc_client, (char*)&input);
}

static void led_brightness_set_flash(struct led_classdev *led_cdev,
				     enum led_brightness brightness)
{
	if(brightness){
		gpio_set_value(LED_FLASH, 1);
	} else {
		gpio_set_value(LED_FLASH, 0);
	}
}

#ifdef CONFIG_KBDLED_SUPPORT
static void led_brightness_set_backlight(struct led_classdev *led_cdev, enum led_brightness brightness)
{
	printk(KERN_ERR "led_brightness_set_backlight  brightness = %d\n",brightness);
#ifdef CONFIG_LCT_AW550
	if(brightness)
	{
		gpio_set_value(LED_GPIO, 1); 	
	} 
	else 
	{
		gpio_set_value(LED_GPIO, 0); 	
	}
#else /* CONFIG_LCT_AW550 */
	if(brightness)
	{
		pmic_led_set_stat_backlight(5,1,0); 	
	} 
	else 
	{
		pmic_led_set_stat_backlight(5,0,0);
	}
#endif /* CONFIG_LCT_AW550 */
}
#endif

static int tricolor_led_probe(struct platform_device *pdev)
{
	int ret = 0;
	int i, j;
	struct tricolor_led_data *tricolor_led;
	printk(KERN_ERR "tricolor leds and flashlight: probe init \n");

	tricolor_led = kzalloc(sizeof(struct tricolor_led_data), GFP_KERNEL);
	if (tricolor_led == NULL) {
		printk(KERN_ERR "tricolor_led_probe: no memory for device\n");
		ret = -ENOMEM;
		goto err;
	}
	memset(tricolor_led, 0, sizeof(struct tricolor_led_data));

	/* initialize tricolor_led->pc_client */
	tricolor_led->rpc_client = oem_rapi_client_init();
	ret = IS_ERR(tricolor_led->rpc_client);
	if (ret) {
		printk(KERN_ERR "[tricolor-led] cannot initialize rpc_client!\n");
		tricolor_led->rpc_client = NULL;
		goto err_init_rpc_client;
	}

	tricolor_led->leds[0].name = "red";
	tricolor_led->leds[0].brightness_set = led_brightness_set_tricolor;

	tricolor_led->leds[1].name = "green";
	tricolor_led->leds[1].brightness_set = led_brightness_set_tricolor;

	tricolor_led->leds[2].name = "blue";
	tricolor_led->leds[2].brightness_set = led_brightness_set_tricolor;
	
	tricolor_led->leds[3].name = "flashlight";
	tricolor_led->leds[3].brightness_set = led_brightness_set_flash;

	#ifdef CONFIG_KBDLED_SUPPORT
	tricolor_led->leds[4].name = "button-backlight";
	tricolor_led->leds[4].brightness_set = led_brightness_set_backlight;
	#endif
	#ifdef CONFIG_KBDLED_SUPPORT
	for (i = 0; i < 5; i++) 
	#else
	for (i = 0; i < 4; i++) 
	#endif
	{	/* red, green, blue, flashlight */
		ret = led_classdev_register(&pdev->dev, &tricolor_led->leds[i]);
		if (ret) {
			printk(KERN_ERR
			       "tricolor_led: led_classdev_register failed\n");
			goto err_led_classdev_register_failed;
		}
	}
	
#ifdef CONFIG_KBDLED_SUPPORT
	for (i = 0; i < 5; i++) 
#else
	for (i = 0; i < 4; i++) 
#endif
	{
		ret = device_create_file(tricolor_led->leds[i].dev, &dev_attr_blink);
		if (ret) {
			printk(KERN_ERR
			       "tricolor_led: device_create_file failed\n");
			goto err_out_attr_blink;
		}
	}
	dev_set_drvdata(&pdev->dev, tricolor_led);
	return 0;

err_out_attr_blink:
        for (j = 0; j < i; j++)
                device_remove_file(tricolor_led->leds[j].dev, &dev_attr_blink);
		#ifdef CONFIG_KBDLED_SUPPORT
		i = 5;
		#else
        i = 4;
		#endif

err_led_classdev_register_failed:
	for (j = 0; j < i; j++)
		led_classdev_unregister(&tricolor_led->leds[j]);

err_init_rpc_client:
	/* If above errors occurred, close pdata->rpc_client */
	if (tricolor_led->rpc_client) {
		oem_rapi_client_close();
		printk(KERN_ERR "tri-color-led: oem_rapi_client_close\n");
	}
	kfree(tricolor_led);
err:
	return ret;
}

static int __devexit tricolor_led_remove(struct platform_device *pdev)
{
	struct tricolor_led_data *tricolor_led;
	int i;
	printk(KERN_ERR "tricolor_led_remove: remove\n");

	tricolor_led = platform_get_drvdata(pdev);
	
#ifdef CONFIG_KBDLED_SUPPORT
	for (i = 0; i < 5; i++) 
#else
	for (i = 0; i < 4; i++) 
#endif
	{
		device_remove_file(tricolor_led->leds[i].dev, &dev_attr_blink);
		led_classdev_unregister(&tricolor_led->leds[i]);
	}
	/* close tricolor_led->rpc_client */
	oem_rapi_client_close();
	tricolor_led->rpc_client = NULL;

	kfree(tricolor_led);
	return 0;
}

static struct platform_driver tricolor_led_driver = {
	.probe = tricolor_led_probe,
	.remove = __devexit_p(tricolor_led_remove),
	.suspend = NULL,
	.resume = NULL,
	.driver = {
		   .name = "tricolor leds and flashlight",
		   .owner = THIS_MODULE,
		   },
};

static int __init tricolor_led_init(void)
{
	printk(KERN_ERR "tricolor_leds_backlight_init: module init\n");
	return platform_driver_register(&tricolor_led_driver);
}

static void __exit tricolor_led_exit(void)
{
	printk(KERN_ERR "tricolor_leds_backlight_exit: module exit\n");
	platform_driver_unregister(&tricolor_led_driver);
}

MODULE_AUTHOR("rockie cheng");
MODULE_DESCRIPTION("tricolor leds and flashlight driver");
MODULE_LICENSE("GPL");

module_init(tricolor_led_init);
module_exit(tricolor_led_exit);
