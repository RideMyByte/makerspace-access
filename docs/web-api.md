# Web Frontend API – Dokumentation

Die Web-Frontend API (`/api/v1/web/...`) ist für ein externes Web-Frontend gedacht.
Sie verwendet den **`WEB_API_KEY`** (zusätzlicher zweiter Schlüssel) und bietet nur die nötigsten Endpunkte.

## Authentifizierung

Alle Endpunkte benötigen einen gültigen API-Key im Header:

```
X-API-Key: <web_api_key>
```

Akzeptierte Keys (nach Priorität):
1. `API_KEY` – Admin-Key (voller Zugriff)
2. `REGISTRATION_API_KEY` – Registrierungs-Key
3. `WEB_API_KEY` – Web-Frontend-Key (eingeschränkter Zugriff)

---

## Endpunkte

### 1. Mitglieder-Liste (Read-only)

```
GET /api/v1/web/members?q=<suchbegriff>
```

**Parameter:**
- `q` (optional) – Sucht in Vorname, Nachname, NFC-IDs

**Rückgabe (WebMemberRead):**
```json
{
  "id": 2,
  "first_name": "Mario",
  "nfc_ids": ["0434521A523880", "A988D1A3"],
  "last_visit_at": "2026-07-13T13:03:00.248562",
  "category": "mitarbeiter",
  "safety_briefing_valid": true,
  "last_safety_briefing": "2026-07-07",
  "postal_code": "45966",
  "is_present": true
}
```

**Enthaltene Felder:**
| Feld | Typ | Beschreibung |
|------|-----|-------------|
| `id` | int | Mitglieds-ID |
| `first_name` | string | Vorname |
| `nfc_ids` | string[] | Liste der NFC-IDs |
| `last_visit_at` | string (ISO) | Letzter Besuch (UTC) |
| `category` | string | Kategorie |
| `safety_briefing_valid` | bool | Grundunterweisung gültig? |
| `last_safety_briefing` | string (ISO) | Datum der letzten Grundunterweisung |
| `postal_code` | string | Postleitzahl |
| `is_present` | bool | Ist aktuell eingeloggt |

---

### 2. Mitglied per NFC-ID abrufen

```
GET /api/v1/web/members/nfc/{nfc_id}
```

**Rückgabe:** `WebMemberRead` (siehe oben)

---

### 3. Anwesende Mitglieder

```
GET /api/v1/web/members/present
```

**Rückgabe:** Liste von `WebMemberRead`

---

### 4. Einloggen

```
POST /api/v1/web/check-in
Content-Type: application/json

{
  "nfc_id": "0434521A523880"
}
```

**Rückgabe:**
```json
{
  "member": { ... },
  "message": "Checked in"
}
```

---

### 5. Ausloggen

```
POST /api/v1/web/check-out
Content-Type: application/json

{
  "nfc_id": "0434521A523880"
}
```

**Rückgabe:**
```json
{
  "member": { ... },
  "message": "Checked out, added 15 minutes"
}
```

---

### 6. Neues Mitglied anlegen

```
POST /api/v1/web/members
Content-Type: application/json

{
  "first_name": "Max",
  "last_name": "Mustermann",
  "email": "max@example.com",
  "postal_code": "12345",
  "category": "buerger",
  "nfc_ids": ["ABCD1234"],
  "safety_briefed": false,
  "last_safety_briefing": null,
  "additional_inductions": [
    {"name": "3D-Drucker", "date": "2026-07-01"}
  ]
}
```

**Pflichtfelder:**
- `first_name` (string)
- `last_name` (string)

**Optionale Felder:**
- `email` (string, default `""`)
- `postal_code` (string oder null)
- `birthday` (date oder null)
- `category` (string, default `"buerger"`): `schueler`, `student`, `buerger`, `unternehmen`, `verein`, `oeffentlich`, `mitarbeiter`
- `last_safety_briefing` (date oder null)
- `is_makerstaff` (bool, default false)
- `registration_date` (date oder null)
- `nfc_ids` (string[], max 10)
- `safety_briefed` (bool) – wenn true und kein `last_safety_briefing` gesetzt, wird heute als Datum verwendet
- `additional_inductions` (array von `{name, date}` oder null, max 20 Einträge)

**Rückgabe:** `WebMemberRead` (erstelltes Mitglied)

**Fehler:**
- `409 Conflict` – NFC-ID bereits vergeben
- `422 Unprocessable Entity` – Ungültige Kategorie

---

## Konfiguration (.env)

```
WEB_API_KEY=web_secret_key_12345
```

Füge diesen Key in deine `.env` Datei ein. Der API-Server muss neu gestartet werden.