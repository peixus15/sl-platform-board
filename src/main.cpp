#include <Arduino.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#include <WiFi.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

#include "Config.h"
#include "SlTransportAdapter.h"
#include "StockholmTheme.h"

constexpr uint16_t kPanelWidth = 64;
constexpr uint16_t kPanelHeight = 32;
constexpr uint8_t kPanelChain = 2;
constexpr uint32_t kPollIntervalMs = 25000;
constexpr uint32_t kReconnectIntervalMs = 30000;
// Departures older than this are dropped so the board never shows long-gone trains.
constexpr uint32_t kStaleAfterMs = 180000;
constexpr char kAccessPointName[] = "SL-Board-Setup";
constexpr char kAccessPointPassword[] = "SLBoard9116";

AppConfig config;
WebServer server(80);
SlTransportAdapter slAdapter;
StockholmTheme stockholmTheme;
SlStopData stopData;
MatrixPanel_I2S_DMA* matrix = nullptr;
String lastError;
bool lastFetchOk = false;
bool displayDirty = true;
bool fetchRequested = false;
uint32_t lastFetchMs = 0;
uint32_t lastSuccessMs = 0;
uint32_t lastReconnectMs = 0;
bool wasWifiConnected = false;

const char kSettingsPage[] PROGMEM = R"HTML(
<!doctype html><html lang="sv"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>SL Perrongskylt</title>
<style>
:root{color-scheme:dark;--bg:#11130f;--panel:#1b1e18;--edge:#343a2c;--ink:#e5e7dd;--muted:#9a9e8e;--amber:#ff9a24;--green:#9dc36b}*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:16px/1.45 "Trebuchet MS",sans-serif}main{max-width:760px;margin:0 auto;padding:28px 18px 44px}.top{display:flex;justify-content:space-between;align-items:end;border-bottom:1px solid var(--edge);padding-bottom:18px;margin-bottom:20px}.eyebrow{color:var(--amber);font-size:12px;font-weight:700;text-transform:uppercase;letter-spacing:1px}h1{font-size:26px;margin:4px 0 0}.status{font-size:13px;color:var(--muted);text-align:right}.status strong{color:var(--green)}form{display:grid;grid-template-columns:1fr 1fr;gap:14px 18px}.field{display:flex;flex-direction:column;gap:6px}.wide{grid-column:1/-1}label{font-size:13px;color:var(--muted)}input,select{width:100%;min-height:42px;border:1px solid var(--edge);border-radius:3px;background:#10120f;color:var(--ink);padding:8px 10px;font:inherit}input[type=range]{padding:0;accent-color:var(--amber)}.help{font-size:12px;color:var(--muted)}button{margin-top:6px;min-height:44px;border:0;border-radius:3px;background:var(--amber);color:#17130b;font-weight:700;font-size:15px;cursor:pointer}.sample{font-family:monospace;color:var(--amber);background:#050605;border:1px solid #303328;padding:12px;margin-top:22px;white-space:pre-line}.note{color:var(--muted);font-size:12px;margin-top:14px}@media(max-width:560px){form{grid-template-columns:1fr}.wide{grid-column:auto}.top{align-items:start;gap:10px;flex-direction:column}.status{text-align:left}}
h2{font-size:15px;color:var(--amber);text-transform:uppercase;letter-spacing:1px;margin:30px 0 12px}.msg{font-size:13px;color:var(--green);min-height:1.4em;align-self:center}.msg.err{color:#e0735a}.warn{color:#e0b05a}
.side{display:flex;flex-direction:column;gap:8px;align-items:end}#lang{width:auto;min-height:34px;padding:4px 8px;font-size:13px}@media(max-width:560px){.side{align-items:start}}
</style></head><body><main><header class="top"><div><div class="eyebrow">Stockholm · Tunnelbana</div><h1 data-t="title">Perrongskylt</h1></div><div class="side"><select id="lang" aria-label="Språk / Language / Idioma"><option value="sv">Svenska</option><option value="en">English</option><option value="es">Español</option></select><div class="status" id="status"></div></div></header>
<h2 data-t="hDisplay"></h2>
<form id="display"><div class="field wide"><label for="site" data-t="station"></label><select id="site" name="site"></select><span class="help" data-t="stationHelp"></span></div>
<div class="field"><label for="platform" data-t="platform"></label><select id="platform" name="platform"></select><span class="help" data-t="platformHelp"></span><span class="help warn" id="platformWarn"></span></div>
<div class="field"><label for="line" data-t="line"></label><select id="line" name="line"></select><span class="help" data-t="lineHelp"></span></div>
<div class="field"><label for="rows" data-t="rows"></label><select id="rows" name="rows"><option value="1" data-t="row1"></option><option value="2" data-t="row2" selected></option></select><span class="help" data-t="rowsHelp"></span></div>
<div class="field"><label for="brightness"><span data-t="brightness"></span> · <output id="brightnessValue">64</output>/255</label><input id="brightness" name="brightness" type="range" min="1" max="255" value="64" oninput="brightnessValue.value=this.value"><span class="help" data-t="brightnessHelp"></span></div>
<div class="field"><button type="submit" data-t="show"></button></div><div class="msg" id="displayMsg"></div></form>
<div class="sample">17 SKARPNÄCK             NU
18 FARSTA STRAND       2 MIN</div>
<h2>Wi-Fi</h2>
<form method="post" action="/config"><div class="field wide"><label for="ssid" data-t="ssid"></label><input id="ssid" name="ssid" autocomplete="off" required><span class="help" data-t="ssidHelp"></span></div><div class="field wide"><label for="wifiPassword" data-t="pass"></label><input id="wifiPassword" name="wifiPassword" type="password" autocomplete="new-password" data-tp="passPh"></div>
<div class="field wide"><button type="submit" data-t="saveWifi"></button></div></form>
<p class="note" data-t="note"></p></main>
<script>
const STATIONS='9110|Abrahamsberg;9300|Akalla;9108|Åkeshov;9282|Alby;9112|Alvik;9107|Ängbyplan;9312|Arenastaden;9293|Aspudden;9291|Axelsberg;9141|Bagarmossen;9163|Bandhagen;9308|Barkarbystaden;9202|Bergshamra;9143|Björkhagen;9105|Blackeberg;9187|Blåsut;9289|Bredäng;9109|Brommaplan;9201|Danderyds sjukhus;9324|Duvbo;9167|Enskede gård;9181|Farsta;9180|Farsta strand;9283|Fittja;9115|Fridhemsplan;9260|Fruängen;9193|Gamla stan;9221|Gärdet;9168|Globen;9183|Gubbängen;9189|Gullmarsplan;9311|Hagalund;9310|Hagastaden;9262|Hägerstensåsen;9160|Hagsätra;9303|Hallonbergen;9281|Hallunda;9144|Hammarbyhöjden;9101|Hässelby gård;9100|Hässelby strand;9320|Hjulsta;9162|Högdalen;9182|Hökarängen;9295|Hornstull;9119|Hötorget;9301|Husby;9327|Huvudsta;9106|Islandstorget;9102|Johannelund;9222|Karlaplan;9142|Kärrtorp;9302|Kista;9113|Kristineberg;9340|Kungsträdgården;9294|Liljeholmen;9290|Mälarhöjden;9297|Mariatorget;9284|Masmo;9191|Medborgarplatsen;9264|Midsommarkransen;9200|Mörby centrum;9304|Näckrosen;9280|Norsborg;9117|Odenplan;9292|Örnsberg;9206|Östermalmstorg;9104|Råcksta;9309|Rådhuset;9118|Rådmansgatan;9161|Rågsved;9322|Rinkeby;9323|Rissne;9220|Ropsten;9186|Sandsborg;9116|Sankt Eriksplan;9288|Sätra;9190|Skanstull;9287|Skärholmen;9188|Skärmarbrink;9140|Skarpnäck;9185|Skogskyrkogården;9192|Slussen;9166|Sockenplan;9305|Solna centrum;9326|Solna strand;9205|Stadion;9307|Stadshagen;9111|Stora mossen;9164|Stureby;9325|Sundbyberg;9165|Svedmyra;9184|Tallkrogen;9001|T-Centralen;9204|Tekniska högskolan;9263|Telefonplan;9321|Tensta;9114|Thorildsplan;9203|Universitetet;9103|Vällingby;9286|Vårberg;9285|Vårby gård;9261|Västertorp;9306|Västra skogen;9296|Zinkensdamm';
const T={
sv:{title:'Perrongskylt',connecting:'Ansluter…',hDisplay:'Visning',station:'Station',stationHelp:'Stationen vars avgångar visas på skylten.',platform:'Spår och riktning',platformHelp:'Varje spår är en riktning. Listan visar vart tågen från spåret går.',line:'Linje',lineHelp:'Visa alla linjer som stannar vid spåret, eller bara en.',rows:'Synliga avgångar',rowsHelp:'Hur många tåg skylten visar samtidigt.',row1:'1 rad',row2:'2 rader',brightness:'Ljusstyrka',brightnessHelp:'Panelens ljusstyrka. Högre värde drar mer ström.',show:'Visa på skylten',ssid:'Wi-Fi-nätverk',ssidHelp:'Nätverket som skylten använder för att hämta avgångar från SL.',ssidPh:'Nätverksnamn',pass:'Wi-Fi-lösenord',passPh:'Lämna tomt för att behålla sparat lösenord',saveWifi:'Spara Wi-Fi och starta om',note:'Inställningsnätverk: SL-Board-Setup · lösenord: SLBoard9116 · öppna http://192.168.4.1',allLines:'Alla linjer',lineN:'Linje',platformN:'Spår',toward:'mot',loading:'Laddar spår…',noPlatforms:'Inga tunnelbaneavgångar just nu – välj spår manuellt.',platformsError:'Kunde inte hämta spår: ',noDevice:'Enheten svarar inte.',saving:'Sparar…',saved:'Sparat – skylten uppdateras nu.',saveFail:'Kunde inte spara.',wifiOn:'Wi-Fi ansluten',apOn:'Inställningsnätverk aktivt',lastErr:'Senaste SL-fel: ',statusFail:'Enhetens status är inte tillgänglig',siteN:'Hållplats'},
en:{title:'Platform board',connecting:'Connecting…',hDisplay:'Display',station:'Station',stationHelp:'The station whose departures are shown on the board.',platform:'Platform and direction',platformHelp:'Each platform is one direction. The list shows where trains from that platform go.',line:'Line',lineHelp:'Show every line that stops at the platform, or just one.',rows:'Visible departures',rowsHelp:'How many trains the board shows at once.',row1:'1 row',row2:'2 rows',brightness:'Brightness',brightnessHelp:'Panel brightness. Higher values draw more power.',show:'Show on board',ssid:'Wi-Fi network',ssidHelp:'The network the board uses to fetch departures from SL.',ssidPh:'Network name',pass:'Wi-Fi password',passPh:'Leave blank to keep the saved password',saveWifi:'Save Wi-Fi and restart',note:'Setup network: SL-Board-Setup · password: SLBoard9116 · open http://192.168.4.1',allLines:'All lines',lineN:'Line',platformN:'Platform',toward:'toward',loading:'Loading platforms…',noPlatforms:'No metro departures right now – pick a platform manually.',platformsError:'Could not load platforms: ',noDevice:'The device is not responding.',saving:'Saving…',saved:'Saved – the board is updating now.',saveFail:'Could not save.',wifiOn:'Wi-Fi connected',apOn:'Setup network active',lastErr:'Last SL error: ',statusFail:'Device status unavailable',siteN:'Stop'},
es:{title:'Panel del andén',connecting:'Conectando…',hDisplay:'Pantalla',station:'Estación',stationHelp:'La estación cuyas salidas se muestran en el panel.',platform:'Andén y dirección',platformHelp:'Cada andén es una dirección. La lista indica hacia dónde van los trenes de ese andén.',line:'Línea',lineHelp:'Muestra todas las líneas que paran en el andén, o solo una.',rows:'Salidas visibles',rowsHelp:'Cuántos trenes muestra el panel a la vez.',row1:'1 fila',row2:'2 filas',brightness:'Brillo',brightnessHelp:'Brillo del panel. Un valor más alto consume más corriente.',show:'Mostrar en el panel',ssid:'Red Wi-Fi',ssidHelp:'La red que usa el panel para descargar las salidas de SL.',ssidPh:'Nombre de la red',pass:'Contraseña Wi-Fi',passPh:'Déjalo vacío para mantener la contraseña guardada',saveWifi:'Guardar Wi-Fi y reiniciar',note:'Red de configuración: SL-Board-Setup · contraseña: SLBoard9116 · abre http://192.168.4.1',allLines:'Todas las líneas',lineN:'Línea',platformN:'Andén',toward:'hacia',loading:'Cargando andenes…',noPlatforms:'No hay salidas de metro ahora mismo; elige el andén a mano.',platformsError:'No se pudieron cargar los andenes: ',noDevice:'El dispositivo no responde.',saving:'Guardando…',saved:'Guardado: el panel se está actualizando.',saveFail:'No se pudo guardar.',wifiOn:'Wi-Fi conectado',apOn:'Red de configuración activa',lastErr:'Último error de SL: ',statusFail:'Estado del dispositivo no disponible',siteN:'Parada'}};
const $=id=>document.getElementById(id);
let lang='sv';try{lang=localStorage.getItem('lang')||''}catch(e){}
if(!T[lang]){const n=(navigator.language||'').slice(0,2);lang=T[n]?n:'sv'}
const t=k=>T[lang][k]||T.sv[k];
let st=null,stFailed=false,platforms=[],platformState='loading',platformError='',msg={key:'',err:false,text:''};
function option(select,value,text){const o=document.createElement('option');o.value=value;o.textContent=text;select.appendChild(o);return o}
function renderStatus(){const s=$('status');s.replaceChildren();if(stFailed){s.textContent=t('statusFail');return}if(!st){s.textContent=t('connecting');return}
const b=document.createElement('strong');b.textContent=st.wifi?t('wifiOn'):t('apOn');s.append(b,' · '+(st.wifi?st.ip:st.ap));
if(st.error){const e=document.createElement('div');e.textContent=t('lastErr')+st.error;s.appendChild(e)}
$('ssid').placeholder=st.ssid||t('ssidPh')}
function fillLines(selected){const p=platforms.find(x=>x.p===$('platform').value);$('line').replaceChildren();option($('line'),'',t('allLines'));(p?p.lines:[]).forEach(l=>option($('line'),l,t('lineN')+' '+l));if(selected&&![...$('line').options].some(o=>o.value===selected))option($('line'),selected,t('lineN')+' '+selected);$('line').value=selected||''}
function renderPlatforms(selPlatform,selLine){const sel=$('platform');sel.replaceChildren();$('platformWarn').textContent='';
if(platformState==='loading'){option(sel,'',t('loading'));sel.disabled=true;fillLines('');return}
platforms.forEach(p=>option(sel,p.p,t('platformN')+' '+p.p+' · '+t('toward')+' '+p.dest.slice(0,3).join(', ')));
if(!platforms.length){for(let i=1;i<=4;i++)option(sel,String(i),t('platformN')+' '+i);$('platformWarn').textContent=platformState==='nodevice'?t('noDevice'):platformError?t('platformsError')+platformError:t('noPlatforms')}
if(selPlatform&&![...sel.options].some(o=>o.value===selPlatform))option(sel,selPlatform,t('platformN')+' '+selPlatform);
sel.value=selPlatform||sel.options[0].value;sel.disabled=false;fillLines(selLine)}
function loadPlatforms(site,selPlatform,selLine){platformState='loading';renderPlatforms();
fetch('/api/platforms?site='+site).then(r=>r.json()).then(d=>{platforms=d.platforms||[];platformError=d.error||'';platformState='ready';renderPlatforms(selPlatform,selLine)}).catch(()=>{platforms=[];platformState='nodevice';renderPlatforms(selPlatform,selLine)})}
function renderMsg(){$('displayMsg').className=msg.err?'msg err':'msg';$('displayMsg').textContent=msg.text||(msg.key?t(msg.key):'')}
function applyLang(){document.documentElement.lang=lang;document.title='SL '+t('title');$('lang').value=lang;
document.querySelectorAll('[data-t]').forEach(e=>e.textContent=t(e.dataset.t));document.querySelectorAll('[data-tp]').forEach(e=>e.placeholder=t(e.dataset.tp));
renderStatus();if(platformState!=='loading')renderPlatforms($('platform').value,$('line').value);else renderPlatforms();renderMsg()}
$('lang').onchange=()=>{lang=$('lang').value;try{localStorage.setItem('lang',lang)}catch(e){}applyLang()};
STATIONS.split(';').map(s=>s.split('|')).sort((a,b)=>a[1].localeCompare(b[1],'sv')).forEach(([id,name])=>option($('site'),id,name));
$('site').onchange=()=>loadPlatforms($('site').value,'','');
$('platform').onchange=()=>fillLines('');
$('display').onsubmit=e=>{e.preventDefault();msg={key:'saving',err:false,text:''};renderMsg();
fetch('/api/display',{method:'POST',body:new URLSearchParams(new FormData($('display')))}).then(r=>r.ok?r.text():r.text().then(x=>{throw new Error(x)})).then(()=>{msg={key:'saved',err:false,text:''};renderMsg()}).catch(err=>{msg={key:'saveFail',err:true,text:err.message};renderMsg()})};
applyLang();
fetch('/api/status').then(r=>r.json()).then(s=>{st=s;renderStatus();
if(![...$('site').options].some(o=>o.value===String(s.site)))option($('site'),s.site,t('siteN')+' '+s.site);$('site').value=s.site;
$('rows').value=s.rows;$('brightness').value=s.brightness;$('brightnessValue').value=s.brightness;
loadPlatforms(s.site,s.platform,s.line)}).catch(()=>{stFailed=true;renderStatus()});
</script>
</body></html>
)HTML";

MatrixPanel_I2S_DMA* createMatrix() {
  HUB75_I2S_CFG panelConfig(kPanelWidth, kPanelHeight, kPanelChain);
  panelConfig.gpio.r1 = 25;
  panelConfig.gpio.g1 = 26;
  panelConfig.gpio.b1 = 27;
  panelConfig.gpio.r2 = 14;
  panelConfig.gpio.g2 = 13;
  panelConfig.gpio.b2 = 23;
  panelConfig.gpio.a = 32;
  panelConfig.gpio.b = 33;
  panelConfig.gpio.c = 17;
  panelConfig.gpio.d = 16;
  panelConfig.gpio.e = -1;
  panelConfig.gpio.clk = 22;
  panelConfig.gpio.lat = 21;
  panelConfig.gpio.oe = 4;
  return new MatrixPanel_I2S_DMA(panelConfig);
}

void handleRoot() {
  server.send_P(200, "text/html; charset=utf-8", kSettingsPage);
}

void handleStatus() {
  JsonDocument status;
  status["wifi"] = WiFi.status() == WL_CONNECTED;
  status["ip"] = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "";
  status["ap"] = WiFi.softAPIP().toString();
  status["ssid"] = config.wifiSsid;
  status["site"] = config.siteId;
  status["platform"] = config.platform;
  status["line"] = config.line;
  status["stop"] = stopData.stopName;
  status["rows"] = config.visibleRows;
  status["brightness"] = config.brightness;
  status["lastFetchOk"] = lastFetchOk;
  status["error"] = lastError;
  String response;
  serializeJson(status, response);
  server.send(200, "application/json", response);
}

// Platform and line designations are short alphanumeric codes such as "2" or "17".
bool isDesignation(const String& value, bool allowEmpty) {
  if (value.isEmpty()) return allowEmpty;
  if (value.length() > 4) return false;
  for (const char character : value) {
    if (!isalnum(static_cast<unsigned char>(character))) return false;
  }
  return true;
}

bool readSiteId(uint16_t& siteId) {
  const long value = server.arg("site").toInt();
  if (value < 1 || value > 65535) {
    server.send(400, "text/plain; charset=utf-8", "Ogiltigt SL-hållplats-ID");
    return false;
  }
  siteId = static_cast<uint16_t>(value);
  return true;
}

void handlePlatforms() {
  uint16_t siteId;
  if (!readSiteId(siteId)) return;

  std::vector<PlatformInfo> platforms;
  String stopName;
  String error;
  JsonDocument response;
  if (slAdapter.fetchPlatforms(siteId, platforms, stopName, error)) {
    response["stop"] = stopName;
    JsonArray list = response["platforms"].to<JsonArray>();
    for (const PlatformInfo& platform : platforms) {
      JsonObject item = list.add<JsonObject>();
      item["p"] = platform.designation;
      JsonArray lines = item["lines"].to<JsonArray>();
      for (const String& line : platform.lines) lines.add(line);
      JsonArray destinations = item["dest"].to<JsonArray>();
      for (const String& destination : platform.destinations) destinations.add(destination);
    }
  } else {
    response["error"] = error;
    response["platforms"].to<JsonArray>();
  }
  String body;
  serializeJson(response, body);
  server.send(200, "application/json", body);
}

void handleSaveDisplay() {
  uint16_t siteId;
  if (!readSiteId(siteId)) return;
  const String platform = server.arg("platform");
  const String line = server.arg("line");
  if (!isDesignation(platform, false) || !isDesignation(line, true)) {
    server.send(400, "text/plain; charset=utf-8", "Ogiltigt spår eller linje");
    return;
  }

  const bool sourceChanged =
      siteId != config.siteId || platform != config.platform || line != config.line;
  config.siteId = siteId;
  config.platform = platform;
  config.line = line;
  config.visibleRows = server.arg("rows") == "1" ? 1 : 2;
  config.brightness = static_cast<uint8_t>(constrain(server.arg("brightness").toInt(), 1, 255));
  saveConfig(config);

  // Apply live: drop the old station's departures and fetch the new ones right away.
  if (sourceChanged) {
    stopData = SlStopData();
    fetchRequested = true;
  }
  displayDirty = true;
  server.send(200, "text/plain; charset=utf-8", "OK");
}

void handleSaveConfig() {
  if (server.hasArg("ssid")) config.wifiSsid = server.arg("ssid");
  if (server.hasArg("wifiPassword") && !server.arg("wifiPassword").isEmpty()) {
    config.wifiPassword = server.arg("wifiPassword");
  }
  saveConfig(config);
  server.send(200, "text/html; charset=utf-8",
              "<!doctype html><html lang=sv><meta charset=utf-8>"
              "<p>Sparat. Startar om…<br>Saved. Restarting…<br>Guardado. Reiniciando…</p>");
  delay(500);
  ESP.restart();
}

void startWebServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/platforms", HTTP_GET, handlePlatforms);
  server.on("/api/display", HTTP_POST, handleSaveDisplay);
  server.on("/config", HTTP_POST, handleSaveConfig);
  server.onNotFound([]() { server.send(404, "text/plain; charset=utf-8", "Hittades inte"); });
  server.begin();
}

void startNetwork() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  WiFi.softAP(kAccessPointName, kAccessPointPassword);
  if (!config.wifiSsid.isEmpty()) {
    WiFi.begin(config.wifiSsid.c_str(), config.wifiPassword.c_str());
  }
}

void updateDepartures() {
  lastFetchMs = millis();
  fetchRequested = false;
  displayDirty = true;
  SlStopData latest;
  String error;
  if (slAdapter.fetch(config.siteId, config.platform, config.line, latest, error)) {
    stopData = latest;
    lastFetchOk = true;
    lastSuccessMs = lastFetchMs;
    lastError = "";
  } else {
    lastFetchOk = false;
    lastError = error;
    Serial.println("SL update failed: " + error);
  }
}

void setup() {
  Serial.begin(115200);
  loadConfig(config);

  matrix = createMatrix();
  if (!matrix->begin()) {
    Serial.println("HUB75 matrix init failed; check panel config and free memory");
  }
  stockholmTheme.begin(*matrix);
  stockholmTheme.render(*matrix, stopData, config.visibleRows, false, config.brightness);
  displayDirty = false;

  startNetwork();
  startWebServer();
  lastFetchMs = millis() - kPollIntervalMs;
  Serial.print("Setup page: http://");
  Serial.println(WiFi.softAPIP());
}

void loop() {
  server.handleClient();
  const uint32_t now = millis();

  const bool wifiConnected = WiFi.status() == WL_CONNECTED;
  if (!wifiConnected && !config.wifiSsid.isEmpty() &&
      now - lastReconnectMs >= kReconnectIntervalMs) {
    lastReconnectMs = now;
    WiFi.reconnect();
  }

  // Fetch right away when Wi-Fi comes up instead of waiting for the next poll.
  const bool justConnected = wifiConnected && !wasWifiConnected;
  wasWifiConnected = wifiConnected;
  if (justConnected) Serial.println("Wi-Fi connected: " + WiFi.localIP().toString());

  if (justConnected || fetchRequested || now - lastFetchMs >= kPollIntervalMs) {
    updateDepartures();
  }

  if (!stopData.departures.empty() && now - lastSuccessMs >= kStaleAfterMs) {
    stopData.departures.clear();
    displayDirty = true;
  }
  if (displayDirty) {
    stockholmTheme.render(*matrix, stopData, config.visibleRows, lastFetchOk, config.brightness);
    displayDirty = false;
  }
  stockholmTheme.animate(*matrix, millis());
  delay(5);
}
