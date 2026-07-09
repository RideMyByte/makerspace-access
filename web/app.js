const apiKeyInput = document.querySelector("#apiKey");
const regApiKeyInput = document.querySelector("#regApiKey");
const messageBox = document.querySelector("#message");
const memberList = document.querySelector("#memberList");
const presentMembers = document.querySelector("#presentMembers");
const editorModal = document.querySelector("#editorModal");
const editMemberForm = document.querySelector("#editMemberForm");
const deleteMemberBtn = document.querySelector("#deleteMemberBtn");
const closeEditorBtn = document.querySelector("#closeEditor");
const registerModal = document.querySelector("#registerModal");
const registerForm = document.querySelector("#registerForm");
const closeRegisterBtn = document.querySelector("#closeRegister");
const openRegisterBtn = document.querySelector("#openRegisterBtn");
const bulkSafetyBtn = document.querySelector("#bulkSafetyBtn");
const searchInput = document.querySelector("#searchInput");
const filterLastVisit = document.querySelector("#filterLastVisit");
const searchBtn = document.querySelector("#searchBtn");

const storedApiKey = localStorage.getItem("makerspaceApiKey");
if (storedApiKey) apiKeyInput.value = storedApiKey;

// ===== UI Config =====
let uiConfig = {};

async function loadUiConfig() {
  try {
    uiConfig = await apiFetch("/api/v1/ui-config");
    const logo = document.querySelector("#mainLogo");
    if (uiConfig.logo_url) {
      logo.src = uiConfig.logo_url;
    }
    if (uiConfig.logo_inverted) {
      logo.style.filter = "invert(1)";
    } else {
      logo.style.filter = "none";
    }
    const headline = document.querySelector("#mainHeadline");
    if (uiConfig.headline) {
      headline.textContent = uiConfig.headline;
    }
    document.title = uiConfig.app_name || "MakerSpace Access Admin";
  } catch (_) {
    // use defaults
  }
}

loadUiConfig();

apiKeyInput.addEventListener("input", () => {
  localStorage.setItem("makerspaceApiKey", apiKeyInput.value);
});

function today() {
  return new Date().toISOString().slice(0, 10);
}

function showMessage(text, type = "info") {
  messageBox.textContent = text;
  messageBox.className = `message ${type}`;
  window.setTimeout(() => {
    messageBox.textContent = "";
    messageBox.className = "message";
  }, 5000);
}

async function apiFetch(path, options = {}, key) {
  const headers = new Headers(options.headers || {});
  headers.set("X-API-Key", key || apiKeyInput.value);
  if (options.body && !headers.has("Content-Type")) {
    headers.set("Content-Type", "application/json");
  }
  const response = await fetch(path, { ...options, headers });
  if (!response.ok) {
    let detail = response.statusText;
    try {
      const error = await response.json();
      detail = error.detail || JSON.stringify(error);
    } catch (_) {
      detail = await response.text();
    }
    throw new Error(`${response.status}: ${detail}`);
  }
  if (response.status === 204) return null;
  return response.json();
}

function formatDate(value) {
  if (!value) return "-";
  return new Date(value).toLocaleString("de-DE");
}

function formatDateOnly(value) {
  if (!value) return "-";
  return value;
}

function memberName(m) {
  return `${m.first_name} ${m.last_name}`;
}

function categoryLabel(cat) {
  const map = {
    buerger: "Bürger*in",
    schueler: "Schüler*in",
    student: "Student*in",
    unternehmen: "Unternehmen",
    verein: "Verein",
    oeffentlich: "Öffentlich",
  };
  return map[cat] || cat;
}

// ===== Statistics =====
function updateStats(members, present) {
  const now = new Date();
  const todayStart = new Date(now.getFullYear(), now.getMonth(), now.getDate());
  const dayOfWeek = now.getDay();
  const mondayOffset = dayOfWeek === 0 ? -6 : 1 - dayOfWeek;
  const weekStart = new Date(todayStart);
  weekStart.setDate(weekStart.getDate() + mondayOffset);
  const sixMonthsAgo = new Date(now);
  sixMonthsAgo.setMonth(sixMonthsAgo.getMonth() - 6);

  let safetyTotal = 0,
    safetyRecent = 0,
    safetyExpired = 0;
  let presentToday = 0,
    presentWeek = 0;
  let visitsToday = 0,
    visitsWeek = 0,
    visitsTotal = 0;
  let makerstaff = 0;

  for (const m of members) {
    if (m.last_safety_briefing) {
      safetyTotal++;
      const d = new Date(m.last_safety_briefing);
      if (d >= sixMonthsAgo) safetyRecent++;
      else safetyExpired++;
    } else {
      safetyExpired++;
    }

    if (m.last_visit_at) {
      const d = new Date(m.last_visit_at);
      if (d >= todayStart) presentToday++;
      if (d >= weekStart) presentWeek++;
      if (d >= todayStart) visitsToday++;
      if (d >= weekStart) visitsWeek++;
    }

    visitsTotal += m.visits || 0;
    if (m.is_makerstaff) makerstaff++;
  }

  document.querySelector("#statSafetyTotal").textContent = safetyTotal;
  document.querySelector("#statSafetyRecent").textContent = safetyRecent;
  document.querySelector("#statSafetyExpired").textContent = safetyExpired;
  document.querySelector("#statPresentNow").textContent = present.length;
  document.querySelector("#statPresentToday").textContent = presentToday;
  document.querySelector("#statPresentWeek").textContent = presentWeek;
  document.querySelector("#statVisitsToday").textContent = visitsToday;
  document.querySelector("#statVisitsWeek").textContent = visitsWeek;
  document.querySelector("#statVisitsTotal").textContent = visitsTotal;
  document.querySelector("#statMembersTotal").textContent = members.length;
  document.querySelector("#statMakerstaff").textContent = makerstaff;
}

// ===== Bulk safety =====
let checkedPresentIds = new Set();

function updateBulkSafetyBtn() {
  bulkSafetyBtn.disabled = checkedPresentIds.size === 0;
}

function togglePresentCheckbox(id) {
  if (checkedPresentIds.has(id)) checkedPresentIds.delete(id);
  else checkedPresentIds.add(id);
  updateBulkSafetyBtn();
}

// ===== Present table =====
function renderPresentTable(members) {
  if (!members.length) {
    presentMembers.innerHTML =
      '<p class="muted">Keine eingeloggten Benutzer.</p>';
    return;
  }

  const rows = members
    .map((m) => {
      const duration = m.current_login_at
        ? Math.round(
            (Date.now() - new Date(m.current_login_at).getTime()) / 60000,
          ) + " min"
        : "-";
      return `
        <tr>
          <td><input type="checkbox" class="present-checkbox" data-id="${m.id}" ${checkedPresentIds.has(m.id) ? "checked" : ""} /></td>
          <td>${m.id}</td>
          <td><a href="#" class="edit-link" data-id="${m.id}">${memberName(m)}</a></td>
          <td>${m.email || "-"}</td>
          <td>${m.nfc_ids?.join(", ") || "-"}</td>
          <td>${formatDate(m.current_login_at)}</td>
          <td>${duration}</td>
          <td><button class="checkout-btn" data-id="${m.id}" title="Ausloggen">&times;</button></td>
        </tr>`;
    })
    .join("");

  presentMembers.innerHTML = `
    <table>
      <thead>
        <tr>
          <th></th><th>ID</th><th>Name</th><th>Kontakt</th><th>NFC-IDs</th>
          <th>Eingeloggt seit</th><th>Dauer</th><th>Aktion</th>
        </tr>
      </thead>
      <tbody>${rows}</tbody>
    </table>`;

  presentMembers.querySelectorAll(".edit-link").forEach((link) => {
    link.addEventListener("click", (e) => {
      e.preventDefault();
      openEditor(Number(link.dataset.id));
    });
  });

  presentMembers.querySelectorAll(".checkout-btn").forEach((btn) => {
    btn.addEventListener("click", async () => {
      const id = btn.dataset.id;
      const m = allMembers.find((x) => x.id === Number(id));
      const nfc = m?.nfc_ids?.[0];
      if (!nfc) {
        showMessage("Keine NFC-ID hinterlegt.", "error");
        return;
      }
      try {
        const result = await apiFetch("/api/v1/members/check-out", {
          method: "POST",
          body: JSON.stringify({ nfc_id: nfc }),
        });
        showMessage(result.message, "success");
        await loadData();
      } catch (error) {
        showMessage(error.message, "error");
      }
    });
  });

  presentMembers.querySelectorAll(".present-checkbox").forEach((cb) => {
    cb.addEventListener("change", () =>
      togglePresentCheckbox(Number(cb.dataset.id)),
    );
  });
}

// ===== All members table =====
function renderMemberTable(members) {
  if (!members.length) {
    memberList.innerHTML = '<p class="muted">Keine Einträge.</p>';
    return;
  }

  const rows = members
    .map(
      (m) => `
        <tr>
          <td>${m.id}</td>
          <td><a href="#" class="edit-link" data-id="${m.id}">${memberName(m)}</a></td>
          <td>${m.email || "-"}</td>
          <td>${categoryLabel(m.category)}</td>
          <td>${formatDateOnly(m.last_safety_briefing)}</td>
          <td>${m.is_present ? "Ja" : "Nein"}</td>
          <td>${formatDate(m.last_visit_at)}</td>
          <td>${m.visits}</td>
          <td>${m.total_presence_minutes} min</td>
          <td>${m.is_makerstaff ? "Team" : ""}</td>
          <td><button class="checkin-btn" data-nfc="${m.nfc_ids?.[0] || ""}" title="Einloggen">&#9654;</button></td>
        </tr>`,
    )
    .join("");

  memberList.innerHTML = `
    <table>
      <thead>
        <tr>
          <th>ID</th><th>Name</th><th>Kontakt</th><th>Kategorie</th>
          <th>Grundunterweisung</th><th>Anwesend</th><th>Letzter Besuch</th>
          <th>Besuche</th><th>Anwesenheit</th><th>Team</th><th></th>
        </tr>
      </thead>
      <tbody>${rows}</tbody>
    </table>`;

  memberList.querySelectorAll(".edit-link").forEach((link) => {
    link.addEventListener("click", (e) => {
      e.preventDefault();
      openEditor(Number(link.dataset.id));
    });
  });

  memberList.querySelectorAll(".checkin-btn").forEach((btn) => {
    btn.addEventListener("click", async () => {
      const nfc = btn.dataset.nfc;
      if (!nfc) {
        showMessage("Keine NFC-ID hinterlegt.", "error");
        return;
      }
      try {
        const result = await apiFetch("/api/v1/members/check-in", {
          method: "POST",
          body: JSON.stringify({ nfc_id: nfc }),
        });
        showMessage(result.message, "success");
        await loadData();
      } catch (error) {
        showMessage(error.message, "error");
      }
    });
  });
}

async function loadData() {
  const params = new URLSearchParams();
  const q = searchInput.value.trim();
  const lv = filterLastVisit.value;
  if (q) params.set("q", q);
  if (lv) params.set("last_visit", lv);
  const query = params.toString() ? "?" + params.toString() : "";

  const [members, present, pending] = await Promise.all([
    apiFetch("/api/v1/members" + query),
    apiFetch("/api/v1/members/present"),
    apiFetch("/api/v1/pending-nfc").catch(() => []),
  ]);
  allMembers = members;
  renderMemberTable(members);
  renderPresentTable(present);
  updateStats(members, present);
}

let allMembers = [];

// ===== Bulk safety =====
bulkSafetyBtn.addEventListener("click", async () => {
  if (checkedPresentIds.size === 0) return;
  try {
    await apiFetch("/api/v1/members/bulk-safety-briefing", {
      method: "POST",
      body: JSON.stringify({
        member_ids: Array.from(checkedPresentIds),
        last_safety_briefing: today(),
      }),
    });
    checkedPresentIds.clear();
    updateBulkSafetyBtn();
    showMessage("Grundunterweisung aktualisiert.", "success");
    await loadData();
  } catch (error) {
    showMessage(error.message, "error");
  }
});

// ===== Editor =====
function openEditor(id) {
  const m = allMembers.find((x) => x.id === id);
  if (!m) {
    showMessage("Mitglied nicht gefunden.", "error");
    return;
  }
  editMemberForm.elements.id.value = m.id;
  editMemberForm.elements.first_name.value = m.first_name;
  editMemberForm.elements.last_name.value = m.last_name;
  editMemberForm.elements.email.value = m.email || "";
  editMemberForm.elements.postal_code.value = m.postal_code || "";
  editMemberForm.elements.birthday.value = m.birthday || "";
  editMemberForm.elements.category.value = m.category;
  editMemberForm.elements.last_safety_briefing.value =
    m.last_safety_briefing || "";
  editMemberForm.elements.is_makerstaff.checked = m.is_makerstaff;
  editMemberForm.elements.nfc_ids.value = (m.nfc_ids || []).join("\n");
  editorModal.style.display = "flex";
}

function closeEditor() {
  editorModal.style.display = "none";
}

closeEditorBtn.addEventListener("click", closeEditor);
editorModal.addEventListener("click", (e) => {
  if (e.target === editorModal) closeEditor();
});

editMemberForm.addEventListener("submit", async (event) => {
  event.preventDefault();
  const form = new FormData(editMemberForm);
  const id = form.get("id");
  const payload = {
    first_name: form.get("first_name"),
    last_name: form.get("last_name"),
    email: form.get("email") || "",
    postal_code: form.get("postal_code") || null,
    birthday: form.get("birthday") || null,
    category: form.get("category"),
    last_safety_briefing: form.get("last_safety_briefing") || null,
    is_makerstaff: form.has("is_makerstaff"),
  };

  try {
    await apiFetch(`/api/v1/members/${id}`, {
      method: "PUT",
      body: JSON.stringify(payload),
    });

    // Sync NFC IDs
    const nfcList = (form.get("nfc_ids") || "")
      .split("\n")
      .map((s) => s.trim().toUpperCase())
      .filter(Boolean);
    const current = new Set(
      allMembers.find((x) => x.id === Number(id))?.nfc_ids || [],
    );

    for (const nfc of nfcList) {
      if (!current.has(nfc)) {
        await apiFetch(`/api/v1/members/${id}/nfc`, {
          method: "POST",
          body: JSON.stringify({ nfc_id: nfc }),
        }).catch(() => {});
      }
    }
    for (const nfc of current) {
      if (!nfcList.includes(nfc)) {
        await apiFetch(`/api/v1/members/${id}/nfc/${nfc}`, {
          method: "DELETE",
        }).catch(() => {});
      }
    }

    showMessage("Mitglied aktualisiert.", "success");
    closeEditor();
    await loadData();
  } catch (error) {
    showMessage(error.message, "error");
  }
});

deleteMemberBtn.addEventListener("click", async () => {
  const id = editMemberForm.elements.id.value;
  if (!id) return;
  if (!confirm(`Mitglied ${id} wirklich löschen?`)) return;
  try {
    await apiFetch(`/api/v1/members/${id}`, { method: "DELETE" });
    showMessage("Mitglied gelöscht.", "success");
    closeEditor();
    await loadData();
  } catch (error) {
    showMessage(error.message, "error");
  }
});

// ===== Registration Modal =====
openRegisterBtn.addEventListener("click", () => {
  registerModal.style.display = "flex";
});

closeRegisterBtn.addEventListener("click", () => {
  registerModal.style.display = "none";
});

registerModal.addEventListener("click", (e) => {
  if (e.target === registerModal) registerModal.style.display = "none";
});

registerForm.addEventListener("submit", async (event) => {
  event.preventDefault();
  const form = new FormData(registerForm);
  const regKey = document.querySelector("#regApiKey").value;
  if (!regKey) {
    showMessage("Registrierungs-API-Key erforderlich.", "error");
    return;
  }

  const nfcIds = (form.get("nfc_ids") || "")
    .split("\n")
    .map((s) => s.trim().toUpperCase())
    .filter(Boolean)
    .slice(0, 10);

  const payload = {
    first_name: form.get("first_name"),
    last_name: form.get("last_name"),
    email: form.get("email") || "",
    postal_code: form.get("postal_code") || null,
    birthday: form.get("birthday") || null,
    category: form.get("category"),
    is_makerstaff: form.has("is_makerstaff"),
    safety_briefed: form.has("safety_briefed"),
    nfc_ids: nfcIds,
  };

  try {
    await apiFetch(
      "/api/v1/registration/members",
      { method: "POST", body: JSON.stringify(payload) },
      regKey,
    );
    showMessage("Mitglied registriert.", "success");
    registerForm.reset();
    registerModal.style.display = "none";
    await loadData();
  } catch (error) {
    showMessage(error.message, "error");
  }
});

// ===== Search =====
searchBtn.addEventListener("click", () => {
  loadData().catch((error) => showMessage(error.message, "error"));
});

searchInput.addEventListener("keydown", (e) => {
  if (e.key === "Enter") searchBtn.click();
});

filterLastVisit.addEventListener("change", () => {
  loadData().catch((error) => showMessage(error.message, "error"));
});

// ===== Refresh =====
document.querySelector("#refreshMembers").addEventListener("click", () => {
  loadData().catch((error) => showMessage(error.message, "error"));
});

document.querySelector("#refreshPresent").addEventListener("click", () => {
  loadData().catch((error) => showMessage(error.message, "error"));
});

window.setInterval(() => {
  loadData().catch(() => {});
}, 5000);

loadData().catch((error) => showMessage(error.message, "error"));
