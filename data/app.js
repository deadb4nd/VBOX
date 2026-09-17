"use strict";

const $ = (id) => document.getElementById(id);

/* ---------------- tabs ---------------- */

document.querySelectorAll(".tab").forEach((tab) => {
  tab.addEventListener("click", () => {
    document.querySelectorAll(".tab").forEach((t) => t.classList.remove("active"));
    document.querySelectorAll(".view").forEach((v) => v.classList.remove("active"));
    tab.classList.add("active");
    $(`tab-${tab.dataset.tab}`).classList.add("active");
  });
});

/* ---------------- helpers ---------------- */

function flash(msg, ok) {
  const el = $("flash");
  el.textContent = msg;
  el.className = "flash show " + (ok ? "ok" : "err");
  clearTimeout(flash._t);
  flash._t = setTimeout(() => (el.className = "flash"), 4000);
}

function setConn(online) {
  const c = $("conn");
  c.classList.toggle("online", online);
  c.classList.toggle("offline", !online);
  $("conn-txt").textContent = online ? "online" : "offline";
}

async function api(path, opts) {
  const res = await fetch(path, opts);
  if (!res.ok) throw new Error(await res.text());
  return res.json();
}

function esc(s) {
  return String(s || "")
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;");
}

const AUTH = ["OPEN", "WEP", "WPA", "WPA2", "WPA3", "?"];
const CMD_LABEL = {
  deauth: "Deauth",
  recon: "WiFi Scan",
  blescan: "BLE Scan",
  blespam: "BLE Spam",
  probe: "Probe Flood",
  fakeap: "Fake AP",
  wait: "Wait",
};

function fmtMs(ms) {
  if (!ms) return "—";
  return ms % 1000 === 0 ? `${ms / 1000}s` : `${(ms / 1000).toFixed(1)}s`;
}

/* ---------------- status polling ---------------- */

function setStatePill(el, text, cls) {
  el.textContent = text;
  el.className = "badge " + cls;
}

function setAllDisabled(disabled) {
  document.querySelectorAll("[data-action]").forEach((b) => {
    if (!b.dataset.locked) b.disabled = disabled;
  });
  $("stop").disabled = !disabled;
  $("stop2").disabled = !disabled;
}

function stepTarget(st) {
  if (st.cmd !== "deauth") return "";
  const ap = st.ap || "";
  const sta = st.sta || "";
  if (!ap && !sta) return "broadcast";
  if (ap && sta) return `${ap} \u2192 ${sta}`;
  if (ap) return `${ap} \u2192 broadcast`;
  return "";
}

function renderQueue(script) {
  const running = !!(script && script.running);
  const steps = (script && script.steps) || [];
  const idx = script ? script.index : 0;

  $("queue-summary").textContent = running ? `${idx + 1}/${steps.length}` : "idle";
  $("script-queue-meta").textContent = running ? `${idx + 1}/${steps.length} running` : "idle";

  if (!running) {
    $("queue-line").className = "queue-empty";
    $("queue-line").textContent = "No script running.";
    $("script-queue").innerHTML = '<div class="queue-empty">No script running.</div>';
    return;
  }

  const cur = steps[idx] || {};
  const next = steps[idx + 1];
  const curLabel = CMD_LABEL[cur.cmd] || cur.cmd || "—";
  $("queue-line").className = "";
  $("queue-line").innerHTML =
    `Step ${idx + 1} of ${steps.length}: <b>${esc(curLabel)}</b> (${fmtMs(cur.ms)})` +
    (next ? ` &middot; next ${esc(CMD_LABEL[next.cmd] || next.cmd)}` : " &middot; final step");

  $("script-queue").innerHTML = steps
    .map((st, i) => {
      const target = stepTarget(st);
      return (
        `<div class="q-item ${esc(st.phase)}">` +
        `<span class="q-idx">${i + 1}</span>` +
        `<span class="q-main"><span class="q-cmd">${esc(CMD_LABEL[st.cmd] || st.cmd)}</span>` +
        (target ? `<span class="q-target">${esc(target)}</span>` : "") +
        `</span>` +
        `<span class="q-dur">${fmtMs(st.ms)}</span>` +
        `</div>`
      );
    })
    .join("");
}

async function pollStatus() {
  try {
    const st = await api("/api/status");
    setConn(true);
    $("action").textContent = st.label || "None";
    $("heap").textContent = `${Math.round(st.free_heap / 1024)} KB`;

    const stateEl = $("state");
    if (st.running) {
      setStatePill(stateEl, "RUNNING", "running");
    } else if (st.idle) {
      setStatePill(stateEl, "SAFE", "safe");
    } else if (st.action !== "none") {
      setStatePill(stateEl, "ARMED", "warning");
    } else {
      setStatePill(stateEl, "IDLE", "idle");
    }

    setAllDisabled(st.running);
    const safe = !st.running && !!st.idle;
    $("safe-hint").classList.toggle("show", safe);

    $("c-deauth").textContent = st.deauth || 0;
    $("c-probe").textContent = st.probes || 0;
    $("c-beacons").textContent = st.beacons || 0;

    renderQueue(st.script);
  } catch (e) {
    setConn(false);
  }
}
setInterval(pollStatus, 1000);
pollStatus();

/* ---------------- control + recon actions ---------------- */

async function startAction(action, extra, label) {
  const body = new URLSearchParams({ name: action });
  if (extra) body.append("extra", extra);
  try {
    await api("/api/action", {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body,
    });
    flash(`Started: ${label || action}`, true);
  } catch (e) {
    flash(e.message || "Failed to start action", false);
  }
  pollStatus();
}

/* ---------------- capabilities -> dynamic UI ---------------- */

const GROUP_TARGET = { 0: "tiles-operations", 1: "tiles-recon" };
let ENABLED = {};

function tileHtml(m) {
  return (
    `<button class="tile${m.danger ? " danger" : ""}" data-action="${esc(m.slug)}"` +
    (m.enabled ? "" : ' disabled data-locked="1"') +
    `><span class="tile-title">${esc(m.label)}</span>` +
    `<span class="tile-sub">${esc(m.hint)}</span></button>`
  );
}

/* Tiles and branding come from the firmware, so adding a backend module
   shows up here with no HTML/JS changes. */
async function loadCapabilities() {
  let caps;
  try {
    caps = await api("/api/capabilities");
  } catch (e) {
    return;
  }

  const brand = caps.brand || {};
  if (brand.name) {
    $("brand-name").textContent = brand.name;
    document.title = `${brand.name} Console`;
  }
  if (brand.tagline) $("brand-tagline").textContent = brand.tagline;

  const buckets = {};
  ENABLED = {};
  (caps.modules || []).forEach((m) => {
    ENABLED[m.slug] = !!m.enabled;
    (buckets[m.group] || (buckets[m.group] = [])).push(m);
  });
  Object.keys(buckets).forEach((g) => {
    const el = $(GROUP_TARGET[g]);
    if (el) el.innerHTML = buckets[g].map(tileHtml).join("");
  });

  /* hide hand-written quick-start tiles whose module is compiled out
     (the generated grids keep disabled tiles visible but greyed) */
  document.querySelectorAll(".tile[data-action]").forEach((t) => {
    if (t.closest("#tiles-operations, #tiles-recon")) return;
    if (ENABLED[t.dataset.action] === false) t.hidden = true;
  });
}

document.addEventListener("click", (e) => {
  const btn = e.target.closest(".tile[data-action]");
  if (!btn || btn.disabled) return;
  const title = btn.querySelector(".tile-title");
  startAction(btn.dataset.action, btn.dataset.extra, title && title.textContent);
});

loadCapabilities();

async function stopAll() {
  try {
    await api("/api/stop", { method: "POST" });
    flash("Stopped", true);
  } catch (e) {
    flash(e.message || "Failed to stop", false);
  }
  pollStatus();
}
$("stop").addEventListener("click", stopAll);
$("stop2").addEventListener("click", stopAll);

/* targeted deauth: fires immediately with bssid (and optional client) */
async function fireTargeted(apHex, clientHex) {
  $("target-name").textContent = apHex + (clientHex ? " \u2192 " + clientHex : "");
  $("target-chip").classList.remove("hidden");
  const body = new URLSearchParams({ name: "deauth" });
  body.set("bssid", apHex);
  if (clientHex) body.set("client", clientHex);
  try {
    await api("/api/action", {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body,
    });
    flash("Targeted deauth started", true);
  } catch (e) {
    flash(e.message || "Deauth failed", false);
  }
  pollStatus();
}

$("clear-target").addEventListener("click", () =>
  $("target-chip").classList.add("hidden"));

$("clear-caps").addEventListener("click", async () => {
  try {
    await api("/api/captures/clear", { method: "POST" });
    flash("Capture log cleared", true);
  } catch (e) {
    flash(e.message || "Clear failed", false);
  }
  renderRecon();
});

/* ---------------- recon rendering ---------------- */

function authLabel(auth) {
  return AUTH[auth] || "?";
}

async function renderRecon() {
  let sc;
  try {
    sc = await api("/api/scan");
  } catch (e) {
    return;
  }

  setStatePill($("wifi-scan"), sc.running ? "ON" : "off", sc.running ? "running" : "idle");
  setStatePill($("ble-scan"), sc.ble_running ? "ON" : "off", sc.ble_running ? "running" : "idle");

  const ctr = sc.counters || {};
  $("c-beacons").textContent = ctr.beacons || 0;
  $("c-probe").textContent = ctr.probe_reqs || 0;
  $("c-deauth").textContent = ctr.deauths || 0;
  $("c-eapol").textContent = ctr.eapol || 0;
  $("c-data").textContent = ctr.data || 0;

  const hv = sc.harvest || {};
  $("h-ready").textContent = hv.ready || 0;
  $("h-pairs").textContent = hv.pairs || 0;
  $("h-frames").textContent = hv.eapol_frames || 0;

  const caps = sc.captures || [];
  const aps = sc.aps || [];
  const stas = sc.stations || [];
  const bles = sc.ble || [];
  $("ap-count").textContent = `${aps.length} found`;
  $("sta-count").textContent = `${stas.length} found`;
  $("ble-count").textContent = `${bles.length} found`;

  const capBody = $("cap-table").querySelector("tbody");
  capBody.innerHTML = "";
  caps.forEach((c) => {
    const tr = document.createElement("tr");
    tr.innerHTML =
      `<td class="mono">${esc(c.ap)}</td>` +
      `<td class="mono">${esc(c.sta)}</td>` +
      `<td class="num">${c.msgs}</td>` +
      `<td class="${c.has_pmkid ? "ok" : "dim"}">${c.has_pmkid ? "PMKID" : "—"}</td>` +
      `<td class="${c.ready ? "ok" : "dim"}">${c.ready ? "READY" : "incomplete"}</td>`;
    capBody.appendChild(tr);
  });

  const apBody = $("ap-table").querySelector("tbody");
  apBody.innerHTML = "";
  aps.forEach((ap) => {
    const tr = document.createElement("tr");
    const name = ap.ssid || (ap.hidden ? "(hidden)" : "(no name)");
    tr.innerHTML =
      `<td class="nm">${esc(name)}</td>` +
      `<td class="num">${ap.ch}</td>` +
      `<td class="rssi">${typeof ap.rssi === "number" ? ap.rssi : "—"}</td>` +
      `<td>${authLabel(ap.auth)}</td>` +
      `<td><button class="btn-cell ap-deauth" data-bssid="${ap.bssid}">Deauth</button></td>`;
    apBody.appendChild(tr);
  });

  const staBody = $("sta-table").querySelector("tbody");
  staBody.innerHTML = "";
  stas.forEach((st) => {
    const tr = document.createElement("tr");
    tr.innerHTML =
      `<td class="mono">${esc(st.mac)}</td>` +
      `<td class="mono">${esc(st.ap)}</td>` +
      `<td class="rssi">${typeof st.rssi === "number" ? st.rssi : "—"}</td>` +
      `<td><button class="btn-cell sta-deauth" data-bssid="${st.ap}" data-client="${st.mac}">Deauth</button></td>`;
    staBody.appendChild(tr);
  });

  const bleBody = $("ble-table").querySelector("tbody");
  bleBody.innerHTML = "";
  bles.forEach((b) => {
    const tr = document.createElement("tr");
    tr.innerHTML =
      `<td class="nm">${esc(b.name)}</td>` +
      `<td class="mono">${esc(b.mac)}</td>` +
      `<td class="rssi">${typeof b.rssi === "number" ? b.rssi : "—"}</td>`;
    bleBody.appendChild(tr);
  });
}

document.addEventListener("click", (e) => {
  const apBtn = e.target.closest(".ap-deauth");
  if (apBtn) {
    fireTargeted(apBtn.dataset.bssid, "");
    return;
  }
  const staBtn = e.target.closest(".sta-deauth");
  if (staBtn) {
    fireTargeted(staBtn.dataset.bssid, staBtn.dataset.client);
  }
});

setInterval(renderRecon, 2000);
renderRecon();

/* ---------------- script ---------------- */

function scriptMsg(msg, ok) {
  const el = $("script-status");
  el.textContent = msg;
  el.className = "toolbar-info " + (ok ? "ok" : "err");
  clearTimeout(scriptMsg._t);
  scriptMsg._t = setTimeout(() => (el.className = "toolbar-info"), 4000);
}

let scriptLoaded = false;
async function loadScript() {
  if (scriptLoaded) return;
  try {
    const s = await api("/api/script");
    $("script-text").value = s.text || "";
    scriptLoaded = true;
    scriptMsg(`${s.steps} step(s) loaded`, true);
  } catch (e) {
    scriptMsg("Could not load script", false);
  }
}

async function postScript(path, verb) {
  try {
    const r = await api(path, {
      method: "POST",
      headers: { "Content-Type": "text/plain" },
      body: $("script-text").value,
    });
    scriptMsg(`${verb} ${r.steps} step(s)`, true);
    pollStatus();
  } catch (e) {
    scriptMsg(e.message || "Failed", false);
  }
}

$("script-save").addEventListener("click", () => postScript("/api/script", "Saved"));
$("script-run").addEventListener("click", () => postScript("/api/script/run", "Running"));
$("script-stop").addEventListener("click", stopAll);

loadScript();

/* ---------------- settings ---------------- */

async function loadSettings() {
  try {
    const s = await api("/api/settings");
    $("default_action").value = s.default_action;
    $("ap_ssid").value = s.ap_ssid;
    $("idle_timeout_ms").value = s.idle_timeout_ms;
    $("warning_duration_ms").value = s.warning_duration_ms;
    $("fakeap_channel").value = s.fakeap_channel;
    $("fakeap_max_connections").value = s.fakeap_max_connections;
    $("fakeap_beacon_interval").value = s.fakeap_beacon_interval;
    $("ble_spam_enabled").checked = s.ble_spam_enabled;
    $("sleep_timeout_ms").value = s.sleep_timeout_ms || 0;
    $("ssids").value = s.ssids;
  } catch (e) {
    flash("Could not load settings", false);
  }
}

$("settings-form").addEventListener("submit", async (ev) => {
  ev.preventDefault();
  const body = new URLSearchParams({
    default_action: $("default_action").value,
    ap_ssid: $("ap_ssid").value,
    idle_timeout_ms: $("idle_timeout_ms").value,
    warning_duration_ms: $("warning_duration_ms").value,
    fakeap_channel: $("fakeap_channel").value,
    fakeap_max_connections: $("fakeap_max_connections").value,
    fakeap_beacon_interval: $("fakeap_beacon_interval").value,
    ble_spam_enabled: $("ble_spam_enabled").checked ? "1" : "0",
    sleep_timeout_ms: $("sleep_timeout_ms").value || "0",
    ssids: $("ssids").value,
  });
  const msgEl = $("save-msg");
  try {
    await api("/api/settings", {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body,
    });
    msgEl.textContent = "Configuration saved";
    msgEl.className = "flash show ok";
  } catch (e) {
    msgEl.textContent = e.message || "Save failed";
    msgEl.className = "flash show err";
  }
  setTimeout(() => (msgEl.className = "flash"), 4000);
});

loadSettings();
