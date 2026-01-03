#pragma once

// ===================
// Select camera model
// ===================
#define CAMERA_MODEL_WROVER_KIT // Has PSRAM
//#define CAMERA_MODEL_ESP_EYE // Has PSRAM
//#define CAMERA_MODEL_ESP32S3_EYE // Has PSRAM
//#define CAMERA_MODEL_M5STACK_PSRAM // Has PSRAM
//#define CAMERA_MODEL_M5STACK_V2_PSRAM // M5Camera version B Has PSRAM
//#define CAMERA_MODEL_M5STACK_WIDE // Has PSRAM
//#define CAMERA_MODEL_M5STACK_ESP32CAM // No PSRAM
//#define CAMERA_MODEL_M5STACK_UNITCAM // No PSRAM
//#define CAMERA_MODEL_AI_THINKER // Has PSRAM
//#define CAMERA_MODEL_TTGO_T_JOURNAL // No PSRAM
// ** Espressif Internal Boards **
//#define CAMERA_MODEL_ESP32_CAM_BOARD
//#define CAMERA_MODEL_ESP32S2_CAM_BOARD
//#define CAMERA_MODEL_ESP32S3_CAM_LCD

constexpr int AUTO_EXPOSURE_LEVEL = -1; // exposure offset for more reliable hit capture
constexpr framesize_t FRAME_MODE = FRAMESIZE_240X240;
constexpr int BRIGHTNESS_THRESHOLD = 220;

#include "camera_pins.h"
#include "vec2.h"
#include <JPEGDEC.h>

bool initCamera()
{
	camera_config_t config;

	config.ledc_channel = LEDC_CHANNEL_0;
	config.ledc_timer = LEDC_TIMER_0;
	config.pin_d0 = Y2_GPIO_NUM;
	config.pin_d1 = Y3_GPIO_NUM;
	config.pin_d2 = Y4_GPIO_NUM;
	config.pin_d3 = Y5_GPIO_NUM;
	config.pin_d4 = Y6_GPIO_NUM;
	config.pin_d5 = Y7_GPIO_NUM;
	config.pin_d6 = Y8_GPIO_NUM;
	config.pin_d7 = Y9_GPIO_NUM;
	config.pin_xclk = XCLK_GPIO_NUM;
	config.pin_pclk = PCLK_GPIO_NUM;
	config.pin_vsync = VSYNC_GPIO_NUM;
	config.pin_href = HREF_GPIO_NUM;
	config.pin_sscb_sda = SIOD_GPIO_NUM;
	config.pin_sscb_scl = SIOC_GPIO_NUM;
	config.pin_pwdn = PWDN_GPIO_NUM;
	config.pin_reset = RESET_GPIO_NUM;
	config.xclk_freq_hz = 24000000;

	config.pixel_format = PIXFORMAT_JPEG; //PIXFORMAT_GRAYSCALE PIXFORMAT_JPEG  PIXFORMAT_YUV422 PIXFORMAT_RGB565
	config.grab_mode = CAMERA_GRAB_LATEST; //CAMERA_GRAB_LATEST CAMERA_GRAB_WHEN_EMPTY
	config.fb_location = CAMERA_FB_IN_PSRAM; //CAMERA_FB_IN_PSRAM CAMERA_FB_IN_DRAM
	config.jpeg_quality = 4;
	config.fb_count = 3;
	config.frame_size = FRAME_MODE;

	esp_err_t err = esp_camera_init(&config);
	if (err != ESP_OK)
	{
		Serial.printf("Camera init failed with error 0x%x", err);
		// dead loop
		while (true) delay(1000);
	}

	delay(200);

	sensor_t* s = esp_camera_sensor_get();
	s->set_whitebal(s, 0);
	s->set_awb_gain(s, 0);
	s->set_ae_level(s, AUTO_EXPOSURE_LEVEL);
	s->set_aec2(s, 0); // classic mode
	s->set_saturation(s, -2);
	s->set_special_effect(s, 2); // grayscale
	s->set_lenc(s, 1);
	s->set_wpc(s, 0);
	s->set_bpc(s, 0);
	s->set_raw_gma(s, 1);

	return true;
}

size_t g_count = 0;
size_t g_xPos = 0;
size_t g_yPos = 0;

int colbackMCU(JPEGDRAW* pDraw)
{
	uint8_t* src = (uint8_t*)pDraw->pPixels;

	for (int dy = 0; dy < pDraw->iHeight; dy++)
		for (int dx = 0; dx < pDraw->iWidth; dx++)
		{
			uint8_t bright = *src++;
			if (bright > BRIGHTNESS_THRESHOLD)
			{
				g_count++;
				g_xPos += pDraw->x + dx;
				g_yPos += pDraw->y + dy;
			}
		}
	return 1; // continue
}

JPEGDEC jpeg;
bool findShot(camera_fb_t* fb, vec2& pos)
{
	g_count = 0;
	g_xPos = 0;
	g_yPos = 0;

	jpeg.openRAM(fb->buf, fb->len, colbackMCU);
	jpeg.setPixelType(EIGHT_BIT_GRAYSCALE);
	jpeg.decode(0, 0, 0);
	jpeg.close();

	if (g_count > 0 && g_count < 128)
	{
		pos.x = (float)g_xPos / ((float)g_count * (float)fb->width);
		pos.y = (float)g_yPos / ((float)g_count * (float)fb->height);
		//Serial.printf("Count: %d   x: %d y: %d\n", g_count, g_xPos / g_count, g_yPos / g_count);
		return true;
	}
	return false;
}