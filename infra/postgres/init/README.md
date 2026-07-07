# PostgreSQL initialization

Place optional PostgreSQL initialization scripts in this directory.

Files here are mounted read-only into `/docker-entrypoint-initdb.d` and are executed by the official PostgreSQL image only when the `postgres_data` volume is first created.

Supported examples:

- `001_schema.sql`
- `002_seed_dev_data.sql`
- executable `.sh` scripts

Do not commit production secrets or environment-specific credentials here.
