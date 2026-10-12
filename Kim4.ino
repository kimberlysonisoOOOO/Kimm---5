#define ENABLE_USER_AUTH
#define ENABLE_DATABASE

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <FirebaseClient.h>

#include <LittleFS.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>

#include <DHT.h>
#include <time.h>


/* =====================================================
   DHT11
   ===================================================== */

#define DHTPIN 4
#define DHTTYPE DHT11

DHT dht(DHTPIN, DHTTYPE);


/* =====================================================
   FIREBASE
   ===================================================== */

// EXACT API KEY FROM YOUR CURRENT FIREBASE WEB CONFIG
#define API_KEY "AIzaSyAMiNzenpJ86iyU7U03qrrDlvFVk0FIDF4"

#define DATABASE_URL "https://my-web-55081-default-rtdb.firebaseio.com/"

// Your Firebase Authentication email
#define USER_EMAIL "kimberly.soniso05@gmail.com"

// IMPORTANT:
// Put your Firebase Authentication password here.
// Do NOT paste the password into chat.
#define USER_PASSWORD "KIMBERLYSONISO2005"


/* =====================================================
   FIREBASE OBJECTS
   ===================================================== */

UserAuth user_auth(
    API_KEY,
    USER_EMAIL,
    USER_PASSWORD,
    3000
);

FirebaseApp app;

WiFiClientSecure ssl_client;

AsyncClientClass aClient(ssl_client);

RealtimeDatabase Database;


/* =====================================================
   WEB SERVER
   ===================================================== */

AsyncWebServer server(80);


/* =====================================================
   WIFI MANAGER
   ===================================================== */

const char *AP_SSID = "ESP-WIFI-MANAGER";

bool wifiManagerMode = false;


/* =====================================================
   SENSOR TIMER
   ===================================================== */

unsigned long lastSensorRead = 0;

const unsigned long SENSOR_INTERVAL = 10000;


/* =====================================================
   FUNCTION DECLARATIONS
   ===================================================== */

void processFirebase(AsyncResult &aResult);

bool connectToSavedWiFi();

void startWiFiManager();

void startMainWebServer();

void setupFirebase();

void sendSensorData();

String readFile(const char *path);

bool writeFile(const char *path, const String &data);

void deleteWiFiFiles();

String getDateString();

String getTimeString();


/* =====================================================
   WIFI MANAGER HTML
   ===================================================== */

const char WIFI_MANAGER_HTML[] PROGMEM = R"rawliteral(

<!DOCTYPE html>

<html lang="en">

<head>

<meta charset="UTF-8">

<meta name="viewport"
      content="width=device-width, initial-scale=1.0">

<title>ESP WiFi Manager</title>

<style>

*{
    box-sizing:border-box;
}

body{

    margin:0;

    padding:20px;

    display:flex;

    justify-content:center;

    align-items:center;

    min-height:100vh;

    font-family:Arial,sans-serif;

    background:#fce7f3;
}

.wifi-card{

    background:white;

    width:100%;

    max-width:500px;

    padding:35px;

    border-radius:20px;

    box-shadow:0 10px 30px rgba(190,24,93,.20);

    border:1px solid #fbcfe8;
}

.wifi-card h1{

    text-align:center;

    color:#be185d;

    margin-bottom:10px;
}

.wifi-card p{

    text-align:center;

    color:#9d174d;

    margin-bottom:25px;
}

.form-group{

    margin-bottom:18px;
}

.form-group label{

    display:block;

    color:#831843;

    font-weight:bold;

    margin-bottom:7px;
}

.form-group input{

    width:100%;

    padding:12px;

    border:1px solid #f9a8d4;

    border-radius:10px;

    font-size:16px;
}

.wifi-button{

    width:100%;

    padding:13px;

    border:none;

    border-radius:10px;

    background:#ec4899;

    color:white;

    font-size:16px;

    font-weight:bold;

    cursor:pointer;

    margin-top:10px;
}

.wifi-button:hover{

    background:#db2777;
}

.info{

    margin-top:20px;

    padding:15px;

    border-radius:12px;

    background:#fff1f7;

    color:#831843;

    font-size:14px;

    line-height:1.6;
}

</style>

</head>


<body>


<div class="wifi-card">


<h1>📶 ESP WiFi Manager</h1>


<p>
Configure the WiFi connection for your ESP32.
</p>


<form action="/" method="POST">


<div class="form-group">

<label for="ssid">
WiFi SSID
</label>

<input
    type="text"
    id="ssid"
    name="ssid"
    placeholder="Enter WiFi name"
    required
>

</div>


<div class="form-group">

<label for="pass">
WiFi Password
</label>

<input
    type="password"
    id="pass"
    name="pass"
    placeholder="Enter WiFi password"
    required
>

</div>


<div class="form-group">

<label for="ip">
IP Address
</label>

<input
    type="text"
    id="ip"
    name="ip"
    placeholder="Optional - leave blank for DHCP"
>

</div>


<div class="form-group">

<label for="gateway">
Gateway Address
</label>

<input
    type="text"
    id="gateway"
    name="gateway"
    placeholder="Optional - leave blank for DHCP"
>

</div>


<button
    type="submit"
    class="wifi-button"
>
Save & Connect
</button>


</form>


<div class="info">

<strong>How it works:</strong>

<br><br>

1. Connect your phone/PC to
<strong>ESP-WIFI-MANAGER</strong>.

<br>

2. Open:
<strong>http://192.168.4.1</strong>

<br>

3. Enter your WiFi SSID and password.

<br>

4. Click Save & Connect.

<br>

5. ESP32 will restart.

<br>

6. ESP32 will connect to your WiFi.

<br>

7. Open the ESP32 IP address to access the website.

</div>


</div>


</body>

</html>

)rawliteral";


/* =====================================================
   READ FILE
   ===================================================== */

String readFile(const char *path)
{
    if (!LittleFS.exists(path))
    {
        return "";
    }

    File file = LittleFS.open(path, "r");

    if (!file)
    {
        return "";
    }

    String data = file.readString();

    file.close();

    data.trim();

    return data;
}


/* =====================================================
   WRITE FILE
   ===================================================== */

bool writeFile(const char *path, const String &data)
{
    File file = LittleFS.open(path, "w");

    if (!file)
    {
        Serial.print("Failed to open file: ");
        Serial.println(path);

        return false;
    }

    file.print(data);

    file.close();

    return true;
}


/* =====================================================
   DELETE WIFI FILES
   ===================================================== */

void deleteWiFiFiles()
{
    LittleFS.remove("/ssid.txt");
    LittleFS.remove("/pass.txt");
    LittleFS.remove("/ip.txt");
    LittleFS.remove("/gateway.txt");

    Serial.println("WiFi settings deleted.");
}


/* =====================================================
   CONNECT SAVED WIFI
   ===================================================== */

bool connectToSavedWiFi()
{
    String ssid = readFile("/ssid.txt");
    String pass = readFile("/pass.txt");
    String ip = readFile("/ip.txt");
    String gateway = readFile("/gateway.txt");


    if (ssid.length() == 0)
    {
        Serial.println();
        Serial.println("No saved WiFi credentials.");

        return false;
    }


    Serial.println();
    Serial.println("=================================");
    Serial.println("      SAVED WIFI FOUND");
    Serial.println("=================================");

    Serial.print("SSID: ");
    Serial.println(ssid);


    WiFi.mode(WIFI_STA);

    delay(500);


    /*
       Optional static IP
    */

    if (
        ip.length() > 0 &&
        gateway.length() > 0
    )
    {
        IPAddress local_IP;
        IPAddress gateway_IP;

        if (
            local_IP.fromString(ip) &&
            gateway_IP.fromString(gateway)
        )
        {
            IPAddress subnet(
                255,
                255,
                255,
                0
            );

            if (
                WiFi.config(
                    local_IP,
                    gateway_IP,
                    subnet
                )
            )
            {
                Serial.println(
                    "Static IP configured."
                );
            }
            else
            {
                Serial.println(
                    "Static IP configuration failed."
                );
            }
        }
    }


    WiFi.begin(
        ssid.c_str(),
        pass.c_str()
    );


    Serial.print(
        "Connecting to WiFi"
    );


    unsigned long startTime =
        millis();


    while (
        WiFi.status() != WL_CONNECTED &&
        millis() - startTime < 20000
    )
    {
        Serial.print(".");

        delay(500);
    }


    Serial.println();


    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.println();
        Serial.println("=================================");
        Serial.println("       WIFI CONNECTED");
        Serial.println("=================================");


        Serial.print("SSID: ");
        Serial.println(WiFi.SSID());


        Serial.print("IP Address: ");
        Serial.println(WiFi.localIP());


        Serial.print("Gateway: ");
        Serial.println(WiFi.gatewayIP());


        Serial.println();

        return true;
    }


    Serial.println(
        "Failed to connect to saved WiFi."
    );


    WiFi.disconnect(true);

    delay(1000);

    return false;
}


/* =====================================================
   START WIFI MANAGER
   ===================================================== */

void startWiFiManager()
{
    wifiManagerMode = true;


    Serial.println();
    Serial.println("=================================");
    Serial.println("       WIFI MANAGER MODE");
    Serial.println("=================================");


    /*
       Start Access Point
    */

    WiFi.mode(WIFI_AP);

    delay(500);


    bool apStarted =
        WiFi.softAP(AP_SSID);


    if (!apStarted)
    {
        Serial.println(
            "ERROR: Failed to start WiFi Manager AP!"
        );
    }


    delay(1000);


    Serial.print("AP SSID: ");
    Serial.println(AP_SSID);


    Serial.print("AP IP Address: ");
    Serial.println(
        WiFi.softAPIP()
    );


    /*
       WiFi Manager page
    */

    server.on(
        "/",
        HTTP_GET,
        [](AsyncWebServerRequest *request)
        {
            request->send(
                200,
                "text/html",
                WIFI_MANAGER_HTML
            );
        }
    );


    /*
       Save WiFi
    */

    server.on(
        "/",
        HTTP_POST,
        [](AsyncWebServerRequest *request)
        {
            String ssid = "";
            String pass = "";
            String ip = "";
            String gateway = "";


            if (
                request->hasParam(
                    "ssid",
                    true
                )
            )
            {
                ssid =
                    request
                    ->getParam(
                        "ssid",
                        true
                    )
                    ->value();
            }


            if (
                request->hasParam(
                    "pass",
                    true
                )
            )
            {
                pass =
                    request
                    ->getParam(
                        "pass",
                        true
                    )
                    ->value();
            }


            if (
                request->hasParam(
                    "ip",
                    true
                )
            )
            {
                ip =
                    request
                    ->getParam(
                        "ip",
                        true
                    )
                    ->value();
            }


            if (
                request->hasParam(
                    "gateway",
                    true
                )
            )
            {
                gateway =
                    request
                    ->getParam(
                        "gateway",
                        true
                    )
                    ->value();
            }


            ssid.trim();
            pass.trim();
            ip.trim();
            gateway.trim();


            if (
                ssid.length() == 0 ||
                pass.length() == 0
            )
            {
                request->send(
                    400,
                    "text/plain",
                    "SSID and password are required."
                );

                return;
            }


            writeFile(
                "/ssid.txt",
                ssid
            );


            writeFile(
                "/pass.txt",
                pass
            );


            writeFile(
                "/ip.txt",
                ip
            );


            writeFile(
                "/gateway.txt",
                gateway
            );


            request->send(
                200,
                "text/html",

                "<html>"
                "<head>"
                "<meta name='viewport' "
                "content='width=device-width, initial-scale=1'>"
                "</head>"

                "<body style='font-family:Arial;"
                "text-align:center;"
                "padding:50px;"
                "background:#fbcfe8;'>"

                "<div style='background:white;"
                "padding:30px;"
                "border-radius:20px;"
                "max-width:500px;"
                "margin:auto;'>"

                "<h1 style='color:#be185d;'>"
                "WiFi Saved!"
                "</h1>"

                "<p>"
                "The ESP32 will restart and "
                "connect to the saved WiFi."
                "</p>"

                "<p>"
                "Please wait..."
                "</p>"

                "</div>"
                "</body>"
                "</html>"
            );


            delay(1500);

            ESP.restart();
        }
    );


    server.begin();


    Serial.println();
    Serial.println("=================================");
    Serial.println(" WIFI MANAGER READY");
    Serial.println("=================================");


    Serial.print(
        "Connect to WiFi: "
    );

    Serial.println(AP_SSID);


    Serial.print(
        "Then open: http://"
    );

    Serial.println(
        WiFi.softAPIP()
    );


    Serial.println();
}


/* =====================================================
   MAIN WEB SERVER
   ===================================================== */

void startMainWebServer()
{
    wifiManagerMode = false;


    /*
       Main website
    */

    server.on(
        "/",
        HTTP_GET,
        [](AsyncWebServerRequest *request)
        {
            if (
                LittleFS.exists(
                    "/index.html"
                )
            )
            {
                request->send(
                    LittleFS,
                    "/index.html",
                    "text/html"
                );
            }
            else
            {
                request->send(
                    404,
                    "text/plain",
                    "index.html not found."
                );
            }
        }
    );


    /*
       Change WiFi
    */

    server.on(
        "/change-wifi",
        HTTP_GET,
        [](AsyncWebServerRequest *request)
        {
            request->send(
                200,
                "text/html",

                "<html>"
                "<head>"
                "<meta name='viewport' "
                "content='width=device-width, initial-scale=1'>"
                "</head>"

                "<body style='font-family:Arial;"
                "text-align:center;"
                "padding:50px;"
                "background:#fbcfe8;'>"

                "<div style='background:white;"
                "padding:30px;"
                "border-radius:20px;"
                "max-width:500px;"
                "margin:auto;'>"

                "<h1 style='color:#be185d;'>"
                "Changing WiFi..."
                "</h1>"

                "<p>"
                "WiFi settings will be cleared."
                "</p>"

                "<p>"
                "The ESP32 will restart."
                "</p>"

                "</div>"
                "</body>"
                "</html>"
            );


            delay(1000);


            deleteWiFiFiles();


            ESP.restart();
        }
    );


    /*
       Static files
    */

    server.serveStatic(
        "/",
        LittleFS,
        "/"
    );


    server.begin();


    Serial.println();
    Serial.println("=================================");
    Serial.println("      MAIN WEB SERVER READY");
    Serial.println("=================================");


    Serial.print(
        "Website: http://"
    );


    Serial.println(
        WiFi.localIP()
    );


    Serial.println();
}


/* =====================================================
   FIREBASE CALLBACK
   ===================================================== */

void processFirebase(AsyncResult &aResult)
{
    if (!aResult.isResult())
    {
        return;
    }


    /*
       EVENT
    */

    if (aResult.isEvent())
    {
        Firebase.printf(
            "Firebase Event - task: %s, msg: %s, code: %d\n",
            aResult.uid().c_str(),
            aResult.eventLog().message().c_str(),
            aResult.eventLog().code()
        );
    }


    /*
       DEBUG
    */

    if (aResult.isDebug())
    {
        Firebase.printf(
            "Firebase Debug - task: %s, msg: %s\n",
            aResult.uid().c_str(),
            aResult.debug().c_str()
        );
    }


    /*
       ERROR
    */

    if (aResult.isError())
    {
        Firebase.printf(
            "Firebase Error - task: %s, msg: %s, code: %d\n",
            aResult.uid().c_str(),
            aResult.error().message().c_str(),
            aResult.error().code()
        );
    }


    /*
       PAYLOAD
    */

    if (aResult.available())
    {
        Firebase.printf(
            "Firebase Payload - task: %s, payload: %s\n",
            aResult.uid().c_str(),
            aResult.c_str()
        );
    }
}


/* =====================================================
   FIREBASE SETUP
   ===================================================== */

void setupFirebase()
{
    Serial.println();
    Serial.println("=================================");
    Serial.println("       FIREBASE SETUP");
    Serial.println("=================================");


    /*
       SSL testing mode
    */

    ssl_client.setInsecure();


    Serial.println(
        "Initializing Firebase..."
    );


    /*
       Firebase initialization
    */

    initializeApp(
        aClient,
        app,
        getAuth(user_auth),
        processFirebase,
        "authTask"
    );


    /*
       Attach Realtime Database
    */

    app.getApp<RealtimeDatabase>(
        Database
    );


    Database.url(
        DATABASE_URL
    );


    Serial.println(
        "Firebase initialization started."
    );


    Serial.println();
}


/* =====================================================
   GET DATE
   ===================================================== */

String getDateString()
{
    struct tm timeinfo;


    if (
        !getLocalTime(
            &timeinfo
        )
    )
    {
        return "1970-01-01";
    }


    char buffer[20];


    strftime(
        buffer,
        sizeof(buffer),
        "%Y-%m-%d",
        &timeinfo
    );


    return String(buffer);
}


/* =====================================================
   GET TIME
   ===================================================== */

String getTimeString()
{
    struct tm timeinfo;


    if (
        !getLocalTime(
            &timeinfo
        )
    )
    {
        return "00:00:00";
    }


    char buffer[20];


    strftime(
        buffer,
        sizeof(buffer),
        "%H:%M:%S",
        &timeinfo
    );


    return String(buffer);
}



/* =====================================================
   SEND SENSOR DATA
   ===================================================== */

void sendSensorData()
{
    // -------------------------------------------------
    // Check Firebase
    // -------------------------------------------------

    if (!app.ready())
    {
        Serial.println();
        Serial.println("Firebase not ready yet...");
        return;
    }


    // -------------------------------------------------
    // READ DHT11
    // IMPORTANT:
    // Read humidity and temperature separately.
    // -------------------------------------------------

    float humidity = dht.readHumidity();
    float temperature = dht.readTemperature();


    // -------------------------------------------------
    // CHECK SENSOR READING
    // -------------------------------------------------

    if (isnan(humidity) || isnan(temperature))
    {
        Serial.println();
        Serial.println("=================================");
        Serial.println("ERROR: Failed to read DHT11");
        Serial.println("=================================");
        Serial.println();

        return;
    }


    // -------------------------------------------------
    // GET DATE AND TIME
    // -------------------------------------------------

    String date = getDateString();
    String time = getTimeString();


    // -------------------------------------------------
    // FIREBASE BASE PATH
    //
    // /ESP32_Data/YYYY-MM-DD/HH:MM:SS
    // -------------------------------------------------

    String basePath =
        "/ESP32_Data/" +
        date +
        "/" +
        time;


    // -------------------------------------------------
    // FIREBASE SENSOR PATHS
    // -------------------------------------------------

    String temperaturePath =
        basePath +
        "/temperature";


    String humidityPath =
        basePath +
        "/humidity";


    // -------------------------------------------------
    // SERIAL MONITOR
    // -------------------------------------------------

    Serial.println();
    Serial.println("=================================");
    Serial.println("       DHT11 SENSOR READING");
    Serial.println("=================================");

    Serial.print("Temperature: ");
    Serial.print(temperature, 1);
    Serial.println(" °C");

    Serial.print("Humidity: ");
    Serial.print(humidity, 1);
    Serial.println(" %");

    Serial.print("Date: ");
    Serial.println(date);

    Serial.print("Time: ");
    Serial.println(time);

    Serial.print("Firebase base path: ");
    Serial.println(basePath);

    Serial.println();


    // -------------------------------------------------
    // DEBUG VALUES BEFORE FIREBASE WRITE
    // -------------------------------------------------

    Serial.println("Firebase values to write:");

    Serial.print("Temperature -> ");
    Serial.println(temperature, 1);

    Serial.print("Humidity    -> ");
    Serial.println(humidity, 1);

    Serial.println();


    // -------------------------------------------------
    // WRITE TEMPERATURE
    // -------------------------------------------------

    Database.set<float>(
        aClient,
        temperaturePath,
        temperature,
        processFirebase,
        "temperatureTask"
    );


    // -------------------------------------------------
    // WRITE HUMIDITY
    // -------------------------------------------------

    Database.set<float>(
        aClient,
        humidityPath,
        humidity,
        processFirebase,
        "humidityTask"
    );


    // -------------------------------------------------
    // WRITE STATUS
    // -------------------------------------------------

    Serial.println("Temperature write task sent.");
    Serial.println("Humidity write task sent.");

    Serial.println();
    Serial.println("Firebase paths:");

    Serial.print("Temperature: ");
    Serial.println(temperaturePath);

    Serial.print("Humidity: ");
    Serial.println(humidityPath);

    Serial.println("=================================");
    Serial.println();
}

/* =====================================================
   SETUP
   ===================================================== */

void setup()
{
    Serial.begin(115200);

    delay(1000);


    Serial.println();
    Serial.println();


    Serial.println(
        "================================="
    );

    Serial.println(
        "   ESP32 DHT11 FIREBASE MONITOR"
    );

    Serial.println(
        "================================="
    );


    /* =================================================
       LITTLEFS
       ================================================= */

    Serial.println();

    Serial.println(
        "Starting LittleFS..."
    );


    if (!LittleFS.begin(true))
    {
        Serial.println(
            "LittleFS mount failed!"
        );


        while (true)
        {
            delay(1000);
        }
    }


    Serial.println(
        "LittleFS ready."
    );


    /* =================================================
       DHT11
       ================================================= */

    dht.begin();


    Serial.println(
        "DHT11 initialized."
    );


    /* =================================================
       WIFI
       ================================================= */

    bool connected =
        connectToSavedWiFi();


    if (!connected)
    {
        /*
           No saved WiFi:
           Start WiFi Manager
        */

        startWiFiManager();

        return;
    }


    /* =================================================
       NTP
       ================================================= */

    Serial.println(
        "Starting NTP time..."
    );


    /*
       Philippines = UTC+8
    */

    configTime(
        8 * 3600,
        0,
        "pool.ntp.org",
        "time.nist.gov",
        "time.google.com"
    );


    Serial.print(
        "Waiting for time"
    );


    struct tm timeinfo;

    int retry = 0;


    while (
        !getLocalTime(
            &timeinfo
        ) &&
        retry < 20
    )
    {
        Serial.print(".");

        delay(500);

        retry++;
    }


    Serial.println();


    if (
        getLocalTime(
            &timeinfo
        )
    )
    {
        Serial.println(
            "Time synchronized."
        );


        Serial.print(
            "Date: "
        );

        Serial.println(
            getDateString()
        );


        Serial.print(
            "Time: "
        );

        Serial.println(
            getTimeString()
        );
    }
    else
    {
        Serial.println(
            "WARNING: Time synchronization failed."
        );
    }


    /* =================================================
       FIREBASE
       ================================================= */

    setupFirebase();


    /* =================================================
       WEB SERVER
       ================================================= */

    startMainWebServer();


    /* =================================================
       READY
       ================================================= */

    Serial.println();

    Serial.println(
        "================================="
    );

    Serial.println(
        "         SYSTEM READY"
    );

    Serial.println(
        "================================="
    );


    Serial.print(
        "Website: http://"
    );


    Serial.println(
        WiFi.localIP()
    );


    Serial.println();
}


/* =====================================================
   LOOP
   ===================================================== */

void loop()
{
    /*
       Firebase async/auth tasks
    */

    if (!wifiManagerMode)
    {
        app.loop();
    }


    /*
       Sensor every 10 seconds
    */

    if (
        !wifiManagerMode &&
        millis() - lastSensorRead >=
        SENSOR_INTERVAL
    )
    {
        lastSensorRead = millis();

        sendSensorData();
    }


    delay(10);
}