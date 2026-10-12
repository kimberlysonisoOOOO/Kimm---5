
/*
  ACTIVITY 5
  ESP32 DevKit V1 + DHT11 + Firebase Realtime Database + microSD

  Firebase path:
  /ESP32_Data/YYYY-MM-DD/HH:MM:SS
      temperature: number
      humidity: number

  SD offline queue:
  /queue/Q00000001.txt
  File content: epoch,temperature,humidity

  Serial Monitor:
  Temperature: 27.6 C | Humidity: 90.0 %

  Important:
  - Set the Wi-Fi and Firebase credentials below.
  - Set Serial Monitor baud rate to 115200.
  - This prototype uses setInsecure(); certificate verification
    is recommended for production projects.
*/

#define ENABLE_USER_AUTH
#define ENABLE_DATABASE

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <FirebaseClient.h>
#include <DHT.h>
#include <SPI.h>
#include <FS.h>
#include <SD.h>
#include <time.h>

// ================= WIFI SETTINGS =================
#define WIFI_SSID       "kim"
#define WIFI_PASSWORD   "12345678"

// ================= FIREBASE SETTINGS =================
#define API_KEY         "AIzaSyCxIAtN-u44ftpWa_K4U6eKr-aUnRdfy6E"
#define USER_EMAIL      "kimberly.soniso05@gmail.com"
#define USER_PASSWORD   "KIMBERLYSONISO2005"

#define DATABASE_URL \
  "https://kimberly-dd5cb-default-rtdb.europe-west1.firebasedatabase.app"

// ================= HARDWARE =================
#define DHT_PIN  4
#define DHT_TYPE DHT11
#define SD_CS    5

// ESP32 VSPI pins:
// SCK=18, MISO=19, MOSI=23, CS=5

// ================= TIMING =================
const unsigned long SAMPLE_INTERVAL_MS = 10000;
const unsigned long WIFI_RETRY_MS = 10000;
const unsigned long WIFI_CONNECT_TIMEOUT_MS = 20000;
const unsigned long NTP_RETRY_MS = 30000;
const unsigned long QUEUE_RETRY_INTERVAL_MS = 3000;
const int MAX_UPLOADS_PER_CYCLE = 3;

// Philippines: UTC+8
const long GMT_OFFSET_SEC = 8L * 60L * 60L;
const int DAYLIGHT_OFFSET_SEC = 0;

// ================= FIREBASE OBJECTS =================
WiFiClientSecure ssl_client;

using AsyncClient = AsyncClientClass;
AsyncClient aClient(ssl_client);

UserAuth user_auth(API_KEY, USER_EMAIL, USER_PASSWORD);
FirebaseApp app;
RealtimeDatabase Database;

// ================= SENSOR / STATE =================
DHT dht(DHT_PIN, DHT_TYPE);

bool sdReady = false;
bool wifiAttemptStarted = false;
bool timeWasSynced = false;

unsigned long lastSampleMs = 0;
unsigned long lastWiFiAttemptMs = 0;
unsigned long wifiAttemptStartMs = 0;
unsigned long lastNtpAttemptMs = 0;
unsigned long lastQueueRetryMs = 0;

uint32_t queueSequence = 1;

// Offline clock fallback: continues while ESP32 has power.
time_t fallbackBaseEpoch = 0;
unsigned long fallbackBaseMillis = 0;

// ====================================================
// TIME HELPERS
// ====================================================

bool isValidEpoch(time_t epoch) {
  return epoch >= 1704067200; // 2024-01-01
}

String dateFromEpoch(time_t epoch) {
  struct tm timeinfo;
  localtime_r(&epoch, &timeinfo);

  char buf[16];
  strftime(buf, sizeof(buf), "%Y-%m-%d", &timeinfo);
  return String(buf);
}

String timeFromEpoch(time_t epoch) {
  struct tm timeinfo;
  localtime_r(&epoch, &timeinfo);

  char buf[16];
  strftime(buf, sizeof(buf), "%H:%M:%S", &timeinfo);
  return String(buf);
}

bool getCurrentEpoch(time_t &epoch) {
  time_t now = time(nullptr);

  if (isValidEpoch(now)) {
    epoch = now;
    return true;
  }

  // Use the last saved time as a fallback while powered.
  if (isValidEpoch(fallbackBaseEpoch)) {
    unsigned long elapsed =
      (millis() - fallbackBaseMillis) / 1000UL;

    epoch = fallbackBaseEpoch + elapsed;
    return true;
  }

  return false;
}

// ====================================================
// SAVE / LOAD LAST TIME
// ====================================================

void saveLastEpoch(time_t epoch) {
  if (!sdReady || !isValidEpoch(epoch)) return;

  File f = SD.open("/last_epoch.txt", FILE_WRITE);
  if (!f) return;

  f.seek(0);
  f.print((long)epoch);
  f.print('\n');
  f.flush();
  f.close();
}

time_t loadLastEpoch() {
  if (!sdReady || !SD.exists("/last_epoch.txt")) return 0;

  File f = SD.open("/last_epoch.txt", FILE_READ);
  if (!f) return 0;

  String s = f.readStringUntil('\n');
  f.close();

  time_t epoch = (time_t)s.toInt();
  return isValidEpoch(epoch) ? epoch : 0;
}

// ====================================================
// SD QUEUE HELPERS
// ====================================================

String queueFileName(uint32_t id) {
  char name[32];

  snprintf(name, sizeof(name), "/queue/Q%08lu.txt",
           (unsigned long)id);

  return String(name);
}

bool isQueueFile(const String &name) {
  return name.startsWith("/queue/Q") &&
         name.endsWith(".txt");
}

void loadQueueSequence() {
  if (!sdReady) return;

  if (SD.exists("/seq.txt")) {
    File f = SD.open("/seq.txt", FILE_READ);

    if (f) {
      uint32_t stored =
        (uint32_t)f.readStringUntil('\n').toInt();

      f.close();

      if (stored > 0) {
        queueSequence = stored;
      }
    }
  }

  // Never overwrite a file that is already pending.
  while (SD.exists(queueFileName(queueSequence))) {
    queueSequence++;
  }
}

void saveQueueSequence() {
  if (!sdReady) return;

  File f = SD.open("/seq.txt", FILE_WRITE);
  if (!f) return;

  f.seek(0);
  f.print((unsigned long)queueSequence);
  f.print('\n');
  f.flush();
  f.close();
}

bool saveReadingToQueue(time_t epoch, float temperature,
                        float humidity) {
  if (!sdReady) return false;

  String filename = queueFileName(queueSequence);

  while (SD.exists(filename)) {
    queueSequence++;
    filename = queueFileName(queueSequence);
  }

  File f = SD.open(filename, FILE_WRITE);
  if (!f) return false;

  // Format: epoch,temperature,humidity
  f.printf("%ld,%.2f,%.2f\n",
           (long)epoch, temperature, humidity);

  f.flush();
  f.close();

  if (!SD.exists(filename)) return false;

  queueSequence++;
  saveQueueSequence();

  return true;
}

bool parseQueueFile(const String &filename, time_t &epoch,
                    float &temperature, float &humidity) {
  File f = SD.open(filename, FILE_READ);
  if (!f) return false;

  String line = f.readStringUntil('\n');
  f.close();
  line.trim();

  int comma1 = line.indexOf(',');
  int comma2 = line.indexOf(',', comma1 + 1);

  if (comma1 < 1 || comma2 < 0) return false;

  epoch = (time_t)line.substring(0, comma1).toInt();
  temperature =
    line.substring(comma1 + 1, comma2).toFloat();
  humidity =
    line.substring(comma2 + 1).toFloat();

  return isValidEpoch(epoch) &&
         !isnan(temperature) &&
         !isnan(humidity);
}

// Normalize the filename returned by the SD library.
String normalizeQueueName(String name) {
  if (name.startsWith("/queue/")) return name;

  while (name.startsWith("/")) {
    name.remove(0, 1);
  }

  if (name.startsWith("queue/")) {
    return "/" + name;
  }

  return "/queue/" + name;
}

String oldestQueueFile() {
  if (!sdReady) return "";

  File dir = SD.open("/queue");
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return "";
  }

  String oldest = "";

  File entry = dir.openNextFile();

  while (entry) {
    String name = normalizeQueueName(String(entry.name()));
    bool isFile = !entry.isDirectory();

    entry.close();

    if (isFile && isQueueFile(name)) {
      if (oldest == "" || name < oldest) {
        oldest = name;
      }
    }

    entry = dir.openNextFile();
  }

  dir.close();
  return oldest;
}

int countPendingFiles() {
  if (!sdReady) return 0;

  int count = 0;

  File dir = SD.open("/queue");
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return 0;
  }

  File entry = dir.openNextFile();

  while (entry) {
    String name = normalizeQueueName(String(entry.name()));
    bool isFile = !entry.isDirectory();

    entry.close();

    if (isFile && isQueueFile(name)) {
      count++;
    }

    entry = dir.openNextFile();
  }

  dir.close();
  return count;
}

// ====================================================
// FIREBASE UPLOAD
// ====================================================

// Uses the synchronous Database.set() API.
// A true return value indicates that the operation succeeded.
bool uploadReading(time_t epoch, float temperature,
                   float humidity) {
  if (WiFi.status() != WL_CONNECTED || !app.ready()) {
    return false;
  }

  String path = "/ESP32_Data/" + dateFromEpoch(epoch) +
                "/" + timeFromEpoch(epoch);

  // Preserve the exact fields expected by the website.
  String json = "{\"temperature\":" +
                String(temperature, 2) +
                ",\"humidity\":" +
                String(humidity, 2) + "}";

  bool success = Database.set<object_t>(
    aClient, path, object_t(json)
  );

  return success;
}

void uploadPendingQueue() {
  if (!sdReady ||
      WiFi.status() != WL_CONNECTED ||
      !app.ready()) {
    return;
  }

  // Do not hammer Firebase if the network is failing.
  if (millis() - lastQueueRetryMs <
      QUEUE_RETRY_INTERVAL_MS) {
    return;
  }

  lastQueueRetryMs = millis();

  for (int i = 0; i < MAX_UPLOADS_PER_CYCLE; i++) {
    String filename = oldestQueueFile();

    if (filename == "") return;

    time_t epoch;
    float temperature, humidity;

    // Keep invalid files for manual inspection.
    if (!parseQueueFile(filename, epoch,
                        temperature, humidity)) {
      return;
    }

    if (!uploadReading(epoch, temperature, humidity)) {
      // Upload failed: keep the file and retry later.
      return;
    }

    // Delete only after confirmed successful upload.
    if (!SD.remove(filename)) {
      return;
    }
  }
}

// ====================================================
// WIFI
// ====================================================

void startWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  wifiAttemptStarted = true;
  wifiAttemptStartMs = millis();
  lastWiFiAttemptMs = millis();
}

void maintainWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    wifiAttemptStarted = false;
    return;
  }

  // Avoid calling WiFi.begin repeatedly while connecting.
  if (wifiAttemptStarted &&
      millis() - wifiAttemptStartMs <
      WIFI_CONNECT_TIMEOUT_MS) {
    return;
  }

  if (millis() - lastWiFiAttemptMs >= WIFI_RETRY_MS) {
    lastWiFiAttemptMs = millis();

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    wifiAttemptStarted = true;
    wifiAttemptStartMs = millis();
  }
}

// ====================================================
// NTP TIME SYNC
// ====================================================

void trySyncNTP() {
  if (WiFi.status() != WL_CONNECTED) return;

  // Allow the first NTP attempt immediately.
  if (lastNtpAttemptMs != 0 &&
      millis() - lastNtpAttemptMs < NTP_RETRY_MS) {
    return;
  }

  lastNtpAttemptMs = millis();

  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC,
             "pool.ntp.org", "time.google.com");

  struct tm timeinfo;

  if (getLocalTime(&timeinfo, 5000)) {
    time_t now = time(nullptr);

    if (isValidEpoch(now)) {
      timeWasSynced = true;
      fallbackBaseEpoch = now;
      fallbackBaseMillis = millis();

      saveLastEpoch(now);
    }
  }
}

// ====================================================
// DHT11 SAMPLING
// ====================================================

void sampleSensor() {
  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();

  if (isnan(temperature) || isnan(humidity)) {
    return;
  }

  // This is the only normal Serial Monitor output.
  Serial.printf("Temperature: %.1f C | Humidity: %.1f %%\n",
                temperature, humidity);

  time_t epoch;

  if (!getCurrentEpoch(epoch)) {
    // No valid time yet: do not create a wrong Firebase path.
    return;
  }

  int pending = countPendingFiles();

  // If older readings are waiting, preserve order by queuing
  // the new reading behind them.
  if (pending > 0) {
    if (saveReadingToQueue(epoch, temperature, humidity)) {
      saveLastEpoch(epoch);
    }
    return;
  }

  // Online with no backlog: upload directly.
  if (WiFi.status() == WL_CONNECTED && app.ready()) {
    if (uploadReading(epoch, temperature, humidity)) {
      saveLastEpoch(epoch);
      return;
    }
  }

  // Offline, Firebase not ready, or upload failed:
  // save this reading for a later retry.
  if (saveReadingToQueue(epoch, temperature, humidity)) {
    saveLastEpoch(epoch);
  }
}

// ====================================================
// SETUP
// ====================================================

void setup() {
  Serial.begin(115200);
  delay(500);

  dht.begin();

  // Initialize SD SPI.
  SPI.begin(18, 19, 23, SD_CS);

  if (SD.begin(SD_CS, SPI)) {
    sdReady = true;

    if (!SD.exists("/queue")) {
      if (!SD.mkdir("/queue")) {
        sdReady = false;
      }
    }

    if (sdReady) {
      loadQueueSequence();

      time_t lastEpoch = loadLastEpoch();

      if (isValidEpoch(lastEpoch)) {
        fallbackBaseEpoch = lastEpoch;
        fallbackBaseMillis = millis();
      }
    }
  }

  // Start Wi-Fi.
  startWiFi();

  // Prototype setting only. Verify server certificates in production.
  ssl_client.setInsecure();

  // Initialize Firebase authentication and database.
  initializeApp(aClient, app, getAuth(user_auth));

  app.getApp<RealtimeDatabase>(Database);
  Database.url(DATABASE_URL);
}

// ====================================================
// LOOP
// ====================================================

void loop() {
  // Required by FirebaseClient for authentication and tasks.
  app.loop();

  maintainWiFi();
  trySyncNTP();

  // Refresh the fallback base when the system clock is valid.
  time_t now = time(nullptr);

  if (isValidEpoch(now) && !timeWasSynced) {
    timeWasSynced = true;
    fallbackBaseEpoch = now;
    fallbackBaseMillis = millis();
    saveLastEpoch(now);
  }

  if (millis() - lastSampleMs >= SAMPLE_INTERVAL_MS) {
    lastSampleMs = millis();
    sampleSensor();
  }

  // Upload saved offline records whenever Firebase is ready.
  uploadPendingQueue();

  delay(5);
}
