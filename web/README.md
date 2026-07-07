# Web

Static admin UI for the makerspace access system.

It is served by nginx from the repository `web/` directory and talks to the FastAPI backend under `/api/v1`.

## Features

- Create guests with form fields.
- Show a table of all guests and their stored information.
- Update `last_safety_briefing`, defaulting to today's date.
- Check in a guest by numeric ID or NFC ID.
- Check out currently present guests from a dropdown.
- Show all currently present guests.

## Authentication

Enter the API key from `infra/.env` into the API-Key field in the page header. The UI sends it as:

```http
X-API-Key: ...
```

The browser stores the API key in `localStorage` for convenience during local development. Use an internal/reverse-proxied deployment and avoid shared browser profiles for production operations.
