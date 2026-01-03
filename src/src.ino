#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "esp_camera.h"
#include "camera.h"
#include "html.h"
#include "vec2.h"
#include <Preferences.h>

// ====== Wi-Fi ======
const char* WIFI_SSID = "SSID";
const char* WIFI_PASS = "PASS";

AsyncWebServer server(80);

Preferences prefs;
vec2 calibrations[4] =
{
  {0.0, 0.0},
  {1.0, 0.0},
  {0.0, 1.0},
  {1.0, 1.0}
};

struct Status
{
	vec2 shots[10] = { };
	uint8_t shotsCount = 0;

	vec2 center;
	vec2 rightBasis;
	vec2 downBasis;
	float rightBasisInvLenSqr;
	float downBasisInvLenSqr;
};

Status g_status;
portMUX_TYPE dataMux = portMUX_INITIALIZER_UNLOCKED;
bool g_calibrateMode = false;
float Hinv[9];

void computeHomographyInv()
{
	float x0 = calibrations[0].x, y0 = calibrations[0].y; // A
	float x1 = calibrations[1].x, y1 = calibrations[1].y; // B
	float x2 = calibrations[2].x, y2 = calibrations[2].y; // C
	float x3 = calibrations[3].x, y3 = calibrations[3].y; // D

	float H[9];

	float dx1 = x1 - x2;
	float dy1 = y1 - y2;
	float dx2 = x3 - x2;
	float dy2 = y3 - y2;

	float dx3 = x0 - x1 + x2 - x3;
	float dy3 = y0 - y1 + y2 - y3;

	float det = dx1 * dy2 - dx2 * dy1;

	float g = (dx3 * dy2 - dx2 * dy3) / det;
	float h = (dx1 * dy3 - dx3 * dy1) / det;

	H[0] = x1 - x0 + g * x1;
	H[1] = x3 - x0 + h * x3;
	H[2] = x0;

	H[3] = y1 - y0 + g * y1;
	H[4] = y3 - y0 + h * y3;
	H[5] = y0;

	H[6] = g;
	H[7] = h;
	H[8] = 1.0f;

	float detH =
		H[0] * (H[4] * H[8] - H[5] * H[7]) -
		H[1] * (H[3] * H[8] - H[5] * H[6]) +
		H[2] * (H[3] * H[7] - H[4] * H[6]);

	float invDet = 1.0f / detH;

	Hinv[0] = (H[4] * H[8] - H[5] * H[7]) * invDet;
	Hinv[1] = -(H[1] * H[8] - H[2] * H[7]) * invDet;
	Hinv[2] = (H[1] * H[5] - H[2] * H[4]) * invDet;

	Hinv[3] = -(H[3] * H[8] - H[5] * H[6]) * invDet;
	Hinv[4] = (H[0] * H[8] - H[2] * H[6]) * invDet;
	Hinv[5] = -(H[0] * H[5] - H[2] * H[3]) * invDet;

	Hinv[6] = (H[3] * H[7] - H[4] * H[6]) * invDet;
	Hinv[7] = -(H[0] * H[7] - H[1] * H[6]) * invDet;
	Hinv[8] = (H[0] * H[4] - H[1] * H[3]) * invDet;
}

vec2 mapPointToSquare(const vec2 P)
{
	float u = Hinv[0] * P.x + Hinv[1] * P.y + Hinv[2];
	float v = Hinv[3] * P.x + Hinv[4] * P.y + Hinv[5];
	float w = Hinv[6] * P.x + Hinv[7] * P.y + Hinv[8];

	vec2 r;
	r.x = u / w;
	r.y = v / w;
	return r;
}

void setupRoutes()
{
	auto indexHandler = [](AsyncWebServerRequest* request) {
		g_calibrateMode = false;
		request->send(200, "text/html", index_html);
		sensor_t* s = esp_camera_sensor_get();
		s->set_ae_level(s, AUTO_EXPOSURE_LEVEL);
		};

	server.on("/", HTTP_GET, indexHandler);
	server.on("/index.html", HTTP_GET, indexHandler);

	server.on("/calibrate.html", HTTP_GET, [](AsyncWebServerRequest* request) {
		g_calibrateMode = true;
		request->send(200, "text/html", calibrate_html);
		sensor_t* s = esp_camera_sensor_get();
		s->set_ae_level(s, +1);
		});

	server.on("/raw.jpg", HTTP_GET, [](AsyncWebServerRequest* request) {
		g_calibrateMode = true;

		camera_fb_t* fb = esp_camera_fb_get();

		if (!fb)
		{
			request->send(500, "text/plain", "Camera Capture Failed");
			return;
		}
		// copy
		size_t len = fb->len;
		uint8_t* copyBuf = (uint8_t*)heap_caps_malloc(len, /* MALLOC_CAP_SPIRAM | */ MALLOC_CAP_8BIT);
		memcpy(copyBuf, fb->buf, len);
		esp_camera_fb_return(fb);

		// send as jpg
		AsyncWebServerResponse* response = request->beginResponse(200, "image/jpeg", (const uint8_t*)copyBuf, len);
		response->addHeader("Cache-Control", "no-store"); // without cache
		request->onDisconnect([copyBuf]() {
			free(copyBuf);
			});
		request->send(response);
		});

	server.on("/points", HTTP_POST, [](AsyncWebServerRequest* request) {}, NULL,
		[](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t, size_t) {
			StaticJsonDocument<512> doc;
			deserializeJson(doc, data);

			for (int i = 0; i < doc.size(); i++)
			{
				float x = doc[i]["x"];
				float y = doc[i]["y"];
				calibrations[i].x = x;
				calibrations[i].y = y;
				Serial.printf("Point %d: %.3f %.3f\n", i, x, y);

				prefs.begin("config", false);
				prefs.putBytes("calibrations", calibrations, sizeof(calibrations));
				prefs.end();
				computeHomographyInv();
			}

			request->send(200, "text/plain", "OK");
		});

	server.on("/data.txt", HTTP_GET, [](AsyncWebServerRequest* request) {
		g_calibrateMode = false;
		String responseStr;
		responseStr.reserve(256);

		taskENTER_CRITICAL(&dataMux);
		responseStr = String(g_status.shotsCount);
		for (int i = 0; i < g_status.shotsCount; i++)
		{
			responseStr += "\n" + String(g_status.shots[i].x, 3) + "\n" + String(g_status.shots[i].y, 3);
		}
		taskEXIT_CRITICAL(&dataMux);

		AsyncWebServerResponse* response = request->beginResponse(200, "text/plain", responseStr);
		response->addHeader("Cache-Control", "no-store"); // without cache
		response->addHeader("Connection", "keep-alive");
		request->send(response);
		});

	server.on("/sound.mp3", HTTP_GET, [](AsyncWebServerRequest* request) {
		AsyncWebServerResponse* response = request->beginResponse(200, "audio/mp3", (const uint8_t*)soundFile, sizeof(soundFile));
		request->send(response);
		});
}

void setup()
{
	Serial.begin(115200);
	Serial.println();

	// ===== Wi-Fi =====
	WiFi.setSleep(false);
	WiFi.begin(WIFI_SSID, WIFI_PASS);
	Serial.printf("Connecting to %s", WIFI_SSID);
	while (WiFi.status() != WL_CONNECTED)
	{
		delay(500);
		Serial.print(".");
	}
	Serial.println("\nConnected!");
	Serial.println(WiFi.localIP());

	// ===== Camera =====
	initCamera();

	// ===== Config =====
	prefs.begin("config", true);
	prefs.getBytes("calibrations", calibrations, sizeof(calibrations));
	prefs.end();
	computeHomographyInv();

	// ===== HTTP =====
	setupRoutes();
	server.begin();

	Serial.println("Server started");

	// We perform heavy image processing on the second core
	xTaskCreatePinnedToCore(worker, "worker", 8192, NULL, 1, NULL, 1);
}

void loop()
{
	delay(1000000);
}

void worker(void*)
{
	unsigned long frames = 0;
	unsigned long lastTime = millis();
	long lastShotTime = millis() + 3000; // time to camera adaptation
	const long shotDelay = 200;

	Serial.println("Worker started");

	while (true)
	{
		if (!g_calibrateMode)
		{
			if (camera_fb_t* fb = esp_camera_fb_get())
			{
				vec2 shotPos;
				if (bool hasShot = findShot(fb, shotPos); hasShot && (((long)millis() - lastShotTime) >= shotDelay))
				{
					lastShotTime = millis();

					shotPos = mapPointToSquare(shotPos);
					shotPos = shotPos * 2.0f - vec2(1.0f, 1.0f);

					//Serial.printf("Shot: %.3f %.3f\n", shotPos.x, shotPos.y);

					taskENTER_CRITICAL(&dataMux);
					{
						g_status.shotsCount++;
						if (g_status.shotsCount == 10) lastShotTime += 2000;
						if (g_status.shotsCount > 10) g_status.shotsCount = 1;

						g_status.shots[g_status.shotsCount - 1].x = shotPos.x;
						g_status.shots[g_status.shotsCount - 1].y = shotPos.y;
					}
					taskEXIT_CRITICAL(&dataMux);
				}

				esp_camera_fb_return(fb);
				frames++;

				unsigned long now = millis();
				if (now - lastTime >= 1000) // 1s
				{
					//Serial.printf("frames/sec =  %d\n", frames);
					lastTime = now;
					frames = 0;
				}
			}
			else
			{
				Serial.println("Сam fail");
			}
		}

		vTaskDelay(1);
	}
}
