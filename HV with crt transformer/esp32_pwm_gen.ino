#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>

// ============== CONFIGURATION ==============
const char* apSSID = "Plasma_Speaker"; // Hotspot Name
const byte DNS_PORT = 53;

const int pwmPin = 8;        // GPIO 8 for PWM output
const int ledcResolution = 10; // 10-bit (0-1023) for finer duty control
const int maxDutyValue = (1 << ledcResolution) - 1; // 1023

// ============== VARIABLES ==============
DNSServer dnsServer;
WebServer server(80);

int currentDuty = 99;       // Actual physical duty cycle (50-99%)
int currentFreq = 15000;    // Default 15kHz
bool isPlayingSong = false;
bool isPlayingTone = false;
unsigned long songStartTime = 0;
int currentNoteIndex = 0;
unsigned long noteStartTime = 0;

// ============== MUSICAL NOTES (Hz) ==============
#define REST     0
#define NOTE_AS4 466
#define NOTE_BB4 466
#define NOTE_C4  262
#define NOTE_D4  294
#define NOTE_E4  330
#define NOTE_F4  349
#define NOTE_G4  392
#define NOTE_A4  440
#define NOTE_B4  494
#define NOTE_C5  523
#define NOTE_D5  587
#define NOTE_E5  659
#define NOTE_F5  698
#define NOTE_G5  784
#define NOTE_A5  880
#define NOTE_B5  988

// ============== SONGS ==============
// Happy Birthday melody
const int happyBday_melody[] = {
  NOTE_C4, NOTE_C4, NOTE_D4, NOTE_C4, NOTE_F4, NOTE_E4, REST,
  NOTE_C4, NOTE_C4, NOTE_D4, NOTE_C4, NOTE_G4, NOTE_F4, REST,
  NOTE_C4, NOTE_C4, NOTE_C5, NOTE_A4, NOTE_F4, NOTE_E4, NOTE_D4, REST,
  NOTE_AS4, NOTE_AS4, NOTE_A4, NOTE_F4, NOTE_G4, NOTE_F4, REST
};

const int happyBday_durations[] = {
  8, 16, 4, 4, 4, 8, 4,
  8, 16, 4, 4, 4, 8, 4,
  8, 16, 4, 4, 4, 4, 8, 4,
  8, 16, 4, 4, 4, 8, 4
};

// Tetris Theme
const int tetris_melody[] = {
  NOTE_E5, NOTE_B4, NOTE_C5, NOTE_D5, NOTE_C5, NOTE_B4, NOTE_A4, NOTE_A4,
  NOTE_C5, NOTE_E5, NOTE_D5, NOTE_C5, NOTE_B4, NOTE_C5, NOTE_D5, NOTE_E5,
  NOTE_C5, NOTE_A4, NOTE_A4, REST, NOTE_D5, NOTE_F5, NOTE_A5, NOTE_G5,
  NOTE_F5, NOTE_E5, NOTE_C5, NOTE_E5, NOTE_D5, NOTE_C5, NOTE_B4, NOTE_B4,
  NOTE_C5, NOTE_D5, NOTE_E5, NOTE_C5, NOTE_A4, NOTE_A4, REST, REST
};

const int tetris_durations[] = {
  8, 8, 8, 8, 8, 8, 8, 8,
  8, 8, 8, 8, 8, 8, 8, 8,
  8, 8, 4, 4, 8, 8, 8, 8,
  8, 8, 8, 8, 8, 8, 8, 8,
  8, 8, 8, 8, 8, 4, 4, 4
};

// Star Wars Imperial March
const int imperial_melody[] = {
  NOTE_E4, NOTE_E4, NOTE_E4, NOTE_C4, NOTE_E4, NOTE_G4, NOTE_G4, REST,
  NOTE_C4, NOTE_G4, NOTE_E4, NOTE_G4, NOTE_C5, NOTE_G4, NOTE_E4, NOTE_G4,
  NOTE_C5, NOTE_G4, NOTE_E4, NOTE_G4, NOTE_C5, NOTE_B4, NOTE_G4, NOTE_E4,
  NOTE_D4, NOTE_C4, NOTE_B4, NOTE_C4, REST
};

const int imperial_durations[] = {
  8, 8, 8, 4, 8, 4, 8, 4,
  8, 8, 8, 8, 8, 8, 8, 8,
  8, 8, 8, 8, 8, 8, 8, 8,
  8, 8, 8, 4, 4
};

// Current song pointers
const int* currentMelody = happyBday_melody;
const int* currentDurations = happyBday_durations;
int currentSongLength = sizeof(happyBday_melody) / sizeof(happyBday_melody[0]);

// ============== PWM FUNCTIONS (ESP32-C3 Core 3.x API) ==============
int dutyToValue(int dutyPercent) {
  return map(dutyPercent, 0, 100, 0, maxDutyValue);
}

void initPWM() {
  ledcAttach(pwmPin, currentFreq, ledcResolution);
  ledcWrite(pwmPin, dutyToValue(currentDuty));
}

void updatePWM() {
  // Use ledcChangeFrequency so the slider is perfectly smooth and doesn't detach/glitch
  ledcChangeFrequency(pwmPin, currentFreq, ledcResolution);
  ledcWrite(pwmPin, dutyToValue(currentDuty));
}

void setTone(int freq, int dutyPercent) {
  if (freq > 0) {
    ledcChangeFrequency(pwmPin, freq, ledcResolution);
    ledcWrite(pwmPin, dutyToValue(dutyPercent));
  } else {
    ledcWrite(pwmPin, 0);  // REST - no output (0 physical duty, 100% active = Safe off)
  }
}

// ============== SONG FUNCTIONS ==============
void startSong(int songNum) {
  isPlayingSong = true;
  isPlayingTone = false;
  songStartTime = millis();
  currentNoteIndex = 0;
  noteStartTime = millis();
  
  switch (songNum) {
    case 1:
      currentMelody = happyBday_melody;
      currentDurations = happyBday_durations;
      currentSongLength = sizeof(happyBday_melody) / sizeof(happyBday_melody[0]);
      break;
    case 2:
      currentMelody = tetris_melody;
      currentDurations = tetris_durations;
      currentSongLength = sizeof(tetris_melody) / sizeof(tetris_melody[0]);
      break;
    case 3:
      currentMelody = imperial_melody;
      currentDurations = imperial_durations;
      currentSongLength = sizeof(imperial_melody) / sizeof(imperial_melody[0]);
      break;
  }
  
  // Play first note using CURRENT duty cycle for flyback safety
  setTone(currentMelody[0], currentDuty);
}

void stopPlayback() {
  isPlayingSong = false;
  isPlayingTone = false;
  updatePWM(); // Restore previous freq/duty
}

void handleSongPlayback() {
  if (!isPlayingSong) return;
  
  unsigned long now = millis();
  
  // Calculate current note duration
  int noteDuration = 1000 / currentDurations[currentNoteIndex];
  unsigned long noteEndTime = noteStartTime + noteDuration;
  
  if (now >= noteEndTime) {
    currentNoteIndex++;
    
    if (currentNoteIndex >= currentSongLength) {
      stopPlayback();
      return;
    }
    
    noteStartTime = noteEndTime;
    // Play next note using CURRENT duty cycle for flyback safety
    setTone(currentMelody[currentNoteIndex], currentDuty);
  }
}

// ============== WEB SERVER (HTML/JS) ==============
const char webpage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Plasma Speaker Controller</title>
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body { 
      font-family: 'Segoe UI', Arial, sans-serif; 
      background: linear-gradient(135deg, #0f0c29, #302b63, #24243e);
      min-height: 100vh; 
      color: #fff; 
      padding: 20px;
    }
    .container { max-width: 480px; margin: 0 auto; }
    h1 { 
      text-align: center; 
      color: #00d4ff; 
      margin-bottom: 30px;
      text-shadow: 0 0 20px rgba(0, 212, 255, 0.5);
      font-size: 1.8rem;
    }
    .card { 
      background: rgba(255,255,255,0.08); 
      backdrop-filter: blur(10px);
      border: 1px solid rgba(255,255,255,0.1);
      border-radius: 16px; 
      padding: 24px; 
      margin-bottom: 20px;
      transition: transform 0.2s;
    }
    .card:hover { transform: translateY(-2px); }
    .card h2 { 
      color: #00d4ff; 
      margin-bottom: 16px;
      font-size: 1.1rem;
      display: flex;
      align-items: center;
      gap: 10px;
    }
    .card h2::before { content: '⚡'; font-size: 1.2rem; }
    .slider-group { margin-bottom: 16px; }
    .slider-label {
      display: flex;
      justify-content: space-between;
      margin-bottom: 8px;
      font-size: 0.95rem;
    }
    .slider-value { 
      color: #00d4ff; 
      font-weight: bold;
      font-family: 'Courier New', monospace;
    }
    input[type="range"] {
      -webkit-appearance: none;
      width: 100%;
      height: 8px;
      border-radius: 4px;
      background: linear-gradient(to right, #00d4ff, #7b2ff7);
      outline: none;
      cursor: pointer;
    }
    input[type="range"]::-webkit-slider-thumb {
      -webkit-appearance: none;
      width: 24px;
      height: 24px;
      border-radius: 50%;
      background: #fff;
      box-shadow: 0 0 10px rgba(0, 212, 255, 0.5);
      cursor: pointer;
    }
    .btn-grid {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 12px;
      margin-top: 16px;
    }
    .btn { 
      padding: 14px 16px; 
      border: none; 
      border-radius: 10px; 
      font-size: 0.9rem;
      font-weight: 600;
      cursor: pointer;
      transition: all 0.2s;
      text-transform: uppercase;
      letter-spacing: 0.5px;
    }
    .btn:hover { transform: scale(1.02); }
    .btn:active { transform: scale(0.98); }
    .btn-song { background: linear-gradient(135deg, #667eea, #764ba2); color: white; }
    .btn-tone { background: linear-gradient(135deg, #f093fb, #f5576c); color: white; }
    .btn-stop { 
      background: linear-gradient(135deg, #434343, #000000); 
      color: #ff4444;
      grid-column: 1 / -1;
    }
    .status-bar {
      text-align: center;
      padding: 12px;
      border-radius: 8px;
      margin-top: 16px;
      font-size: 0.9rem;
      font-weight: 600;
      transition: all 0.3s;
    }
    .status-pwm {
      background: rgba(0, 212, 255, 0.15);
      color: #00d4ff;
      border: 1px solid rgba(0, 212, 255, 0.3);
    }
    .status-playing {
      background: rgba(0, 255, 100, 0.15);
      color: #00ff64;
      border: 1px solid rgba(0, 255, 100, 0.3);
      animation: pulse 1s infinite;
    }
    @keyframes pulse { 0%, 100% { opacity: 1; } 50% { opacity: 0.7; } }
    .info {
      text-align: center;
      margin-top: 20px;
      font-size: 0.8rem;
      color: rgba(255,255,255,0.4);
    }
    .visual-bar {
      height: 4px;
      background: rgba(255,255,255,0.1);
      border-radius: 2px;
      margin-top: 12px;
      overflow: hidden;
    }
    .visual-fill {
      height: 100%;
      background: linear-gradient(to right, #00d4ff, #7b2ff7);
      border-radius: 2px;
      transition: width 0.1s;
    }
  </style>
</head>
<body>
  <div class="container">
    <h1>🎵 Plasma Speaker Controller</h1>
    
    <div class="card">
      <h2>Flyback Duty Cycle</h2>
      <div class="slider-group">
        <div class="slider-label">
          <span>Active (Negative) Duty</span>
          <span class="slider-value" id="negDutyVal">-1%</span>
        </div>
        <input type="range" id="dutySlider" min="-50" max="-1" value="-1" step="1">
      </div>
      <div class="slider-label">
        <span>Physical PWM Duty</span>
        <span class="slider-value" id="actDutyVal">99%</span>
      </div>
      <div class="visual-bar">
        <div class="visual-fill" id="dutyBar" style="width: 99%"></div>
      </div>
    </div>
    
    <div class="card">
      <h2>Arc Frequency</h2>
      <div class="slider-group">
        <div class="slider-label">
          <span>PWM Frequency</span>
          <span class="slider-value" id="freqVal">15.0 kHz</span>
        </div>
        <input type="range" id="freqSlider" min="1000" max="30000" value="15000" step="100">
      </div>
      <div class="visual-bar">
        <div class="visual-fill" id="freqBar" style="width: 48%"></div>
      </div>
    </div>
    
    <div class="card">
      <h2>Music Player</h2>
      <div class="btn-grid">
        <button class="btn btn-song" onclick="playSong(1)">🎂 Happy Birthday</button>
        <button class="btn btn-song" onclick="playSong(2)">🎮 Tetris</button>
        <button class="btn btn-song" onclick="playSong(3)">⚔️ Imperial March</button>
        <button class="btn btn-tone" onclick="playTone(440)">🎵 A4 Tone</button>
        <button class="btn btn-tone" onclick="playTone(1000)">🔊 1kHz Tone</button>
        <button class="btn btn-stop" onclick="stopAll()">⬛ STOP - Return to Slider</button>
      </div>
      <div class="status-bar status-pwm" id="statusBar">
        ⚡ Static Arc Mode Active
      </div>
    </div>
    
    <div class="info">
      WARNING: High Voltage! Observe appropriate safety measures.
    </div>
  </div>

  <script>
    const dutySlider = document.getElementById('dutySlider');
    const freqSlider = document.getElementById('freqSlider');
    const negDutyVal = document.getElementById('negDutyVal');
    const actDutyVal = document.getElementById('actDutyVal');
    const freqVal = document.getElementById('freqVal');
    const dutyBar = document.getElementById('dutyBar');
    const freqBar = document.getElementById('freqBar');
    const statusBar = document.getElementById('statusBar');
    
    let updateTimeout = null;
    let isPlaying = false;
    
    function debounceUpdate() {
      if (updateTimeout) clearTimeout(updateTimeout);
      updateTimeout = setTimeout(() => {
        if (!isPlaying) {
          sendPWMUpdate();
        }
      }, 10);
    }
    
    dutySlider.addEventListener('input', function() {
      const val = parseInt(this.value);
      const actual = 100 + val;
      negDutyVal.textContent = val + '%';
      actDutyVal.textContent = actual + '%';
      dutyBar.style.width = actual + '%';
      debounceUpdate();
    });
    
    freqSlider.addEventListener('input', function() {
      const val = parseInt(this.value);
      freqVal.textContent = (val / 1000).toFixed(1) + ' kHz';
      freqBar.style.width = ((val - 1000) / 29000 * 100) + '%';
      debounceUpdate();
    });
    
    function sendPWMUpdate() {
      fetch('/pwm?duty=' + dutySlider.value + '&freq=' + freqSlider.value)
        .catch(err => console.log('Update failed'));
    }
    
    function playSong(num) {
      isPlaying = true;
      statusBar.textContent = '🎵 Playing Music...';
      statusBar.className = 'status-bar status-playing';
      fetch('/song?num=' + num).catch(err => console.log('Song failed'));
    }
    
    function playTone(freq) {
      isPlaying = true;
      statusBar.textContent = '🔊 Playing Tone: ' + freq + ' Hz';
      statusBar.className = 'status-bar status-playing';
      fetch('/tone?freq=' + freq).catch(err => console.log('Tone failed'));
    }
    
    function stopAll() {
      isPlaying = false;
      statusBar.textContent = '⚡ Static Arc Mode Active';
      statusBar.className = 'status-bar status-pwm';
      fetch('/stop').then(() => {
        // Send a quick update to restore sliders after stopping
        sendPWMUpdate();
      });
    }
  </script>
</body>
</html>
)rawliteral";

// ============== WEB HANDLERS ==============
void handleRoot() {
  server.send_P(200, "text/html", webpage);
}

void handleCaptivePortal() {
  // Redirect to root IP to trigger device OS captive portal login screens
  server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString(), true);
  server.send(302, "text/plain", "");
}

void handlePWM() {
  if (server.hasArg("duty")) {
    int negDuty = server.arg("duty").toInt();
    currentDuty = 100 + negDuty;  // Convert -1 to 99, -50 to 50
    currentDuty = constrain(currentDuty, 0, 100);
  }
  if (server.hasArg("freq")) {
    currentFreq = server.arg("freq").toInt();
    currentFreq = constrain(currentFreq, 1000, 30000);
  }
  
  if (!isPlayingSong && !isPlayingTone) {
    updatePWM();
  }
  
  server.send(200, "text/plain", "OK");
}

void handleSong() {
  if (server.hasArg("num")) {
    startSong(server.arg("num").toInt());
  }
  server.send(200, "text/plain", "OK");
}

void handleTone() {
  if (server.hasArg("freq")) {
    int toneFreq = server.arg("freq").toInt();
    isPlayingTone = true;
    isPlayingSong = false;
    setTone(toneFreq, currentDuty); // Use current safe duty cycle
  }
  server.send(200, "text/plain", "OK");
}

void handleStop() {
  stopPlayback();
  server.send(200, "text/plain", "OK");
}

// ============== SETUP & LOOP ==============
void setup() {
  Serial.begin(115200);
  
  // Initialize PWM safely
  initPWM();

  // Setup Wi-Fi Access Point (Captive Portal)
  Serial.println("Configuring Access Point...");
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apSSID); // No password makes it an open captive portal
  
  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);

  // Intercept all DNS requests and route to the ESP32 IP
  dnsServer.start(DNS_PORT, "*", IP);

  // Setup Web Server Routes
  server.on("/", handleRoot);
  server.on("/pwm", handlePWM);
  server.on("/song", handleSong);
  server.on("/tone", handleTone);
  server.on("/stop", handleStop);

  // Apple & Android Captive Portal Routes
  server.on("/generate_204", handleCaptivePortal); 
  server.on("/hotspot-detect.html", handleCaptivePortal);
  server.onNotFound(handleCaptivePortal);
  
  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  // Process Captive Portal DNS and UI Web requests
  dnsServer.processNextRequest();
  server.handleClient();
  
  // Process Audio
  handleSongPlayback();
}