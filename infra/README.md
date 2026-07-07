# Infrastructure

Docker-based infrastructure for the makerspace access system.

## Services

- `postgres`: PostgreSQL database for core business data such as users, permissions, devices, NFC credentials, and access events. It is only attached to the internal backend network and is not exposed directly to the host.
- `nginx`: Public HTTP entry point on port `80` by default. It provides a simple health endpoint and reverse proxies `/api/` to the future backend API service.
- `api`: FastAPI backend skeleton. It is disabled by default via the `api` profile so the infrastructure can run without application code during early setup.

## Environment setup

From the repository root:

```sh
cp infra/.env.example infra/.env
```

Then edit `infra/.env` and change at least `POSTGRES_PASSWORD` before using the stack beyond local experiments.

## Start the stack

From `infra/`:

```sh
docker compose --env-file .env up -d postgres nginx
```

The initial stack starts PostgreSQL and nginx only. The API placeholder is disabled by default via the `api` profile.

Start the FastAPI backend topology with:

```sh
docker compose --env-file .env --profile api up -d --build
```

## Inspect logs

From `infra/`:

```sh
docker compose --env-file .env logs -f nginx
docker compose --env-file .env logs -f postgres
```

For all running services:

```sh
docker compose --env-file .env logs -f
```

## Persistent storage

Named Docker volumes are used for persistent service data:

- `postgres_data`: PostgreSQL database files.
- `nginx_cache`: nginx cache/runtime cache data.
- `nginx_logs`: nginx access and error logs.

These volumes survive container recreation. To remove them intentionally, use Docker volume commands or `docker compose down -v`. Do not use `down -v` unless you are prepared to delete local database data.

## Networks

- `frontend`: Bridge network for public-facing services. nginx publishes host port `80` from this network.
- `backend`: Internal-only bridge network for service-to-service traffic between nginx, the future API, and PostgreSQL.

## Files that should not be committed

Do not commit:

- `infra/.env`
- Any files containing real passwords, tokens, private keys, or production hostnames.
- Local database dumps unless explicitly sanitized.

The example file `infra/.env.example` is safe to commit and documents required variables.

## Notes

- HTTPS/certificates are intentionally not configured yet.
- Business logic is intentionally not implemented here.
- PostgreSQL is preferred for core relational business data. Time-series or analytics databases can be added later only if a specific need emerges.
