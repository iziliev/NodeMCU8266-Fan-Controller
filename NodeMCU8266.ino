#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include <Wire.h>
#include <ArduinoJson.h>

// --- Настройки ---
#define WIFI_SSID "Ilievi"
#define WIFI_PASSWORD "Ilievi8404038868"

#define BOT_TOKEN "8216030461:AAEi4NMikQECugsB_o-psymdKvh06Q0fgcc"
#define CHAT_ID "6767873175"

#define DHTPIN D2
#define DHTTYPE DHT22
#define RELAY_PIN D5

#define OLED_SDA D1
#define OLED_SCL D6

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// Безопасни температури за оборудването
#define TEMP_CRITICAL 50  // Критична температура - незабавно действие
#define TEMP_WARNING 34.9   // Предупреждение за висока температура
#define TEMP_ON 35        // Включване на вентилаторите
#define TEMP_OFF 32       // Изключване на вентилаторите

#define GRAPH_POINTS 4
#define GRAPH_INTERVAL 5000  // 5 секунди

DHT dht(DHTPIN, DHTTYPE);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

float graphTemps[GRAPH_POINTS] = {0,0,0,0};
int graphIndex = 0;

bool fanState = false;
unsigned long lastCheck = 0;
unsigned long lastTempAlert = 0;
long last_update_id = 0;

// --- Тест Telegram ---
//void testTelegramConnection() {
//  WiFiClientSecure client;
//  client.setInsecure();
//  if (!client.connect("api.telegram.org", 443)) return;
//  String url = "/bot" + String(BOT_TOKEN) + "/getMe";
//  client.print(String("GET ") + url + " HTTP/1.1\r\nHost: api.telegram.org\r\nConnection: close\r\n\r\n");
//  delay(500);
//  client.stop();
//}

// --- Функция за тестване на връзката с Telegram ---
void testTelegramConnection() {
  WiFiClientSecure client;
  client.setInsecure();
  
  Serial.println("Testing Telegram connection...");
  
  if (!client.connect("api.telegram.org", 443)) {
    Serial.println("Connection to Telegram failed");
    return;
  }
  
  String url = "/bot" + String(BOT_TOKEN) + "/getMe";
  client.print(String("GET ") + url + " HTTP/1.1\r\n" +
               "Host: api.telegram.org\r\n" +
               "Connection: close\r\n\r\n");
  
  // Изчакване на отговор
  delay(1000);
  
  Serial.println("Telegram response:");
  while (client.connected() || client.available()) {
    if (client.available()) {
      String line = client.readStringUntil('\n');
      Serial.println(line);
    }
  }
  
  client.stop();
}

// --- Изпращане на съобщение ---
void sendTelegramMessage(String message) {
  WiFiClientSecure client;
  client.setInsecure();
  if (!client.connect("api.telegram.org", 443)) return;

  message.replace(" ", "%20");
  message.replace("\n", "%0A");
  message.replace(":", "%3A");
  message.replace("°", "%C2%B0");

  String url = "/bot" + String(BOT_TOKEN) + "/sendMessage?chat_id=" + CHAT_ID + "&text=" + message;
  client.print(String("GET ") + url + " HTTP/1.1\r\nHost: api.telegram.org\r\nConnection: close\r\n\r\n");
  client.stop();
}

// --- Функция за получаване на команди ---
String getTelegramUpdates() {
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(5000);

  if (!client.connect("api.telegram.org", 443)) {
    Serial.println("Connection to Telegram failed");
    return "";
  }

  String url = "/bot" + String(BOT_TOKEN) + "/getUpdates?timeout=5";
  if (last_update_id != 0) {
    url += "&offset=" + String(last_update_id + 1);
  }
  
  client.print(String("GET ") + url + " HTTP/1.1\r\n" +
               "Host: api.telegram.org\r\n" +
               "User-Agent: ESP8266\r\n" +
               "Connection: close\r\n\r\n");

  // Изчакване на отговор
  unsigned long timeout = millis();
  while (client.connected() && millis() - timeout < 5000) {
    if (client.available()) break;
    delay(10);
  }

  String response;
  while (client.available()) {
    response += client.readString();
  }
  
  client.stop();

  // Намиране на началото на JSON отговора
  int jsonStart = response.indexOf('{');
  if (jsonStart == -1) {
    Serial.println("No JSON found in response");
    return "";
  }
  
  String json = response.substring(jsonStart);
  Serial.println("Received JSON: " + json);

  // Парсване на JSON
  DynamicJsonDocument doc(2048);
  DeserializationError error = deserializeJson(doc, json);
  
  if (error) {
    Serial.print("JSON parse error: ");
    Serial.println(error.c_str());
    return "";
  }

  if (doc["ok"] == true && doc["result"].size() > 0) {
    JsonObject result = doc["result"][0];
    last_update_id = result["update_id"].as<long>();
    String text = result["message"]["text"].as<String>();
    Serial.println("Received command: " + text);
    return text;
  }

  return "";
}

// --- Графика ---
void updateTempHistory(float newTemp) {
  graphTemps[graphIndex] = newTemp;
  graphIndex = (graphIndex + 1) % GRAPH_POINTS;
}

void drawGraph(){
  int graphBottom = 60;
  int graphHeight = 20;
  int padding = 6; // Отстъп от краищата
  int graphLeft = padding;
  int graphRight = SCREEN_WIDTH - padding;
  int graphWidth = graphRight - graphLeft;
  float tempMin = 20;
  float tempMax = 70;

  for(int i=0;i<GRAPH_POINTS;i++){
    int idx = (graphIndex + i) % GRAPH_POINTS;
    int x = graphLeft + i*(graphWidth/(GRAPH_POINTS-1));
    int y = graphBottom - int((graphTemps[idx]-tempMin)*graphHeight/(tempMax-tempMin));

    display.fillCircle(x,y,2,WHITE);

    // Число над точката (центрирано)
    String tempStr = String(int(graphTemps[idx]));
    int16_t tx, ty; uint16_t tw, th;
    display.getTextBounds(tempStr.c_str(),0,0,&tx,&ty,&tw,&th);
    display.setCursor(x - tw/2, y - 10);
    display.print(tempStr);

    // Линия към следващата точка
    if(i<GRAPH_POINTS-1){
      int next_idx = (graphIndex + i + 1) % GRAPH_POINTS;
      int next_x = graphLeft + (i+1)*(graphWidth/(GRAPH_POINTS-1));
      int next_y = graphBottom - int((graphTemps[next_idx]-tempMin)*graphHeight/(tempMax-tempMin));
      display.drawLine(x,y,next_x,next_y,WHITE);
    }
  }
}

// --- Setup ---
void setup() {
  Serial.begin(115200);
  dht.begin();
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

  Wire.begin(OLED_SDA, OLED_SCL);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.setRotation(2);
  display.clearDisplay();
  display.setTextColor(WHITE);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) delay(500);

// Тестване на връзката с Telegram
  testTelegramConnection();
  //sendTelegramMessage("🤖 Ботът е стартиран! 🌡️");
  // Изпращане на начално съобщение
  sendTelegramMessage("🤖 Ботът е стартиран и готов за работа! 🌡️");
  sendTelegramMessage("📊 Температурни настройки:\n• Предупреждение: " + String(TEMP_WARNING) + "°C\n• Включване: " + String(TEMP_ON) + "°C\n• Изключване: " + String(TEMP_OFF) + "°C\n• Критична: " + String(TEMP_CRITICAL) + "°C");
  
  delay(2000);
}

// --- Loop ---
void loop() {
  float temp = dht.readTemperature();
  if (isnan(temp)) {
    Serial.println("Failed to read from DHT");
    return;
  }

  updateTempHistory(temp);

  display.clearDisplay();

  // IP
  display.setTextSize(1);
  display.setCursor(0,0);
  display.print("IP: " + WiFi.localIP().toString());

  // Температура
  display.setTextSize(2);
  display.setCursor(0,15);
  display.print(String(temp));

  // Малък кръг градус
  int16_t bx, by; uint16_t bw, bh;
  display.getTextBounds(String(temp).c_str(),0,0,&bx,&by,&bw,&bh);
  display.fillCircle(bw+2,19,2,WHITE);
  display.setCursor(bw+6,15);
  display.print("C");

  // FAN срещу температурата
  String fanText = String("FAN:") + (fanState?"ON":"OFF");
  display.setTextSize(1);
  int16_t tx, ty; uint16_t tw, th;
  display.getTextBounds(fanText.c_str(),0,0,&tx,&ty,&tw,&th);
  display.setCursor(SCREEN_WIDTH - tw - 2, 15);
  display.print(fanText);

  // Графика
  drawGraph();
  display.display();

  // Проверка за критични температури
  if (temp >= TEMP_CRITICAL) {
    digitalWrite(RELAY_PIN, HIGH);
    fanState = true;
    if (millis() - lastTempAlert > 60000) {  // Изпращай предупреждение само веднъж на минута
      sendTelegramMessage("🚨 КРИТИЧНА ТЕМПЕРАТУРА: " + String(temp) + "°C\nВентилаторите са принудително включени!\nНезабавно проверете оборудването!");
      lastTempAlert = millis();
    }
  }
  // Проверка за предупреждение за висока температура
  else if (temp >= TEMP_WARNING && millis() - lastTempAlert > 300000) {  // Предупреждение на всеки 5 минути
    sendTelegramMessage("⚠️ Висока температура: " + String(temp) + "°C\nОборудването е в риск!");
    lastTempAlert = millis();
  }
  // Автоматично управление на вентилатора
  else if (temp >= TEMP_ON && !fanState) {
    digitalWrite(RELAY_PIN, HIGH);
    fanState = true;
    sendTelegramMessage("🌡️ Температура: " + String(temp) + "°C\nВентилаторите се включват");
  } 
  else if (temp <= TEMP_OFF && fanState) {
    digitalWrite(RELAY_PIN, LOW);
    fanState = false;
    sendTelegramMessage("✅ Температура: " + String(temp) + "°C\nВентилаторите се изключват");
  }

  // Проверка за Telegram команди на всеки 5 сек
  if (millis() - lastCheck > 5000) {
    lastCheck = millis();
    String cmd = getTelegramUpdates();

    if (cmd == "/temp") {
      String statusMsg = "🌡️ Текуща температура: " + String(temp) + "°C\n";
      statusMsg += "🎛️ Състояние на вентилаторите: " + String(fanState ? "Включени" : "Изключени");
      sendTelegramMessage(statusMsg);
    } else if (cmd == "/status") {
      String statusMsg = "📊 СТАТУС НА СИСТЕМАТА:\n";
      statusMsg += "🌡️ Температура: " + String(temp) + "°C\n";
      statusMsg += "🎛️ Вентилатори: " + String(fanState ? "Включени" : "Изключени") + "\n";
      statusMsg += "📶 WiFi: " + String(WiFi.RSSI()) + " dBm\n";
      statusMsg += "🔌 Мрежа: " + WiFi.localIP().toString();
      sendTelegramMessage(statusMsg);
    } else if (cmd == "/help") {
      String helpMsg = "📌 НАЛИЧНИ КОМАНДИ:\n";
      helpMsg += "/temp - текуща температура\n";
      helpMsg += "/status - пълен статус на системата\n";
      helpMsg += "/help - показва помощно съобщение\n\n";
      helpMsg += "🌡️ ТЕМПЕРАТУРНИ НАСТРОЙКИ:\n";
      helpMsg += "• Предупреждение: " + String(TEMP_WARNING) + "°C\n";
      helpMsg += "• Включване: " + String(TEMP_ON) + "°C\n";
      helpMsg += "• Изключване: " + String(TEMP_OFF) + "°C\n";
      helpMsg += "• Критична: " + String(TEMP_CRITICAL) + "°C";
      sendTelegramMessage(helpMsg);
    } else if (cmd == "/fan_on") {
      digitalWrite(RELAY_PIN, HIGH);
      fanState = true;
      sendTelegramMessage("🔛 Вентилаторите са ръчно включени\n🌡️ Температура: " + String(temp) + "°C");
    } else if (cmd == "/fan_off") {
      digitalWrite(RELAY_PIN, LOW);
      fanState = false;
      sendTelegramMessage("🔴 Вентилаторите са ръчно изключени\n🌡️ Температура: " + String(temp) + "°C");
    } else if (cmd != "" && cmd != "/start") {
      sendTelegramMessage("❌ Неразпозната команда: " + cmd + "\nИзползвайте /help за списък с команди.");
    }
  }

  delay(5000);
}
