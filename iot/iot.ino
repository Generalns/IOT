/*

ağırlık 0 dan aşağı olduysa sürahi kaldırıldı, sıcaklık ölçmeye gerek yok 10 daniyede bir ölçmeye devam et
Ağırlık 0 civarlarındaysa içi boş 10 saniyede bir örnekleyebilir, sıckalık ölçme
Ağırlık 0 dan fazlaysa artık kahve dolu sıcaklık ölç 5 saniyede bir bunu yapabilirsin mesela, diğerine göre daha kısa sürsün

*/

#include "HX711.h"
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>

#define CONNECTION_WARNING_LED D7
#define GREEN_LED D6
#define TEMP_WARNING_LED D5
#define LOADCELL_DOUT_PIN D4
#define LOADCELL_SCK_PIN D3
#define TEMP_PIN A0

// sıcaklık
const float B = 3950;
const float R0 = 100000;
float thermistorValue;
float min_tempareture = 30;
float temp_avg = 0.0;
int temp_avg_count = 0;

// ağırlık
HX711 scale;
float calibration_factor = 211;
float jug_weight = 200;
float weight_error = 10;
float weight;
float last_weight;

// delay
int low_delay = 3000;
int hight_delay = 10000;
int current_delay;
unsigned long last_delay_time = 0;
int data_sending_delay = 15000;
int ping_sending_delay = 20000;
unsigned long last_ping_time = 0;
unsigned long last_data_time = 0;

// eeprom data
struct Data
{
  float temp;
  float weight;
  unsigned long unixTime;
};
const int maxDataCount = 5;
const int dataSize = sizeof(Data);
const int totalDataSize = maxDataCount * dataSize;

// WiFi credentials
const char *ssid = "UzunKilic2.4";
const char *password = "Bar1st@!";

// API details
const char *apiUrl = "http://192.168.1.65";
const int apiPort = 8000;
const char *apiSensorEndpoint = "/api/sensor";
const char *apiPingEndpoint = "/api/ping";
const char *apiUnixEndpoint = "/api/unix";

// unix time
unsigned long startUnixTime; // Başlangıç için Unix zamanı

// TODO: eeproam kullanmak için bunu false yap, gereksiz eeproam kulllanma cihaz bozulmasın, proje çok fazla kullanıyor
bool EEPROM_OFF = true;

WiFiClient client;

void setup()
{

  Serial.begin(9600);

  // led pinleri
  pinMode(CONNECTION_WARNING_LED, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);        // LED pinini çıkış olarak ayarla
  pinMode(TEMP_WARNING_LED, OUTPUT); // LED pinini çıkış olarak ayarla
  digitalWrite(GREEN_LED, LOW);
  digitalWrite(TEMP_WARNING_LED, LOW);
  digitalWrite(CONNECTION_WARNING_LED, LOW);

  // Connect to Wi-Fi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    digitalWrite(CONNECTION_WARNING_LED, HIGH);
    delay(500);
    digitalWrite(CONNECTION_WARNING_LED, LOW);
    delay(500);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi");

  // ağırlık sensörü
  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  scale.set_scale(calibration_factor);
  scale.tare();

  // başlangıç ölçümünü ölç son ağırlık ölçümü olarak at
  scale.power_up();
  last_weight = scale.get_units(5);
  last_weight = last_weight - jug_weight;
  if (last_weight < 0)
  {
    last_weight = 0;
  }
  scale.power_down();

  // wifi connectionu tamam olunca  startUnixTime  değişenini bir istek ile al ve bu değeri güncelle.
  String apiUrlWithEndpoint = String(apiUrl) + ":" + String(apiPort) + String(apiUnixEndpoint);
  HTTPClient http;
  http.begin(client, apiUrlWithEndpoint);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  int httpCode = http.GET();

  if (httpCode > 0)
  {
    String payload = http.getString();
    Serial.println(payload); // Print response
    DynamicJsonDocument doc(256);
    deserializeJson(doc, payload);
    startUnixTime = doc["unix"];
  }
  else
  {
    Serial.println("hata unix time alınamadı");
    startUnixTime = 1704037656;
  }
  http.end();

  // eeprom içindeki dataları yaz
  printData();

  // baslangıc delay değeri ata
  current_delay = hight_delay;

  start_led_signal();
}

void loop()
{

  unsigned long current_time = millis();

  // ölçüm yapma bloğu
  if (current_time - last_delay_time > current_delay)
  {

    last_delay_time = millis();

    // agırlık ölç
    scale.power_up();
    weight = scale.get_units(5);
    weight = weight - jug_weight;
    if (weight < 0)
    {
      weight = 0;
    }
    scale.power_down();

    Serial.print(weight);
    Serial.println(" grams,   ");

    if (weight > weight_error)
    { // sürahi dolu

      current_delay = low_delay;

      // sürahi dolu diye yeşil ışığı yak
      digitalWrite(GREEN_LED, HIGH);

      // sıcaklık ölçümü yap
      thermistorValue = Termistor();

      Serial.print(thermistorValue);
      Serial.println(" °C ");

      temp_avg = ((temp_avg * temp_avg_count) + thermistorValue) / (temp_avg_count + 1);
      temp_avg_count++;

      // sürahi dolu ve kahve sıcaklığı min sıcaklıktan soğuk ise kırmızı ışığı yak
      if (temp_avg < min_tempareture)
      {
        digitalWrite(TEMP_WARNING_LED, HIGH);
      }
      else
      { // sürahi dolu ve kahve sıcaklığı min sıcaklıktan yüksek kırmızı ışığı söndür
        digitalWrite(TEMP_WARNING_LED, LOW);
      }

      // ağırlıkta anlık değişim varsa data gönderme süresini beklemeden gönder data gönderme ve ping süresini güncelle
      if (abs(last_weight - weight) > weight_error)
      {

        last_ping_time = current_time;
        last_data_time = current_time;

        Serial.print("Ortalama Sıcaklık: ");
        Serial.println(temp_avg);
        Serial.println("Ağırlık çok değişti");
        send_data(weight, temp_avg);
        temp_avg = 0.0;
        temp_avg_count = 0;
      }
      else if (current_time - last_data_time > data_sending_delay)
      { // data gönderme süresi dolduysa gönder
        last_ping_time = current_time;
        last_data_time = current_time;

        Serial.print("Ortalama Sıcaklık: ");
        Serial.println(temp_avg);

        send_data(weight, temp_avg);
        temp_avg = 0.0;
        temp_avg_count = 0;
      }

      // son ölçülen ağırlığı güncelle
      last_weight = weight;
    }
    else
    { // sürahi boş

      current_delay = hight_delay;

      digitalWrite(GREEN_LED, LOW);
      digitalWrite(TEMP_WARNING_LED, LOW);
    }
  }

  // son atılan ping süresi dolmuş ping at
  if (current_time - last_ping_time > ping_sending_delay)
  {
    last_ping_time = current_time;
    bool result = ping();

    // ping başarılı olursa eski gönderilmeyen data varsa gönder
    if (result)
    {

      int validDataInEeprome = getValidDataCount();
      if (validDataInEeprome > 0)
      {
        Serial.println("gönderilemeyen data var ");
        // gönderilemeyen tüm dataları aşağıdaki işlemleri uygula
        for (int i = 0; i < validDataInEeprome; i++)
        {
          // gönderilemeyen datayı al ve eepromdan sil
          Data cantBeSendedData = removeAndReturnFirstData();
          //  burda cantBeSendedData içindeki verileri kullanarak dataları gönder.
          // Bunlar eskiden gönderilemeyn datadır
          sendDataToApi(cantBeSendedData.temp, cantBeSendedData.weight, cantBeSendedData.unixTime);

          Serial.print("gönderilemeyen data gönderildi:  ");
          Serial.print(": Time = ");
          Serial.print((cantBeSendedData.unixTime));
          Serial.print(", Temp = ");
          Serial.print(cantBeSendedData.temp);
          Serial.print(", Weight = ");
          Serial.println(cantBeSendedData.weight);

          printData();
        }
      }
    }
  }
}

// SICAKLIK ÖLÇÜMÜ
float Termistor()
{
  int sensorValue = analogRead(TEMP_PIN);
  float R = R0 * (1023.0 / sensorValue - 1.0);
  return 1.0 / ((log(R / R0) / B) + (1.0 / 298.15)) - 273.15;
}

bool ping()
{
  // Burada bizim api ye ping atılacak.
  // atılan ping sonucunda api ye ulaşılırsa bu fonksiyon true döndürmeli, aksi taktirde false döndürsün
  // CONNECTION_WARNING_LED ledini eğer ping başarılı olursa LOW başarısız olurs HIGH olur

  // bağlantı kopuksa bağlanmayı dene en fazla 4 kere dene
  int i = 0;
  while (WiFi.status() != WL_CONNECTED && i < 4)
  {
    Serial.println("Not connected to WiFi. Retrying...");
    i++;
    digitalWrite(CONNECTION_WARNING_LED, HIGH);
    delay(500);
    digitalWrite(CONNECTION_WARNING_LED, LOW);
    delay(500);
  }

  // denemeye rağmen bağlantı yoksa false döndür
  if (WiFi.status() != WL_CONNECTED)
  {
    digitalWrite(CONNECTION_WARNING_LED, HIGH);
    return false;
  }

  // bağlantı varsa connection ledini söndür
  digitalWrite(CONNECTION_WARNING_LED, LOW);

  // ping için get isteği yarat
  String apiUrlWithEndpoint = String(apiUrl) + ":" + String(apiPort) + String(apiPingEndpoint);
  HTTPClient http;
  http.begin(client, apiUrlWithEndpoint);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  int httpCode = http.GET();
  http.end();

  // başarılı değilse connection error ver ve connection hata ledini yak
  if (httpCode != 200)
  {
    Serial.print("ping başarısız, status kodu: ");
    Serial.println(httpCode);
    digitalWrite(CONNECTION_WARNING_LED, HIGH);
    return false;
  }

  Serial.print("ping başarılı");
  return true;
}

void send_data(float weight, float tempareture)
{
  // ilk olarak ping göder
  bool result = ping();
  unsigned long currentUnixTime = startUnixTime + ((millis() / 1000) % 60);
  //  eğer ping giderse  bağlantı var
  if (result)
  {

    // gönderilemeyen data var mı kontrol et
    int validDataInEeprome = getValidDataCount();
    if (validDataInEeprome > 0)
    {
      Serial.println("gönderilemeyen data var ");

      // gönderilemeyen tüm dataları aşağıdaki işlemleri uygula
      for (int i = 0; i < validDataInEeprome; i++)
      {
        // gönderilemeyen datayı al ve eepromdan sil
        Data cantBeSendedData = removeAndReturnFirstData();
        //  burda cantBeSendedData içindeki verileri kullanarak dataları gönder.
        // Bunlar eskiden gönderilemeyn datadır
        sendDataToApi(cantBeSendedData.temp, cantBeSendedData.weight, cantBeSendedData.unixTime);

        Serial.print("gönderilemeyen data gönderildi:  ");
        Serial.print(": Time = ");
        Serial.print((cantBeSendedData.unixTime));
        Serial.print(", Temp = ");
        Serial.print(cantBeSendedData.temp);
        Serial.print(", Weight = ");
        Serial.println(cantBeSendedData.weight);

        printData();
      }
    }
    // önceden gönderilemeyn dataları gönderdik, sıra aşağıdaki güncel datayı göndermekte
    sendDataToApi(tempareture, weight, currentUnixTime);
    Serial.print("suanki data gönderildi:  ");
    Serial.print(": Time = ");
    Serial.print((currentUnixTime));
    Serial.print(", Temp = ");
    Serial.print(tempareture);
    Serial.print(", Weight = ");
    Serial.println(weight);
  }
  else
  { /// eğer ping başarısız olursa datayı sakla
    Serial.println("Ping Başarısız Yeni Data Eklendi");
    Serial.print(": Time = ");
    Serial.print((currentUnixTime));
    Serial.print(", Temp = ");
    Serial.print(tempareture);
    Serial.print(", Weight = ");
    Serial.println(weight);

    Data lastData = {tempareture, weight, currentUnixTime};
    addData(lastData);
    printData();
  }
}

void sendDataToApi(float temp, float weight, unsigned long unixTime)
{

  String apiUrlWithEndpoint = String(apiUrl) + ":" + String(apiPort) + String(apiSensorEndpoint) + "?weight=" + String(weight) + "&temperature=" + String(temp) + "&unix=" + String(unixTime);

  HTTPClient http;
  http.begin(client, apiUrlWithEndpoint);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  Serial.println(http.getString());
  // Send the POST request
  int httpCode = http.POST("");
  Serial.print("veri gönderme, http status code: ");
  Serial.println(httpCode);

  if (httpCode > 0)
  {
    String response = http.getString();
    Serial.println("POST request sent successfully");
    Serial.println("Server response: " + response);
  }
  else
  {
    Serial.println("Error sending POST request");
  }
  http.end();
}

// Veri ekleme
void addData(Data data)
{
  // eeprom gereksiz kulanılmasın diye eklendi
  if (EEPROM_OFF)
  {
    return;
  }

  Data buffer;
  int emptyIndex = -1;

  // Boş veya ölü veri olan ilk indexi bul
  for (int i = 0; i < maxDataCount; i++)
  {
    EEPROM.get(i * dataSize, buffer);
    if (buffer.temp < 0 || buffer.weight < 0)
    {
      emptyIndex = i;
      break;
    }
  }

  // Eğer boş yer varsa oraya ekle, yoksa en eski veriyi sil
  if (emptyIndex == -1)
  {
    for (int i = 0; i < totalDataSize - dataSize; i += dataSize)
    {
      EEPROM.get(i + dataSize, buffer);
      EEPROM.put(i, buffer);
    }
    EEPROM.put(totalDataSize - dataSize, data);
  }
  else
  {
    EEPROM.put(emptyIndex * dataSize, data);
  }
}

// Verileri yazdırma
void printData()
{
  // eeprom gereksiz kulanılmasın diye eklendi
  if (EEPROM_OFF)
  {
    return;
  }

  Data data;
  Serial.println("EEPROM içindeki gönderilemeyn datalar:");
  for (int i = 0; i < maxDataCount; i++)
  {
    EEPROM.get(i * dataSize, data);
    if (data.temp >= 0 && data.weight >= 0)
    {
      Serial.print("Index ");
      Serial.print(i);
      Serial.print(": Time = ");
      Serial.print(data.unixTime);
      Serial.print(", Temp = ");
      Serial.print(data.temp);
      Serial.print(", Weight = ");
      Serial.println(data.weight);
    }
  }
}

// İlk veriyi sil ve yazdır
Data removeAndReturnFirstData()
{
  //  eeprom gereksiz kulanılmasın diye eklendi
  if (EEPROM_OFF)
  {
    return Data{-1.0, -1.0};
  }

  Data data;
  Data removedData;
  int firstValidIndex = -1;

  // İlk dolu veriyi bul
  for (int i = 0; i < maxDataCount; i++)
  {
    EEPROM.get(i * dataSize, data);
    if (data.temp >= 0 && data.weight >= 0)
    {
      firstValidIndex = i;
      break;
    }
  }

  // Eğer dolu bir veri bulunursa, onu sil
  if (firstValidIndex != -1)
  {
    EEPROM.get(firstValidIndex * dataSize, removedData);
    // Silinen veriden sonra gelen verileri kaydır
    for (int i = 0; i < totalDataSize - dataSize; i += dataSize)
    {
      EEPROM.get(i + dataSize, data);
      EEPROM.put(i, data);
    }
    Data invalidData = {-1.0, -1.0}; // Geçersiz veri
    EEPROM.put(totalDataSize - dataSize, invalidData);
    return removedData; // Silinen veriyi döndür
  }
  else
  {
    Serial.println("No valid data to remove.");
    return Data{-1.0, -1.0};
  }
}

// EEPROM'daki geçerli veri sayısını döndür
int getValidDataCount()
{
  // eeprom gereksiz kulanılmasın diye eklendi
  if (EEPROM_OFF)
  {
    return 0;
  }

  Data data;
  int validCount = 0;

  for (int i = 0; i < maxDataCount; i++)
  {
    EEPROM.get(i * dataSize, data);
    if (data.temp >= 0 && data.weight >= 0)
    {
      validCount++;
    }
  }

  return validCount;
}

void start_led_signal()
{
  digitalWrite(GREEN_LED, HIGH);
  digitalWrite(TEMP_WARNING_LED, LOW);
  digitalWrite(CONNECTION_WARNING_LED, LOW);
  delay(400);
  digitalWrite(GREEN_LED, LOW);
  digitalWrite(TEMP_WARNING_LED, HIGH);
  delay(400);
  digitalWrite(TEMP_WARNING_LED, LOW);
  digitalWrite(CONNECTION_WARNING_LED, HIGH);
  delay(400);
  digitalWrite(CONNECTION_WARNING_LED, LOW);
  delay(400);
  digitalWrite(GREEN_LED, HIGH);
  digitalWrite(TEMP_WARNING_LED, HIGH);
  digitalWrite(CONNECTION_WARNING_LED, HIGH);
  delay(400);
  digitalWrite(GREEN_LED, LOW);
  digitalWrite(TEMP_WARNING_LED, LOW);
  digitalWrite(CONNECTION_WARNING_LED, LOW);
}
