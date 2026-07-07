const messageBox = document.querySelector("#message");
const registerForm = document.querySelector("#registerForm");
const regApiKeyInput = document.querySelector("#regApiKey");

function showMessage(text, type = "info") {
  messageBox.textContent = text;
  messageBox.className = `message ${type}`;
  window.setTimeout(() => {
    messageBox.textContent = "";
    messageBox.className = "message";
  }, 5000);
}

async function apiFetch(path, options = {}) {
  const headers = new Headers(options.headers || {});
  headers.set("X-API-Key", regApiKeyInput.value);
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

registerForm.addEventListener("submit", async (event) => {
  event.preventDefault();
  const regKey = regApiKeyInput.value;
  if (!regKey) {
    showMessage("Registrierungs-API-Key erforderlich.", "error");
    return;
  }

  const form = new FormData(registerForm);
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
    await apiFetch("/api/v1/registration/members", {
      method: "POST",
      body: JSON.stringify(payload),
    });
    showMessage("Mitglied registriert.", "success");
    registerForm.reset();
  } catch (error) {
    showMessage(error.message, "error");
  }
});
