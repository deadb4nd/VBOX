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

/* ---------------- status polling ---------------- */

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

async function pollStatus() {
  try {
    const st = await api("/api/status");
    setConn(true);
    $("action").textContent = st.label || "None";
    $("heap").textContent = `${Math.round(st.free_heap / 1024)} KB`;

    const stateEl = $("state");
    if (st.running) {
      stateEl.textContent = "RUNNING";
      stateEl.className = "state running";
    } else if (st.action !== "none") {
      stateEl.textContent = "ARMED";
      stateEl.className = "state warning";
    } else {
      stateEl.textContent = "IDLE";
      stateEl.className = "state idle";
    }

    $("stop").disabled = !st.running;
    document.querySelectorAll(".btn.act").forEach((b) => (b.disabled = st.running));
  } catch (e) {
    setConn(false);
  }
}
setInterval(pollStatus, 1000);
pollStatus();

/* ---------------- control ---------------- */

document.querySelectorAll(".btn.act").forEach((btn) => {
  btn.addEventListener("click", async () => {
    try {
      await api("/api/action", {
        method: "POST",
        headers: { "Content-Type": "application/x-www-form-urlencoded" },
        body: "name=" + encodeURIComponent(btn.dataset.action),
      });
      flash(`Started: ${btn.textContent}`, true);
    } catch (e) {
      flash(e.message || "Failed to start action", false);
    }
    pollStatus();
  });
});

$("stop").addEventListener("click", async () => {
  try {
    await api("/api/stop", { method: "POST" });
    flash("Stopped", true);
  } catch (e) {
    flash(e.message || "Failed to stop", false);
  }
  pollStatus();
});

/* ---------------- settings ---------------- */

async function loadSettings() {
  try {
    const s = await api("/api/settings");
    $("default_action").value = s.default_action;
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