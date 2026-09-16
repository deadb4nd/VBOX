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

const AUTH = ["OPEN", "WEP", "WPA", "WPA2", "WPA3", "?"];

/* ---------------- status polling ---------------- */

function setStatePill(el, text, cls) {
  el.textContent = text;
  el.className = "state " + cls;
}

function setAllDisabled(disabled) {
  document
    .querySelectorAll(".btn[data-action]")
    .forEach((b) => (b.disabled = disabled));
  $("#stop").disabled = !disabled;
  $("#stop2").disabled = !disabled;
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
  } catch (e) {
    setConn(false);
  }
}
setInterval(pollStatus, 1000);
pollStatus();

/* ---------------- control + recon actions ---------------- */

document.querySelectorAll(".btn[data-action]").forEach((btn) => {
  btn.addEventListener("click", async () => {
    const extra = btn.dataset.extra;
    const body = new URLSearchParams({
      name: btn.dataset.action,
    });
    if (extra) body.append("extra", extra);
    try {
      await api("/api/action", {
        method: "POST",
        headers: { "Content-Type": "application/x-www-form-urlencoded" },
        body,
      });
      flash(`Started: ${btn.textContent}`, true);
    } catch (e) {
      flash(e.message || "Failed to start action", false);
    }
    pollStatus();
  });
});

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
  $("target-name").textContent =
    apHex + (clientHex ? " -> " + clientHex : "");
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

/* handshake harvest: clear the in-RAM capture log */
$("clear-caps").addEventListener("click", async () => {
  try {
    await api("/api/captures/clear", { method: "POST" });
    flash("Captures cleared", true);
  } catch (e) {
    flash(e.message || "Clear failed", false);
  }
  renderRecon();
});

/* ---------------- recon rendering ---------------- */

function esc(s) {
  return String(s || "")
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;");
}

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

  const capBody = $("cap-table").querySelector("tbody");
  capBody.innerHTML = "";
  (sc.captures || []).forEach((c) => {
    const tr = document.createElement("tr");
    tr.innerHTML =
      `<td class="mono">${esc(c.ap)}</td>` +
      `<td class="mono">${esc(c.sta)}</td>` +
      `<td>${c.msgs}</td>` +
      `<td class="${c.has_pmkid ? "ok" : ""}">` +
      (c.has_pmkid ? "PMKID" : "-") + `</td>` +
      `<td class="${c.ready ? "ok" : ""}">${c.ready ? "READY" : "-"}</td>`;
    capBody.appendChild(tr);
  });

  const apBody = $("ap-table").querySelector("tbody");
  apBody.innerHTML = "";
  (sc.aps || []).forEach((ap) => {
    const tr = document.createElement("tr");
    const name = ap.ssid || (ap.hidden ? "(hidden)" : "(no name)");
    tr.innerHTML =
      `<td class="nm">${esc(name)}</td>` +
      `<td>${ap.ch}</td>` +
      `<td class="rssi">${typeof ap.rssi === "number" ? ap.rssi : "-"}</td>` +
      `<td>${authLabel(ap.auth)}</td>` +
      `<td><button class="mini ap-deauth" data-bssid="${ap.bssid}">Deauth</button></td>`;
    apBody.appendChild(tr);
  });

  const staBody = $("sta-table").querySelector("tbody");
  staBody.innerHTML = "";
  (sc.stations || []).forEach((st) => {
    const tr = document.createElement("tr");
    tr.innerHTML =
      `<td class="mono">${esc(st.mac)}</td>` +
      `<td class="mono">${esc(st.ap)}</td>` +
      `<td class="rssi">${typeof st.rssi === "number" ? st.rssi : "-"}</td>` +
      `<td><button class="mini sta-deauth" data-bssid="${st.ap}" data-client="${st.mac}">Deauth</button></td>`;
    staBody.appendChild(tr);
  });

  const bleBody = $("ble-table").querySelector("tbody");
  bleBody.innerHTML = "";
  (sc.ble || []).forEach((b) => {
    const tr = document.createElement("tr");
    tr.innerHTML =
      `<td class="nm">${esc(b.name)}</td>` +
      `<td class="mono">${esc(b.mac)}</td>` +
      `<td class="rssi">${typeof b.rssi === "number" ? b.rssi : "-"}</td>`;
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
    return;
  }
});

setInterval(renderRecon, 2000);
renderRecon();

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
    ssids: $("ssids").value,
  });
  const msgEl = $("save-msg");
  try {
    await api("/api/settings", {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body,
    });
    msgEl.textContent = "Settings saved";
    msgEl.className = "flash show ok";
  } catch (e) {
    msgEl.textContent = e.message || "Save failed";
    msgEl.className = "flash show err";
  }
  setTimeout(() => (msgEl.className = "flash"), 4000);
});

loadSettings();