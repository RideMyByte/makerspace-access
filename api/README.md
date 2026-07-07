# API

FastAPI backend for the makerspace access system.

The API currently provides a minimal data layer for guests and admin users. It is intentionally small and ready for later SQLAlchemy/Alembic expansion.

## Endpoints

Public health endpoints:

- `GET /health`
- `GET /api/v1/health`

Protected endpoints, requiring `X-API-Key`:

- `POST /api/v1/guests` - create a guest.
- `GET /api/v1/guests/{guest_id}` - fetch all stored information for one guest.
- `POST /api/v1/users` - create an admin user record with a hashed password.

## Authentication

All non-health API calls use a simple API-key header for now:

```http
X-API-Key: your-token-from-infra-env
```

This is compatible with ESP32 HTTP clients and simple web calls. Replace it later with scoped tokens/JWTs or a dedicated device credential flow when roles and admin login are implemented.

## Guest fields

Guest records currently include:

- `id`
- `last_name`
- `first_name`
- `email`
- `phone_number`
- `nfc_id`
- `last_safety_briefing`
- `wh_employee`
- `pupil`
- `student`
- `staff`
- `created_at`
- `visits`
- `hours_on_site`

Example:

```sh
curl -X POST http://localhost/api/v1/guests \
  -H 'Content-Type: application/json' \
  -H 'X-API-Key: change-me-to-a-long-random-token' \
  -d '{
    "id": 1,
    "last_name": "Mustermann",
    "first_name": "Max",
    "email": "max@example.org",
    "phone_number": "+49123456789",
    "nfc_id": "04AABBCCDD",
    "last_safety_briefing": "2026-07-02",
    "wh_employee": false,
    "pupil": false,
    "student": true,
    "staff": false,
    "visits": 0,
    "hours_on_site": 0
  }'
```

Fetch:

```sh
curl http://localhost/api/v1/guests/1 \
  -H 'X-API-Key: change-me-to-a-long-random-token'
```

## Environment variables

- `API_KEY` - required for protected endpoints.
- `CREATE_TABLES_ON_STARTUP` - creates tables automatically on startup for early development. Keep enabled locally; replace with Alembic migrations later.
- `DATABASE_URL` - preferred full SQLAlchemy database URL.
- `POSTGRES_DB`
- `POSTGRES_USER`
- `POSTGRES_PASSWORD`
- `POSTGRES_HOST` - defaults to `postgres`.
- `POSTGRES_PORT` - defaults to `5432`.

If `DATABASE_URL` is set, it is used directly. Otherwise, the app builds a PostgreSQL URL for SQLAlchemy/psycopg.

## Run locally with Python

```sh
cd api
python -m venv .venv
. .venv/bin/activate
pip install -r requirements.txt
API_KEY=dev-token uvicorn app.main:app --reload
```

## Run with Docker Compose

From `infra/`, copy the environment example first:

```sh
cp .env.example .env
```

Edit `.env` and set a strong `POSTGRES_PASSWORD` and `API_KEY`.

Then start the full topology including the API profile:

```sh
docker compose --env-file .env --profile api up -d --build
```

Check health through nginx:

```sh
curl http://localhost/api/v1/health
```

## Database integration notes

`app/db.py` creates the SQLAlchemy engine lazily. Tables are currently created on startup when `CREATE_TABLES_ON_STARTUP=true`, which is useful for early development. The intended next step is adding Alembic migrations before the schema becomes stable.
